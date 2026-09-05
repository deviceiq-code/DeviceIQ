#include "MQTTClient.h"

#include "Globals.h"
#include "components/Blinds.h"
#include "components/Button.h"
#include "components/Relay.h"
#include "components/Thermometer.h"

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

    pDiscoveryRemovalQueue = xQueueCreateStatic(
        DISCOVERY_REMOVAL_QUEUE_LENGTH, sizeof(DiscoveryRemoval), pDiscoveryRemovalQueueStorage, &pDiscoveryRemovalQueueControl
    );
    if (pDiscoveryRemovalQueue == nullptr) return false;

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
    if (xQueueSend(pEventQueue, &event, 0) == pdTRUE) return true;
    pDroppedEvents.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool mqttclient::RequestDiscoveryRemoval(component::Classes componentClass, int16_t id) noexcept {
    if (!pEnabled || pDiscoveryRemovalQueue == nullptr) return false;
    const DiscoveryRemoval removal{componentClass, id};
    return xQueueSend(pDiscoveryRemovalQueue, &removal, 0) == pdTRUE;
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
        // Left queued (not discarded) while disconnected, unlike
        // pEventQueue - a removal is still worth applying once back
        // online, whereas a stale runtime event no longer is.
        ProcessDiscoveryRemovals();
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
        ++pConsecutiveConnectFailures;
        if (pConsecutiveConnectFailures == 1 || (pConsecutiveConnectFailures % 12U) == 0U) {
            Logger.Log("MQTT connection failed (state " + String(pClient.state()) + ", attempts: " + String(pConsecutiveConnectFailures) + ")", logger::LogLevels::Warning);
        }
        return false;
    }

    Logger.Log(
        pConsecutiveConnectFailures > 0
            ? "MQTT connection recovered: " + pBroker + ":" + String(pPort)
            : "MQTT connected to " + pBroker + ":" + String(pPort),
        logger::LogLevels::Information
    );
    pConsecutiveConnectFailures = 0;
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


void mqttclient::ProcessDiscoveryRemovals() {
    DiscoveryRemoval removal;
    while (xQueueReceive(pDiscoveryRemovalQueue, &removal, 0) == pdTRUE) {
        UnpublishDiscovery(removal.componentClass, removal.id);
    }
}

void mqttclient::DiscardEvents() {
    ComponentEvent event;
    while (xQueueReceive(pEventQueue, &event, 0) == pdTRUE) {}
}
void mqttclient::ProcessEvent(const ComponentEvent& event) {
    if (event.source == nullptr || !event.source->IsPublic() || !ValidTopicSegment(event.source->Name())) return;

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
    } else if (event.source->Class() == component::Classes::Thermometer &&
        event.code == thermometer::EventCodes::Changed) {
        PublishComponentState(*event.source);
    } else if (event.source->Class() == component::Classes::Blinds && event.code == blinds::EventCodes::Changed) {
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
    if (target == nullptr || !target->IsPublic()) {
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
        if (item != nullptr && item->IsPublic() && ValidTopicSegment(item->Name())) {
            (void)Publish(pHostname + "/" + item->Name() + "/Online", state, true);
        }
    }
}

void mqttclient::PublishAllStates() {
    for (size_t index = 0; index < ComponentController.Count(); ++index) {
        const component* item = ComponentController.At(index);
        if (item != nullptr && item->IsPublic()) PublishComponentState(*item);
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
    } else if (item.Class() == component::Classes::Thermometer) {
        const thermometer& value = static_cast<const thermometer&>(item);
        if (!value.Available()) return;
        (void)Publish(ComponentTopic(item, "Get", "temperature"), String(value.Temperature(), 2), true);
        if (value.HasHumidity()) {
            (void)Publish(ComponentTopic(item, "Get", "humidity"), String(value.Humidity(), 2), true);
        }
    } else if (item.Class() == component::Classes::Blinds) {
        const blinds& value = static_cast<const blinds&>(item);
        const char* state = value.State() != blinds::Motion::Stopped ? blinds::MotionName(value.State()) :
            value.Position() == 0 ? "closed" : value.Position() == 100 ? "open" : "stopped";
        (void)Publish(ComponentTopic(item, "Get", "state"), state, true);
        (void)Publish(ComponentTopic(item, "Get", "position"), String(value.Position()), true);
    }
}

