#include "MQTTClient.h"

#include "Globals.h"
#include "components/Button.h"
#include "components/Relay.h"

mqttclient* mqttclient::pActiveInstance = nullptr;

mqttclient::mqttclient() : pClient(pNetworkClient) {}

bool mqttclient::Start() noexcept {
    if (pTaskHandle != nullptr) return true;

    pEnabled = Settings.MQTT.Enabled();
    if (!pEnabled) return true;

    pBroker = Settings.MQTT.Broker();
    pPort = Settings.MQTT.Port();
    pUser = Settings.MQTT.User();
    pPassword = Settings.MQTT.Password();
    pHostname = Network.Hostname();
    pDiscoveryEnabled = Settings.MQTT.DiscoveryEnabled();
    pDiscoveryPrefix = Settings.MQTT.DiscoveryPrefix();
    pDeviceID = DeviceIdentifier();

    if (pBroker.isEmpty() || !ValidTopicSegment(pHostname) ||
        (pDiscoveryEnabled && !ValidTopicSegment(pDiscoveryPrefix))) {
        Logger.Log("MQTT configuration is invalid", logger::LogLevels::Error);
        return false;
    }

    pEventQueue = xQueueCreateStatic(EVENT_QUEUE_LENGTH, sizeof(ComponentEvent), pEventQueueStorage, &pEventQueueControl);
    if (pEventQueue == nullptr) return false;

    pClient.setServer(pBroker.c_str(), pPort);
    pClient.setCallback(MessageCallback);
    pClient.setKeepAlive(MQTT_KEEP_ALIVE_SECONDS);
    pClient.setSocketTimeout(MQTT_SOCKET_TIMEOUT_SECONDS);
    if (!pClient.setBufferSize(MQTT_BUFFER_SIZE)) return false;

    pActiveInstance = this;
    const BaseType_t result = xTaskCreate(TaskEntry, "MQTT", TASK_STACK_SIZE, this, TASK_PRIORITY, &pTaskHandle);
    if (result != pdPASS) {
        pTaskHandle = nullptr;
        pActiveInstance = nullptr;
        return false;
    }
    return true;
}

bool mqttclient::Notify(const ComponentEvent& event) noexcept {
    if (!pEnabled || pEventQueue == nullptr || event.source == nullptr) return false;
    return xQueueSend(pEventQueue, &event, 0) == pdTRUE;
}

void mqttclient::TaskEntry(void* parameter) {
    static_cast<mqttclient*>(parameter)->Task();
}

void mqttclient::MessageCallback(char* topic, uint8_t* payload, unsigned int length) {
    if (pActiveInstance != nullptr && topic != nullptr) pActiveInstance->HandleMessage(topic, payload, length);
}

