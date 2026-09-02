#include "WebhookServer.h"

#include <ArduinoJson.h>
#include <cstdlib>

#include "core/Globals.h"
#include "components/Blinds.h"
#include "components/Button.h"
#include "components/Component.h"
#include "components/Relay.h"
#include "components/Thermometer.h"

namespace {
    const char* PropertyResultMessage(ComponentPropertyResult result) {
        switch (result) {
            case ComponentPropertyResult::Accepted: return "accepted";
            case ComponentPropertyResult::PropertyNotSupported: return "property not supported";
            case ComponentPropertyResult::InvalidValue: return "invalid value";
            case ComponentPropertyResult::ComponentDisabled: return "component disabled";
            case ComponentPropertyResult::CommandRejected: return "command rejected";
            default: return "unknown error";
        }
    }

    // Mirrors HTTPServer.cpp's HandleComponentsGet per-class field logic.
    void AppendComponentJson(JsonObject entry, const component& item) {
        entry["id"] = item.ID();
        entry["name"] = item.Name();
        entry["class"] = component::ClassName(item.Class());
        entry["enabled"] = item.Enabled();

        if (item.Class() == component::Classes::Relay) {
            entry["state"] = static_cast<const relay&>(item).State();
        } else if (item.Class() == component::Classes::Button) {
            entry["state"] = static_cast<const button&>(item).State();
        } else if (item.Class() == component::Classes::Blinds) {
            const blinds& value = static_cast<const blinds&>(item);
            entry["state"] = blinds::MotionName(value.State());
            entry["position"] = value.Position();
            entry["targetPosition"] = value.TargetPosition();
        } else if (item.Class() == component::Classes::Thermometer) {
            const thermometer& value = static_cast<const thermometer&>(item);
            entry["available"] = value.Available();
            entry["hasHumidity"] = value.HasHumidity();
            if (value.Available()) {
                entry["temperature"] = value.Temperature();
                if (value.HasHumidity()) entry["humidity"] = value.Humidity();
            }
        }
    }
}

webhookserver::webhookserver() noexcept : pMutex(xSemaphoreCreateMutexStatic(&pMutexStorage)) {
    configASSERT(pMutex != nullptr);
    pPort = Defaults.Webhooks.Port;
    pEnabled = Defaults.Webhooks.Enabled;
}

webhookserver::~webhookserver() {
    Stop();
}

bool webhookserver::Start() {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;
    if (!pEnabled || pTaskHandle != nullptr) return true;

    return xTaskCreate(TaskEntry, "WebhookServer", TASK_STACK_SIZE, this, TASK_PRIORITY, &pTaskHandle) == pdPASS;
}

void webhookserver::Stop() {
    TaskHandle_t task = nullptr;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return;
        task = pTaskHandle;
    }

    if (task == nullptr || task == xTaskGetCurrentTaskHandle()) return;
    xTaskNotify(task, STOP_NOTIFICATION, eSetBits);

    const TickType_t startedAt = xTaskGetTickCount();
    while ((xTaskGetTickCount() - startedAt) < pdMS_TO_TICKS(1000)) {
        Lock lock(pMutex);
        if (lock.IsLocked() && pTaskHandle == nullptr) return;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void webhookserver::Enabled(bool value) noexcept {
    Lock lock(pMutex);
    if (lock.IsLocked()) pEnabled = value;
}

bool webhookserver::Enabled() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() ? pEnabled : false;
}

void webhookserver::Port(uint16_t value) noexcept {
    Lock lock(pMutex);
    if (lock.IsLocked()) pPort = (value == 0) ? Defaults.Webhooks.Port : value;
}

uint16_t webhookserver::Port() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() ? pPort : 0;
}

bool webhookserver::TokenValid(const String& provided) noexcept {
    const String expected = Settings.Webhooks.Token();
    if (expected.isEmpty() || provided.length() != expected.length()) return false;

    volatile uint8_t difference = 0;
    for (size_t i = 0; i < expected.length(); ++i) {
        difference |= static_cast<uint8_t>(provided[i]) ^ static_cast<uint8_t>(expected[i]);
    }
    return difference == 0;
}

void webhookserver::TaskEntry(void* parameter) {
    static_cast<webhookserver*>(parameter)->Task();
}

