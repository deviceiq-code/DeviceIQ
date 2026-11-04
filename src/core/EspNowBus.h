#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "EspNowProtocol.h"

// Controller side of the ESP-NOW component bus. Components never touch the
// radio: an output records its desired level here and an input reads its last
// reported level, and the "EspNow" task reconciles that against each node.
// Nothing on this class's public surface blocks on the radio, so it's safe to
// call from component Control()/HandleCommand().
class espnowbus {
    public:
        static constexpr size_t MAX_NODES = 12;
        static constexpr size_t MAX_PENDING = 6;
        static constexpr size_t MAX_NAME_LENGTH = 24;

        struct Port {
            int8_t node = -1;
            uint8_t pin = 0;
        };

        struct NodeInfo {
            String name;
            String mac;
            bool online = false;
            bool configSynced = false;
            bool configRejected = false;
            uint16_t firmware = 0;
            uint16_t pinMask = 0;
            uint8_t ports = 0;
            uint32_t lastSeenSeconds = 0;
            bool seen = false;
        };

        struct PendingInfo {
            String mac;
            uint16_t firmware = 0;
            uint16_t pinMask = 0;
            uint32_t lastSeenSeconds = 0;
        };

        enum class AdoptionState : uint8_t { Idle, Working, Succeeded, Failed };

        espnowbus() noexcept;
        espnowbus(const espnowbus&) = delete;
        espnowbus& operator=(const espnowbus&) = delete;

        // Load() must run before components are configured, so they can attach
        // to their nodes; Start() after, so the first Hello from a node is
        // answered with the complete port table.
        [[nodiscard]] bool Load();
        [[nodiscard]] bool Start();
        [[nodiscard]] bool Running() const noexcept { return pTaskHandle != nullptr; }
        [[nodiscard]] uint8_t Channel() const noexcept;

        // An unknown node or a full port table returns an invalid Port and
        // logs it, instead of failing: one bad remote component must not stop
        // every local one from starting. Such a port just reads as offline.
        [[nodiscard]] Port AttachOutput(const String& node, uint8_t pin, bool safeLevel, uint16_t autoOffMs) noexcept;
        [[nodiscard]] Port AttachInput(const String& node, uint8_t pin, bool pullUp) noexcept;

        // With requireOnline, refuses (and records nothing) while the node is
        // unreachable. Without it the level is recorded and delivered when the
        // node comes back.
        [[nodiscard]] bool SetOutput(const Port& port, bool level, bool requireOnline) noexcept;
        [[nodiscard]] bool InputLevel(const Port& port, bool& level) const noexcept;
        [[nodiscard]] bool Online(const Port& port) const noexcept;

        [[nodiscard]] bool HasNode(const String& name) const noexcept;
        [[nodiscard]] size_t NodeCount() const noexcept;
        [[nodiscard]] bool GetNode(size_t index, NodeInfo& info) const noexcept;
        [[nodiscard]] size_t PendingCount() const noexcept;
        [[nodiscard]] bool GetPending(size_t index, PendingInfo& info) const noexcept;

        // Adoption is only possible while the discovery window is open.
        void OpenDiscovery(uint16_t seconds = DISCOVERY_WINDOW_SECONDS) noexcept;
        void CloseDiscovery() noexcept;
        [[nodiscard]] uint32_t DiscoverySecondsLeft() const noexcept;

        // Adopt and Replace only start the exchange: follow it through
        // Adoption(). Replace hands an existing node's identity (name, and
        // so every component referencing it) to a different board.
        [[nodiscard]] bool Adopt(const String& mac, const String& name, String& error) noexcept;
        [[nodiscard]] bool Replace(const String& name, const String& mac, String& error) noexcept;
        [[nodiscard]] AdoptionState Adoption(String& message) const noexcept;

        [[nodiscard]] bool Identify(const String& selector, String& error) noexcept;
        [[nodiscard]] bool Rename(const String& name, const String& newName, String& error) noexcept;
        [[nodiscard]] bool Remove(const String& name, String& error) noexcept;

        [[nodiscard]] static bool IsValidName(const String& name) noexcept;

    private:
        class Lock {
            public:
                explicit Lock(SemaphoreHandle_t mutex) noexcept : pMutex(mutex), pLocked(mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {}
                ~Lock() { if (pLocked) xSemaphoreGive(pMutex); }
                Lock(const Lock&) = delete;
                Lock& operator=(const Lock&) = delete;
                [[nodiscard]] bool IsLocked() const noexcept { return pLocked; }
            private:
                SemaphoreHandle_t pMutex;
                bool pLocked;
        };

        struct Node {
            bool used = false;
            String name;
            uint8_t mac[6] = {0};
            uint8_t key[espnowprotocol::KeyLength] = {0};

            espnowprotocol::PortConfig ports[espnowprotocol::MaxPorts]{};
            uint8_t portCount = 0;
            uint16_t desired = 0;
            uint16_t reported = 0;
            bool reportedKnown = false;

            // A Hello only proposes a session: it's replayable, so the node's
            // nonce stays a candidate until a frame tagged with it arrives.
            uint32_t controllerNonce = 0;
            uint32_t nodeNonce = 0;
            uint32_t candidateNonce = 0;
            uint32_t candidateCrc = 0;
            uint32_t reportedCrc = 0;
            uint32_t rxSequence = 0;
            uint32_t txSequence = 0;
            bool session = false;

            bool online = false;
            bool seen = false;
            bool configSynced = false;
            bool configRejected = false;
            bool identifyRequested = false;
            uint8_t forgetFrames = 0;
            uint16_t firmware = 0;
            uint16_t pinMask = 0;
            TickType_t lastHeardAt = 0;
            TickType_t nextSyncAt = 0;
        };

