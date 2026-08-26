#pragma once

#include <functional>
#include <utility>

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

class network {
    public:
        using callback_t = std::function<void()>;
        enum APMode : uint8_t { Offline = 0, WifiClient, SoftAP };

        network() noexcept;
        network(const network&) = delete;
        network& operator=(const network&) = delete;

        [[nodiscard]] bool Start();
        [[nodiscard]] APMode Connect();

        void ConnectionTimeout(uint16_t value) noexcept;
        [[nodiscard]] uint16_t ConnectionTimeout() const noexcept;

        void ReconnectEnabled(bool value) noexcept;
        [[nodiscard]] bool ReconnectEnabled() const noexcept;
        void ReconnectInitialInterval(uint16_t value) noexcept;
        [[nodiscard]] uint16_t ReconnectInitialInterval() const noexcept;
        void ReconnectMaximumInterval(uint16_t value) noexcept;
        [[nodiscard]] uint16_t ReconnectMaximumInterval() const noexcept;
        void FallbackAPEnabled(bool value) noexcept;
        [[nodiscard]] bool FallbackAPEnabled() const noexcept;
        void FallbackAPRetention(uint16_t value) noexcept;
        [[nodiscard]] uint16_t FallbackAPRetention() const noexcept;

        void DHCP_Client(bool value) noexcept;
        [[nodiscard]] bool DHCP_Client() const noexcept;

        void SSID(String value);
        [[nodiscard]] String SSID() const;
        [[nodiscard]] bool Passphrase(String value);
        [[nodiscard]] String Passphrase() const;

        void SoftAP_SSID(String value);
        [[nodiscard]] String SoftAP_SSID() const;
        [[nodiscard]] bool SoftAP_Password(String value);
        [[nodiscard]] String SoftAP_Password() const;

        void Hostname(String value);
        [[nodiscard]] String Hostname() const;

        [[nodiscard]] bool IP_Address(const String& value) noexcept;
        void IP_Address(IPAddress value) noexcept;
        [[nodiscard]] IPAddress IP_Address() const noexcept;

        [[nodiscard]] bool Netmask(const String& value) noexcept;
        void Netmask(IPAddress value) noexcept;
        void Netmask(uint8_t cidr) noexcept;
        [[nodiscard]] IPAddress Netmask() const noexcept;

        [[nodiscard]] bool Gateway(const String& value) noexcept;
        void Gateway(IPAddress value) noexcept;
        [[nodiscard]] IPAddress Gateway() const noexcept;

        void DNS_Server(uint8_t index, IPAddress value) noexcept;
        [[nodiscard]] IPAddress DNS_Server(uint8_t index) const noexcept;

        [[nodiscard]] int32_t RSSI() const noexcept;
        [[nodiscard]] APMode ConnectionMode() const noexcept;
        [[nodiscard]] String MAC_Address() const;
        [[nodiscard]] IPAddress CurrentNetmask() const noexcept;
        [[nodiscard]] IPAddress CurrentDNS_Server(uint8_t index) const noexcept;

        void OnModeChanged(callback_t callback);

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

        struct Configuration {
            uint16_t connectionTimeout;
            bool reconnectEnabled;
            uint16_t reconnectInitialInterval;
            uint16_t reconnectMaximumInterval;
            bool fallbackAPEnabled;
            uint16_t fallbackAPRetention;
            bool dhcpClient;
            String ssid;
            String passphrase;
            String softAPSSID;
            String softAPPassword;
            String hostname;
            IPAddress ipAddress;
            IPAddress netmask;
            IPAddress gateway;
            IPAddress dns[2];
        };

        enum NotificationBits : uint32_t { ConnectRequested = 1UL << 0, ConfigurationChanged = 1UL << 1, ControlChanged = 1UL << 2, WiFiEventReceived = 1UL << 3 };

        static constexpr uint32_t MODE_CHECK_INTERVAL_MS = 1000;
        static constexpr uint32_t TASK_POLL_INTERVAL_MS = 250;
        static constexpr uint32_t TASK_STACK_SIZE = 4096;
        static constexpr UBaseType_t TASK_PRIORITY = 2;

        static bool ParseIPAddress(const String& value, IPAddress& destination) noexcept;
        static bool IsPrintableASCII(const String& value) noexcept;
        static bool IsHex64(const String& value) noexcept;
        static bool IsValidStationPassword(const String& value) noexcept;
        static bool IsValidSoftAPPassword(const String& value) noexcept;
        static TickType_t SecondsToTicks(uint16_t seconds) noexcept;
        static bool TimeReached(TickType_t now, TickType_t target) noexcept;

        static void TaskEntry(void* parameter) { static_cast<network*>(parameter)->Task(); }
        void Task();
        void Control();
        APMode ConnectInternal();
        bool BeginStationConnection(const Configuration& configuration);
        bool StartSoftAP(const Configuration& configuration);
        void StopSoftAP();
        void ScheduleReconnect(const Configuration& configuration, TickType_t now);
        void UpdateConnectionState();
        void Notify(uint32_t bits) noexcept;
        [[nodiscard]] Configuration ConfigurationSnapshot() const;

        StaticSemaphore_t pMutexStorage{};
        SemaphoreHandle_t pMutex = nullptr;
        TaskHandle_t pTaskHandle = nullptr;
        wifi_event_id_t pWiFiEventId = 0;

        uint32_t pPendingNotifications = 0;
        TickType_t pLastModeCheck = 0;

        // Configuration. Timeout values are expressed in seconds.
        uint16_t pConnectionTimeout = 30;
        bool pReconnectEnabled = true;
        uint16_t pReconnectInitialInterval = 5;
        uint16_t pReconnectMaximumInterval = 60;
        bool pFallbackAPEnabled = true;
        uint16_t pFallbackAPRetention = 300;
        bool pDHCP_Client = true;

        String pSSID;
        String pPassphrase;
        String pSoftAP_SSID = "DeviceIQ";
        String pSoftAP_Password;
        String pHostname = "deviceiq";

        IPAddress pIP_Address{0, 0, 0, 0};
        IPAddress pNetmask{255, 255, 255, 0};
        IPAddress pGateway{0, 0, 0, 0};
        IPAddress pDNS_Server[2]{IPAddress(8, 8, 8, 8), IPAddress(8, 8, 4, 4)};

        // Runtime state, updated only by the network task.
        bool pStationConnected = false;
        bool pStationConnectionPending = false;
        bool pSoftAPActive = false;
        TickType_t pStationConnectionStartedAt = 0;
        TickType_t pStationConnectedAt = 0;
        TickType_t pNextReconnectAt = 0;
        uint16_t pCurrentReconnectInterval = 0;
        APMode pConnectionMode = APMode::Offline;
        IPAddress pCurrentIPAddress{0, 0, 0, 0};
        IPAddress pCurrentNetmask{0, 0, 0, 0};
        IPAddress pCurrentGateway{0, 0, 0, 0};
        IPAddress pCurrentDNS_Server[2]{IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0)};
        int32_t pRSSI = 0;
        String pMACAddress;
        callback_t pOnModeChanged;
        bool pStatusPublished = false;
};
