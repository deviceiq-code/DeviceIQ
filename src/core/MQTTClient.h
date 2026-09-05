#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>

#include "components/Component.h"

class mqttclient final {
    public:
        mqttclient();
        mqttclient(const mqttclient&) = delete;
        mqttclient& operator=(const mqttclient&) = delete;

        [[nodiscard]] bool Start() noexcept;
        [[nodiscard]] bool Enabled() const noexcept { return pEnabled; }
        [[nodiscard]] bool Connected() noexcept { return pClient.connected(); }
        [[nodiscard]] bool Notify(const ComponentEvent& event) noexcept;
        [[nodiscard]] uint32_t TakeDroppedEvents() noexcept { return pDroppedEvents.exchange(0, std::memory_order_relaxed); }
        // Thread-safe, like Notify() - callable from whatever task removed the
        // component (Telnet or the web server), never from the MQTT task
        // itself. Queued rather than published inline because pClient (and
        // every Publish*Discovery call) is only ever touched from Task().
        [[nodiscard]] bool RequestDiscoveryRemoval(component::Classes componentClass, int16_t id) noexcept;

    private:
        struct DiscoveryRemoval {
            component::Classes componentClass;
            int16_t id;
        };

        static constexpr UBaseType_t EVENT_QUEUE_LENGTH = 32;
        static constexpr UBaseType_t DISCOVERY_REMOVAL_QUEUE_LENGTH = 16;
        static constexpr uint32_t TASK_STACK_SIZE = 6144;
        static constexpr UBaseType_t TASK_PRIORITY = 2;
        static constexpr uint16_t MQTT_BUFFER_SIZE = 2048;
        static constexpr uint16_t MQTT_KEEP_ALIVE_SECONDS = 30;
        static constexpr uint16_t MQTT_SOCKET_TIMEOUT_SECONDS = 5;
        static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;

        static mqttclient* pActiveInstance;
        static void TaskEntry(void* parameter);
        static void MessageCallback(char* topic, uint8_t* payload, unsigned int length);

        void Task();
        [[nodiscard]] bool Connect() noexcept;
        void ProcessEvents();
        void DiscardEvents();
        void ProcessEvent(const ComponentEvent& event);
        void ProcessDiscoveryRemovals();
        // Publishes an empty, retained payload to every discovery config
        // topic a component of this class could have had - the MQTT
        // discovery protocol's own way to tell Home Assistant to forget an
        // entity. Safe to call for a component that never had discovery
        // published (private Blinds member, or discovery was disabled at
        // the time) - clearing a topic with nothing retained is a no-op.
        void UnpublishDiscovery(component::Classes componentClass, int16_t id);
        void HandleMessage(const String& topic, const uint8_t* payload, size_t length);
        void PublishAvailability(const char* state);
        void PublishAllStates();
        void PublishComponentState(const component& item);
        void PublishDiscovery();
        void PublishRelayDiscovery(const component& item);
        void PublishButtonDiscovery(const component& item);
        void PublishThermometerDiscovery(const component& item);
        void PublishBlindsDiscovery(const component& item);
        void AddDiscoveryMetadata(JsonDocument& document, const component& item, const String& uniqueId);
        [[nodiscard]] bool Publish(const String& topic, const String& payload, bool retained = false);
        [[nodiscard]] String ComponentTopic(const component& item, const char* direction, const char* property) const;
        [[nodiscard]] String AvailabilityTopic() const;
        [[nodiscard]] String UniqueID(const component& item, const char* suffix) const;
        [[nodiscard]] String UniqueID(int16_t id, const char* suffix) const;
        [[nodiscard]] static bool ValidTopicSegment(const String& value) noexcept;
        [[nodiscard]] static String DeviceIdentifier();

        WiFiClient pNetworkClient;
        PubSubClient pClient;
        String pBroker;
        String pUser;
        String pPassword;
        String pHostname;
        String pDiscoveryPrefix;
        String pDeviceID;
        uint16_t pPort = 1883;
        bool pEnabled = false;
        bool pDiscoveryEnabled = true;
        bool pDiscoveryRequested = false;
        TickType_t pLastConnectAttempt = 0;
        uint32_t pConsecutiveConnectFailures = 0;
        uint32_t pConsecutivePublishFailures = 0;
        std::atomic<uint32_t> pDroppedEvents{0};
        TaskHandle_t pTaskHandle = nullptr;

        StaticQueue_t pEventQueueControl{};
        uint8_t pEventQueueStorage[EVENT_QUEUE_LENGTH * sizeof(ComponentEvent)]{};
        QueueHandle_t pEventQueue = nullptr;

        StaticQueue_t pDiscoveryRemovalQueueControl{};
        uint8_t pDiscoveryRemovalQueueStorage[DISCOVERY_REMOVAL_QUEUE_LENGTH * sizeof(DiscoveryRemoval)]{};
        QueueHandle_t pDiscoveryRemovalQueue = nullptr;
};