        struct Pending {
            bool used = false;
            uint8_t mac[6] = {0};
            uint16_t firmware = 0;
            uint16_t pinMask = 0;
            bool identifyRequested = false;
            TickType_t lastHeardAt = 0;
        };

        struct AdoptionJob {
            bool active = false;
            int8_t replaces = -1;
            String name;
            uint8_t mac[6] = {0};
            uint8_t key[espnowprotocol::KeyLength] = {0};
            uint32_t challenge = 0;
            uint8_t attempts = 0;
            TickType_t nextAttemptAt = 0;
        };

        struct ReceivedFrame {
            uint8_t mac[6];
            uint8_t length;
            uint8_t data[espnowprotocol::MaxFrameLength];
        };

        struct OutgoingFrame {
            uint8_t mac[6];
            uint8_t data[espnowprotocol::MaxFrameLength];
            size_t length = 0;
        };

        static constexpr uint16_t DISCOVERY_WINDOW_SECONDS = 180;
        static constexpr uint32_t TASK_STACK_SIZE = 8192;
        static constexpr UBaseType_t TASK_PRIORITY = 2;
        static constexpr UBaseType_t RECEIVE_QUEUE_LENGTH = 8;
        static constexpr TickType_t IDLE_WAIT = pdMS_TO_TICKS(50);
        static constexpr TickType_t RADIO_CHECK_INTERVAL = pdMS_TO_TICKS(1000);
        static constexpr TickType_t CONFIG_RETRY_INTERVAL = pdMS_TO_TICKS(300);
        static constexpr TickType_t SET_RETRY_INTERVAL = pdMS_TO_TICKS(150);
        static constexpr TickType_t OFFLINE_TIMEOUT = pdMS_TO_TICKS(35000);
        static constexpr TickType_t PENDING_TIMEOUT = pdMS_TO_TICKS(30000);
        static constexpr TickType_t ADOPT_RETRY_INTERVAL = pdMS_TO_TICKS(300);
        static constexpr uint8_t ADOPT_MAX_ATTEMPTS = 15;
        static constexpr uint8_t FORGET_FRAME_COUNT = 3;

        static void TaskEntry(void* parameter);
        static void OnReceive(const uint8_t* mac, const uint8_t* data, int length);
        void Task();
        void Wake() noexcept;

        void ServiceRadio();
        bool StartRadio();
        void StopRadio();
        void EnsurePeer(const uint8_t* mac);
        void Send(const OutgoingFrame& frame);

        void HandleFrame(const ReceivedFrame& frame);
        void HandleAnnounce(const ReceivedFrame& frame, const uint8_t* body, size_t bodyLength);
        void HandleAdopted(const ReceivedFrame& frame, const uint8_t* body, size_t bodyLength);
        void HandleHello(Node& node, const ReceivedFrame& frame, const uint8_t* body, size_t bodyLength, OutgoingFrame& reply);
        void HandleSessionFrame(Node& node, const espnowprotocol::Header& header, const uint8_t* body, size_t bodyLength, OutgoingFrame& reply);
        void Tick(TickType_t now);
        void TickAdoption(TickType_t now);
        void TickPending(TickType_t now);
        void TickNode(size_t index, TickType_t now);
        void FinishAdoption();

        size_t SealFor(Node& node, espnowprotocol::Message type, const void* body, size_t bodyLength, OutgoingFrame& frame) noexcept;
        [[nodiscard]] Node* FindByMac(const uint8_t* mac) noexcept;
        [[nodiscard]] Node* FindByName(const String& name) noexcept;
        [[nodiscard]] const Node* FindByName(const String& name) const noexcept;
        [[nodiscard]] Pending* FindPending(const uint8_t* mac) noexcept;
        [[nodiscard]] int FreeSlot() const noexcept;
        [[nodiscard]] Port Attach(const String& node, const espnowprotocol::PortConfig& config) noexcept;
        [[nodiscard]] bool BeginAdoption(const uint8_t* mac, const String& name, int8_t replaces, String& error) noexcept;
        void ResetSession(Node& node) noexcept;
        void DropPending(Pending& pending) noexcept;
        void MarkOffline(Node& node) noexcept;
        void ClearNode(Node& node) noexcept;
        [[nodiscard]] static espnowprotocol::ConfigBody BuildConfig(const Node& node) noexcept;
        [[nodiscard]] static uint32_t ConfigCrc(const Node& node) noexcept;
        [[nodiscard]] static uint16_t OutputMask(const Node& node) noexcept;
        [[nodiscard]] static bool ParseMac(const String& text, uint8_t* mac) noexcept;
        [[nodiscard]] static String FormatMac(const uint8_t* mac);

        StaticSemaphore_t pMutexStorage{};
        SemaphoreHandle_t pMutex = nullptr;
        StaticQueue_t pReceiveQueueControl{};
        uint8_t pReceiveQueueStorage[RECEIVE_QUEUE_LENGTH * sizeof(ReceivedFrame)]{};
        QueueHandle_t pReceiveQueue = nullptr;
        TaskHandle_t pTaskHandle = nullptr;

        Node pNodes[MAX_NODES];
        Pending pPending[MAX_PENDING];
        AdoptionJob pJob;
        AdoptionState pAdoptionState = AdoptionState::Idle;
        String pAdoptionMessage;
        TickType_t pDiscoveryUntil = 0;
        bool pDiscoveryOpen = false;

        bool pRadioStarted = false;
        uint8_t pInterface = 0;
        int pRadioMode = 0;
        TickType_t pNextRadioCheckAt = 0;
};