void mqttclient::PublishDiscovery() {
    if (!pDiscoveryEnabled || !pClient.connected()) return;
    for (size_t index = 0; index < ComponentController.Count(); ++index) {
        const component* item = ComponentController.At(index);
        if (item == nullptr || !item->IsPublic() || !ValidTopicSegment(item->Name())) continue;
        if (item->Class() == component::Classes::Relay) PublishRelayDiscovery(*item);
        else if (item->Class() == component::Classes::Button) PublishButtonDiscovery(*item);
        else if (item->Class() == component::Classes::Thermometer) PublishThermometerDiscovery(*item);
        else if (item->Class() == component::Classes::Blinds) PublishBlindsDiscovery(*item);
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
    eventTypes.add("Changed");
    String eventPayload;
    if (serializeJson(eventDocument, eventPayload) > 0) {
        (void)Publish(pDiscoveryPrefix + "/event/" + eventUnique + "/config", eventPayload, true);
    }
}

void mqttclient::PublishThermometerDiscovery(const component& item) {
    const thermometer& value = static_cast<const thermometer&>(item);

    const String temperatureUnique = UniqueID(item, "temperature");
    JsonDocument temperatureDocument;
    AddDiscoveryMetadata(temperatureDocument, item, temperatureUnique);
    temperatureDocument["name"] = item.Name() + " Temperature";
    temperatureDocument["stat_t"] = ComponentTopic(item, "Get", "temperature");
    temperatureDocument["dev_cla"] = "temperature";
    temperatureDocument["unit_of_meas"] = "°C";
    temperatureDocument["stat_cla"] = "measurement";
    temperatureDocument["sug_dsp_prc"] = 1;
    String temperaturePayload;
    if (serializeJson(temperatureDocument, temperaturePayload) > 0) {
        (void)Publish(pDiscoveryPrefix + "/sensor/" + temperatureUnique + "/config", temperaturePayload, true);
    }

    if (!value.HasHumidity()) return;
    const String humidityUnique = UniqueID(item, "humidity");
    JsonDocument humidityDocument;
    AddDiscoveryMetadata(humidityDocument, item, humidityUnique);
    humidityDocument["name"] = item.Name() + " Humidity";
    humidityDocument["stat_t"] = ComponentTopic(item, "Get", "humidity");
    humidityDocument["dev_cla"] = "humidity";
    humidityDocument["unit_of_meas"] = "%";
    humidityDocument["stat_cla"] = "measurement";
    humidityDocument["sug_dsp_prc"] = 1;
    String humidityPayload;
    if (serializeJson(humidityDocument, humidityPayload) > 0) {
        (void)Publish(pDiscoveryPrefix + "/sensor/" + humidityUnique + "/config", humidityPayload, true);
    }
}

void mqttclient::PublishBlindsDiscovery(const component& item) {
    const String unique = UniqueID(item, "cover");
    JsonDocument document;
    AddDiscoveryMetadata(document, item, unique);
    document["dev_cla"] = "blind";
    document["cmd_t"] = ComponentTopic(item, "Set", "state");
    document["stat_t"] = ComponentTopic(item, "Get", "state");
    document["pos_t"] = ComponentTopic(item, "Get", "position");
    document["set_pos_t"] = ComponentTopic(item, "Set", "position");
    document["pos_clsd"] = 0;
    document["pos_open"] = 100;
    document["pl_open"] = "open";
    document["pl_cls"] = "close";
    document["pl_stop"] = "stop";
    document["stat_clsd"] = "closed";
    document["stat_open"] = "open";
    document["stat_opening"] = "opening";
    document["stat_closing"] = "closing";
    document["stat_stopped"] = "stopped";
    String payload;
    if (serializeJson(document, payload) > 0) {
        (void)Publish(pDiscoveryPrefix + "/cover/" + unique + "/config", payload, true);
    }
}

// Mirrors PublishRelayDiscovery/PublishButtonDiscovery/PublishThermometerDiscovery/
// PublishBlindsDiscovery's topics exactly, empty payload instead of a real
// config - Home Assistant's own protocol for forgetting a discovered entity.
// The component is already gone by the time this runs (comp remove already
// erased it from config.json), so there's no HasHumidity() to check for
// Thermometer - clearing the humidity topic unconditionally is harmless
// even for a component that never had one.
void mqttclient::UnpublishDiscovery(component::Classes componentClass, int16_t id) {
    if (!pDiscoveryEnabled) return;

    switch (componentClass) {
        case component::Classes::Relay:
            (void)Publish(pDiscoveryPrefix + "/switch/" + UniqueID(id, "state") + "/config", "", true);
            break;
        case component::Classes::Button:
            (void)Publish(pDiscoveryPrefix + "/binary_sensor/" + UniqueID(id, "state") + "/config", "", true);
            (void)Publish(pDiscoveryPrefix + "/event/" + UniqueID(id, "event") + "/config", "", true);
            break;
        case component::Classes::Thermometer:
            (void)Publish(pDiscoveryPrefix + "/sensor/" + UniqueID(id, "temperature") + "/config", "", true);
            (void)Publish(pDiscoveryPrefix + "/sensor/" + UniqueID(id, "humidity") + "/config", "", true);
            break;
        case component::Classes::Blinds:
            (void)Publish(pDiscoveryPrefix + "/cover/" + UniqueID(id, "cover") + "/config", "", true);
            break;
        default:
            break;
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
    if (pClient.publish(topic.c_str(), payload.c_str(), retained)) {
        if (pConsecutivePublishFailures > 0) {
            Logger.Log("MQTT publishing recovered after " + String(pConsecutivePublishFailures) + " failure(s)", logger::LogLevels::Information);
            pConsecutivePublishFailures = 0;
        }
        return true;
    }
    ++pConsecutivePublishFailures;
    if (pConsecutivePublishFailures == 1 || (pConsecutivePublishFailures % 20U) == 0U) {
        Logger.Log("MQTT publish failed: " + topic + " (failures: " + String(pConsecutivePublishFailures) + ")", logger::LogLevels::Warning);
    }
    return false;
}

String mqttclient::ComponentTopic(const component& item, const char* direction, const char* property) const {
    return pHostname + "/" + item.Name() + "/" + direction + "/" + property;
}

String mqttclient::AvailabilityTopic() const {
    return pHostname + "/Online";
}

String mqttclient::UniqueID(const component& item, const char* suffix) const {
    return UniqueID(item.ID(), suffix);
}

String mqttclient::UniqueID(int16_t id, const char* suffix) const {
    return pDeviceID + "_" + String(id) + "_" + suffix;
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