void webhookserver::Task() {
    uint16_t port = 81;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) {
            vTaskDelete(nullptr);
            return;
        }
        port = pPort;
    }

    WebServer server(port);
    RegisterRoutes(server);

    bool serverStarted = false;

    while (true) {
        uint32_t notifications = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notifications, 0);
        if ((notifications & STOP_NOTIFICATION) != 0) break;

        const network::APMode networkMode = Network.ConnectionMode();
        if (!serverStarted && networkMode != network::APMode::Offline) {
            server.begin();
            serverStarted = true;
        }

        if (serverStarted) server.handleClient();

        vTaskDelay(TASK_DELAY);
    }

    if (serverStarted) server.stop();

    {
        Lock lock(pMutex);
        if (lock.IsLocked()) pTaskHandle = nullptr;
    }

    vTaskDelete(nullptr);
}

void webhookserver::RegisterRoutes(WebServer& server) {
    server.on("/component/set", HTTP_GET, [this, &server]() { HandleComponentSet(server); });
    server.on("/component/set", HTTP_POST, [this, &server]() { HandleComponentSet(server); });
    server.on("/component/get", HTTP_GET, [this, &server]() { HandleComponentGet(server); });
    server.on("/component/get", HTTP_POST, [this, &server]() { HandleComponentGet(server); });
    server.onNotFound([this, &server]() { HandleNotFound(server); });
}

void webhookserver::HandleComponentSet(WebServer& server) {
    if (!TokenValid(server.arg("token"))) {
        Logger.Log("Webhooks: rejected request from " + server.client().remoteIP().toString() + ": invalid token", logger::LogLevels::Warning);
        server.send(401, "application/json", "{\"error\":\"invalid token\"}");
        return;
    }

    const String idArg = server.arg("id");
    char* end = nullptr;
    const long id = std::strtol(idArg.c_str(), &end, 10);
    const String property = server.arg("property");
    const String value = server.arg("value");
    const IPAddress remoteIP = server.client().remoteIP();

    if (end == idArg.c_str() || id < 1 || id > INT16_MAX || property.isEmpty()) {
        server.send(400, "application/json", "{\"error\":\"missing or invalid id/property\"}");
        return;
    }

    // Mirrors the web UI's /api/components/set: only components explicitly
    // marked public are reachable here - a Blinds group's private Relay and
    // Button members, for instance, can never be targeted directly.
    component* target = ComponentController.FindByID(static_cast<int16_t>(id));
    if (target == nullptr || !target->IsPublic()) {
        server.send(404, "application/json", "{\"error\":\"component not found\"}");
        return;
    }

    const ComponentPropertyResult result = target->SetProperty(property, value, pdMS_TO_TICKS(100));
    Logger.Log(
        "Webhooks: component set " + String(result == ComponentPropertyResult::Accepted ? "accepted" : "rejected") +
            " from " + remoteIP.toString() + ": " + target->Name() + "." + property + "=" + value,
        result == ComponentPropertyResult::Accepted ? logger::LogLevels::Information : logger::LogLevels::Warning
    );

    if (result != ComponentPropertyResult::Accepted) {
        server.send(422, "application/json", String("{\"error\":\"") + PropertyResultMessage(result) + "\"}");
        return;
    }

    server.send(200, "application/json", "{\"success\":true}");
}

void webhookserver::HandleComponentGet(WebServer& server) {
    if (!TokenValid(server.arg("token"))) {
        Logger.Log("Webhooks: rejected request from " + server.client().remoteIP().toString() + ": invalid token", logger::LogLevels::Warning);
        server.send(401, "application/json", "{\"error\":\"invalid token\"}");
        return;
    }

    JsonDocument document;
    const String idArg = server.arg("id");

    if (idArg.isEmpty()) {
        // No id: report every public component, mirroring the web UI's
        // /api/components.
        JsonArray components = document["components"].to<JsonArray>();
        for (size_t index = 0; index < ComponentController.Count(); ++index) {
            const component* item = ComponentController.At(index);
            if (item == nullptr || !item->IsPublic()) continue;
            AppendComponentJson(components.add<JsonObject>(), *item);
        }
    } else {
        char* end = nullptr;
        const long id = std::strtol(idArg.c_str(), &end, 10);
        if (end == idArg.c_str() || id < 1 || id > INT16_MAX) {
            server.send(400, "application/json", "{\"error\":\"invalid id\"}");
            return;
        }

        const component* target = ComponentController.FindByID(static_cast<int16_t>(id));
        if (target == nullptr || !target->IsPublic()) {
            server.send(404, "application/json", "{\"error\":\"component not found\"}");
            return;
        }

        AppendComponentJson(document.to<JsonObject>(), *target);
    }

    String payload;
    serializeJson(document, payload);
    server.send(200, "application/json", payload);
}

void webhookserver::HandleNotFound(WebServer& server) {
    server.send(404, "application/json", "{\"error\":\"not found\"}");
}