void mqttclient::Task() {
    while (true) {
        if (Network.ConnectionMode() != network::APMode::WifiClient || WiFi.status() != WL_CONNECTED) {
            if (pClient.connected()) pClient.disconnect();
            DiscardEvents();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!pClient.connected()) {
            const TickType_t now = xTaskGetTickCount();
            if (static_cast<TickType_t>(now - pLastConnectAttempt) >= pdMS_TO_TICKS(RECONNECT_INTERVAL_MS)) {
                pLastConnectAttempt = now;
                (void)Connect();
            }
            if (!pClient.connected()) DiscardEvents();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        (void)pClient.loop();
        if (pDiscoveryRequested) {
            pDiscoveryRequested = false;
            PublishDiscovery();
            PublishAllStates();
        }
        ProcessEvents();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool mqttclient::Connect() noexcept {
    const String availability = AvailabilityTopic();
    const String clientID = pDeviceID;
    const bool connected = pUser.isEmpty()
        ? pClient.connect(clientID.c_str(), availability.c_str(), 0, true, "offline")
        : pClient.connect(clientID.c_str(), pUser.c_str(), pPassword.c_str(), availability.c_str(), 0, true, "offline");

    if (!connected) {
        Logger.Log("MQTT connection failed (state " + String(pClient.state()) + ")", logger::LogLevels::Warning);
        return false;
    }

    Logger.Log("MQTT connected to " + pBroker + ":" + String(pPort), logger::LogLevels::Information);
    (void)Publish(availability, "online", true);
    PublishAvailability("online");

    if (!pClient.subscribe((pHostname + "/+/Set/+").c_str())) {
        Logger.Log("MQTT command subscription failed", logger::LogLevels::Warning);
    }

    if (pDiscoveryEnabled) {
        (void)pClient.subscribe((pDiscoveryPrefix + "/status").c_str());
        PublishDiscovery();
    }
    PublishAllStates();
    return true;
}

void mqttclient::ProcessEvents() {
    ComponentEvent event;
    while (xQueueReceive(pEventQueue, &event, 0) == pdTRUE) ProcessEvent(event);
}


void mqttclient::DiscardEvents() {
    ComponentEvent event;
    while (xQueueReceive(pEventQueue, &event, 0) == pdTRUE) {}
}
void mqttclient::ProcessEvent(const ComponentEvent& event) {
    if (event.source == nullptr || !ValidTopicSegment(event.source->Name())) return;

    String eventName;
    if (event.source->ResolveEvent(event.code, eventName)) {
        JsonDocument document;
        document["event_type"] = eventName;
        document["value"] = event.value;
        String payload;
        if (serializeJson(document, payload) > 0) {
            (void)Publish(ComponentTopic(*event.source, "Get", "event"), payload, false);
        }
    }

    if (event.source->Class() == component::Classes::Relay && event.code == relay::EventCodes::Changed) {
        PublishComponentState(*event.source);
    } else if (event.source->Class() == component::Classes::Button &&
        (event.code == button::EventCodes::Pressed || event.code == button::EventCodes::Released)) {
        PublishComponentState(*event.source);
    }
}

void mqttclient::HandleMessage(const String& topic, const uint8_t* payload, size_t length) {
    if (pDiscoveryEnabled && topic == pDiscoveryPrefix + "/status") {
        String status;
        for (size_t index = 0; index < length; ++index) status += static_cast<char>(payload[index]);
        status.trim();
        if (status.equalsIgnoreCase("online")) pDiscoveryRequested = true;
        return;
    }

    if (length == 0 || length > 64 || !topic.startsWith(pHostname + "/")) return;
    const String relative = topic.substring(pHostname.length() + 1);
    const int first = relative.indexOf('/');
    const int second = first < 0 ? -1 : relative.indexOf('/', first + 1);
    if (first <= 0 || second <= first + 1 || relative.indexOf('/', second + 1) >= 0) return;

    const String componentName = relative.substring(0, first);
    const String direction = relative.substring(first + 1, second);
    const String property = relative.substring(second + 1);
    if (!direction.equalsIgnoreCase("Set") || property.isEmpty()) return;

    component* target = ComponentController.FindByName(componentName);
    if (target == nullptr) {
        Logger.Log("MQTT target component not found: " + componentName, logger::LogLevels::Warning);
        return;
    }

    String value;
    value.reserve(length);
    for (size_t index = 0; index < length; ++index) value += static_cast<char>(payload[index]);
    value.trim();

    if (target->SetProperty(property, value, pdMS_TO_TICKS(100)) != ComponentPropertyResult::Accepted) {
        Logger.Log("MQTT command rejected: " + target->Name() + "." + property + "=" + value, logger::LogLevels::Warning);
    }
}

void mqttclient::PublishAvailability(const char* state) {
    for (size_t index = 0; index < ComponentController.Count(); ++index) {
        const component* item = ComponentController.At(index);
        if (item != nullptr && ValidTopicSegment(item->Name())) {
            (void)Publish(pHostname + "/" + item->Name() + "/Online", state, true);
        }
    }
}

void mqttclient::PublishAllStates() {
    for (size_t index = 0; index < ComponentController.Count(); ++index) {
        const component* item = ComponentController.At(index);
        if (item != nullptr) PublishComponentState(*item);
    }
}

void mqttclient::PublishComponentState(const component& item) {
    if (!ValidTopicSegment(item.Name())) return;
    if (item.Class() == component::Classes::Relay) {
        const relay& value = static_cast<const relay&>(item);
        (void)Publish(ComponentTopic(item, "Get", "state"), value.State() ? "on" : "off", true);
    } else if (item.Class() == component::Classes::Button) {
        const button& value = static_cast<const button&>(item);
        (void)Publish(ComponentTopic(item, "Get", "state"), value.State() ? "pressed" : "released", true);
    }
}

void mqttclient::PublishDiscovery() {
    if (!pDiscoveryEnabled || !pClient.connected()) return;
    for (size_t index = 0; index < ComponentController.Count(); ++index) {
        const component* item = ComponentController.At(index);
        if (item == nullptr || !ValidTopicSegment(item->Name())) continue;
        if (item->Class() == component::Classes::Relay) PublishRelayDiscovery(*item);
        else if (item->Class() == component::Classes::Button) PublishButtonDiscovery(*item);
    }
}

void mqttclient::PublishRelayDiscovery(const component& item) {
    const String unique = UniqueID(item, "state");
    JsonDocument document;
    AddDiscoveryMetadata(document, item, unique);
    document["cmd_t"] = ComponentTopic(item, "Set", "state");
    document["stat_t"] = ComponentTopic(item, "Get", "state");
    document["pl_on"] = "on";
    document["pl_off"] = "off";
    document["stat_on"] = "on";
    document["stat_off"] = "off";
    String payload;
    if (serializeJson(document, payload) > 0) {
        (void)Publish(pDiscoveryPrefix + "/switch/" + unique + "/config", payload, true);
    }
}

void mqttclient::PublishButtonDiscovery(const component& item) {
    const String stateUnique = UniqueID(item, "state");
    JsonDocument stateDocument;
    AddDiscoveryMetadata(stateDocument, item, stateUnique);
    stateDocument["stat_t"] = ComponentTopic(item, "Get", "state");
    stateDocument["pl_on"] = "pressed";
    stateDocument["pl_off"] = "released";
    String statePayload;
    if (serializeJson(stateDocument, statePayload) > 0) {
        (void)Publish(pDiscoveryPrefix + "/binary_sensor/" + stateUnique + "/config", statePayload, true);
    }

    const String eventUnique = UniqueID(item, "event");
    JsonDocument eventDocument;
    AddDiscoveryMetadata(eventDocument, item, eventUnique);
    eventDocument["name"] = item.Name() + " Events";
    eventDocument["stat_t"] = ComponentTopic(item, "Get", "event");
    JsonArray eventTypes = eventDocument["event_types"].to<JsonArray>();
    eventTypes.add("Pressed");
    eventTypes.add("Released");
    eventTypes.add("Clicked");
    eventTypes.add("LongClicked");
    eventTypes.add("DoubleClicked");
    eventTypes.add("TripleClicked");
    String eventPayload;
    if (serializeJson(eventDocument, eventPayload) > 0) {
        (void)Publish(pDiscoveryPrefix + "/event/" + eventUnique + "/config", eventPayload, true);
    }
}

void mqttclient::AddDiscoveryMetadata(JsonDocument& document, const component& item, const String& uniqueId) {
    document["name"] = item.Name();
    document["uniq_id"] = uniqueId;
    document["avty_t"] = AvailabilityTopic();
    document["pl_avail"] = "online";
    document["pl_not_avail"] = "offline";
    JsonObject device = document["dev"].to<JsonObject>();
    device["ids"] = pDeviceID;
    device["name"] = pHostname;
    device["mf"] = Version::ProductFamily;
    device["mdl"] = Version::ProductName;
    device["sw"] = Version::Software::Info();
    device["hw"] = Version::Hardware::Info();
    device["sn"] = Version::SerialNumber();
    JsonObject origin = document["o"].to<JsonObject>();
    origin["name"] = Version::ProductFamily;
    origin["sw"] = Version::Software::Info();
}

bool mqttclient::Publish(const String& topic, const String& payload, bool retained) {
    if (!pClient.connected() || topic.isEmpty()) return false;
    if (pClient.publish(topic.c_str(), payload.c_str(), retained)) return true;
    Logger.Log("MQTT publish failed: " + topic, logger::LogLevels::Warning);
    return false;
}

String mqttclient::ComponentTopic(const component& item, const char* direction, const char* property) const {
    return pHostname + "/" + item.Name() + "/" + direction + "/" + property;
}

String mqttclient::AvailabilityTopic() const {
    return pHostname + "/Online";
}

String mqttclient::UniqueID(const component& item, const char* suffix) const {
    return pDeviceID + "_" + String(item.ID()) + "_" + suffix;
}

bool mqttclient::ValidTopicSegment(const String& value) noexcept {
    return !value.isEmpty() && value.indexOf('/') < 0 && value.indexOf('+') < 0 && value.indexOf('#') < 0;
}

String mqttclient::DeviceIdentifier() {
    String identifier = Network.MAC_Address();
    identifier.toLowerCase();
    identifier.replace(":", "");
    identifier.replace("-", "");
    if (identifier.isEmpty()) {
        const uint64_t chip = ESP.getEfuseMac();
        char fallback[13];
        snprintf(fallback, sizeof(fallback), "%04x%08x", static_cast<uint16_t>(chip >> 32), static_cast<uint32_t>(chip));
        identifier = fallback;
    }
    return "deviceiq_" + identifier;
}
