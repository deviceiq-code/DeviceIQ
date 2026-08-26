#include "Network.h"

network::network() noexcept : pMutex(xSemaphoreCreateMutexStatic(&pMutexStorage)) {
    configASSERT(pMutex != nullptr);
}

bool network::Start() {
    if (pTaskHandle != nullptr) return true;

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);

    pWiFiEventId = WiFi.onEvent([this](arduino_event_id_t, arduino_event_info_t) { Notify(NotificationBits::WiFiEventReceived); });

    const BaseType_t result = xTaskCreate(TaskEntry, "Network", TASK_STACK_SIZE, this, TASK_PRIORITY, &pTaskHandle);

    if (result != pdPASS) {
        if (pWiFiEventId != 0) {
            WiFi.removeEvent(pWiFiEventId);
            pWiFiEventId = 0;
        }
        pTaskHandle = nullptr;
        return false;
    }

    return true;
}

network::APMode network::Connect() {
    Notify(NotificationBits::ConnectRequested);

    return ConnectionMode();
}

void network::ConnectionTimeout(uint16_t value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;

    pConnectionTimeout = value;

    Notify(NotificationBits::ControlChanged);
}

uint16_t network::ConnectionTimeout() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() ? pConnectionTimeout : 0;
}

void network::ReconnectEnabled(bool value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;

    pReconnectEnabled = value;

    Notify(NotificationBits::ControlChanged);
}

bool network::ReconnectEnabled() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() && pReconnectEnabled;
}

void network::ReconnectInitialInterval(uint16_t value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    pReconnectInitialInterval = value == 0 ? 1 : value;

    Notify(NotificationBits::ControlChanged);
}

uint16_t network::ReconnectInitialInterval() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() ? pReconnectInitialInterval : 0;
}

void network::ReconnectMaximumInterval(uint16_t value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    pReconnectMaximumInterval = value == 0 ? 1 : value;

    Notify(NotificationBits::ControlChanged);
}

uint16_t network::ReconnectMaximumInterval() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() ? pReconnectMaximumInterval : 0;
}

void network::FallbackAPEnabled(bool value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    pFallbackAPEnabled = value;

    Notify(NotificationBits::ControlChanged);
}

bool network::FallbackAPEnabled() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() && pFallbackAPEnabled;
}

void network::FallbackAPRetention(uint16_t value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    pFallbackAPRetention = value;

    Notify(NotificationBits::ControlChanged);
}

uint16_t network::FallbackAPRetention() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() ? pFallbackAPRetention : 0;
}

void network::DHCP_Client(bool value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;

    pDHCP_Client = value;

    Notify(NotificationBits::ConfigurationChanged);
}

bool network::DHCP_Client() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() && pDHCP_Client;
}

void network::SSID(String value) {
    if (value.length() > 32) value.remove(32);

    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    pSSID = std::move(value);

    Notify(NotificationBits::ConfigurationChanged);
}

String network::SSID() const {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return String();

    return pConnectionMode == APMode::SoftAP ? pSoftAP_SSID : pSSID;
}

bool network::Passphrase(String value) {
    if (!IsValidStationPassword(value)) return false;

    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;

    pPassphrase = std::move(value);

    Notify(NotificationBits::ConfigurationChanged);

    return true;
}

String network::Passphrase() const {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return String();

    return pConnectionMode == APMode::SoftAP ? pSoftAP_Password : pPassphrase;
}

void network::SoftAP_SSID(String value) {
    if (value.length() > 32) value.remove(32);

    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    pSoftAP_SSID = std::move(value);

    Notify(NotificationBits::ConfigurationChanged);
}

String network::SoftAP_SSID() const {
    Lock lock(pMutex);

    return lock.IsLocked() ? pSoftAP_SSID : String();
}

bool network::SoftAP_Password(String value) {
    if (!IsValidSoftAPPassword(value)) return false;

    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;

    pSoftAP_Password = std::move(value);

    Notify(NotificationBits::ConfigurationChanged);

    return true;
}

String network::SoftAP_Password() const {
    Lock lock(pMutex);

    return lock.IsLocked() ? pSoftAP_Password : String();
}

void network::Hostname(String value) {
    if (value.length() > 63) value.remove(63);

    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    pHostname = std::move(value);

    Notify(NotificationBits::ConfigurationChanged);
}

String network::Hostname() const {
    Lock lock(pMutex);

    return lock.IsLocked() ? pHostname : String();
}

bool network::IP_Address(const String& value) noexcept {
    IPAddress parsed;
    if (!ParseIPAddress(value, parsed)) return false;

    IP_Address(parsed);

    return true;
}

void network::IP_Address(IPAddress value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    pIP_Address = value;

    Notify(NotificationBits::ConfigurationChanged);
}

IPAddress network::IP_Address() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() ? pCurrentIPAddress : IPAddress(0, 0, 0, 0);
}

bool network::Netmask(const String& value) noexcept {
    IPAddress parsed;
    if (!ParseIPAddress(value, parsed)) return false;

    Netmask(parsed);

    return true;
}

void network::Netmask(IPAddress value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    pNetmask = value;

    Notify(NotificationBits::ConfigurationChanged);
}

void network::Netmask(uint8_t cidr) noexcept {
    if (cidr > 32) return;
    const uint32_t mask = cidr == 0 ? 0 : 0xFFFFFFFFUL << (32 - cidr);
    Netmask(IPAddress(mask >> 24, mask >> 16, mask >> 8, mask));
}

IPAddress network::Netmask() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() ? pNetmask : IPAddress(0, 0, 0, 0);
}

bool network::Gateway(const String& value) noexcept {
    IPAddress parsed;
    if (!ParseIPAddress(value, parsed)) return false;

    Gateway(parsed);

    return true;
}

void network::Gateway(IPAddress value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;

    pGateway = value;

    Notify(NotificationBits::ConfigurationChanged);
}

IPAddress network::Gateway() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() ? pCurrentGateway : IPAddress(0, 0, 0, 0);
}

void network::DNS_Server(uint8_t index, IPAddress value) noexcept {
    if (index >= 2) return;

    Lock lock(pMutex);
    if (!lock.IsLocked()) return;

    pDNS_Server[index] = value;

    Notify(NotificationBits::ConfigurationChanged);
}

IPAddress network::DNS_Server(uint8_t index) const noexcept {
    if (index >= 2) return IPAddress(0, 0, 0, 0);

    Lock lock(pMutex);

    return lock.IsLocked() ? pDNS_Server[index] : IPAddress(0, 0, 0, 0);
}

int32_t network::RSSI() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() ? pRSSI : 0;
}

network::APMode network::ConnectionMode() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() ? pConnectionMode : APMode::Offline;
}

String network::MAC_Address() const {
    Lock lock(pMutex);

    return lock.IsLocked() ? pMACAddress : String();
}

IPAddress network::CurrentNetmask() const noexcept {
    Lock lock(pMutex);

    return lock.IsLocked() ? pCurrentNetmask : IPAddress(0, 0, 0, 0);
}

IPAddress network::CurrentDNS_Server(uint8_t index) const noexcept {
    if (index >= 2) return IPAddress(0, 0, 0, 0);

    Lock lock(pMutex);

    return lock.IsLocked() ? pCurrentDNS_Server[index] : IPAddress(0, 0, 0, 0);
}

void network::OnModeChanged(callback_t callback) {
    Lock lock(pMutex);
    if (lock.IsLocked()) pOnModeChanged = std::move(callback);
}

bool network::ParseIPAddress(const String& value, IPAddress& destination) noexcept {
    IPAddress parsed;
    if (!parsed.fromString(value)) return false;

    destination = parsed;

    return true;
}

bool network::IsPrintableASCII(const String& value) noexcept {
    for (size_t index = 0; index < value.length(); ++index) {
        const uint8_t character = static_cast<uint8_t>(value[index]);
        if (character < 0x20 || character > 0x7E) return false;
    }

    return true;
}

bool network::IsHex64(const String& value) noexcept {
    if (value.length() != 64) return false;

    for (size_t index = 0; index < value.length(); ++index) {
        const char character = value[index];
        const bool isHex =
            (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f') ||
            (character >= 'A' && character <= 'F');
        if (!isHex) return false;
    }

    return true;
}

bool network::IsValidStationPassword(const String& value) noexcept {
    if (value.isEmpty()) return true;
    if (IsHex64(value)) return true;

    return value.length() >= 8 && value.length() <= 63 && IsPrintableASCII(value);
}

bool network::IsValidSoftAPPassword(const String& value) noexcept {
    return value.isEmpty() || (value.length() >= 8 && value.length() <= 63 && IsPrintableASCII(value));
}

TickType_t network::SecondsToTicks(uint16_t seconds) noexcept {
    const uint64_t ticks = static_cast<uint64_t>(seconds) * configTICK_RATE_HZ;

    if (ticks == 0) return 1;
    if (ticks >= static_cast<uint64_t>(portMAX_DELAY)) return portMAX_DELAY - 1;

    return static_cast<TickType_t>(ticks);
}

bool network::TimeReached(TickType_t now, TickType_t target) noexcept {
    return static_cast<int32_t>(now - target) >= 0;
}

void network::TaskEntry(void* parameter) {
    static_cast<network*>(parameter)->Task();
}

void network::Task() {
    pLastModeCheck = xTaskGetTickCount();
    pPendingNotifications = NotificationBits::ConnectRequested;

    while (true) {
        uint32_t notifications = 0;
        xTaskNotifyWait(0, 0xFFFFFFFFUL, &notifications, pdMS_TO_TICKS(TASK_POLL_INTERVAL_MS));
        pPendingNotifications |= notifications;

        if ((pPendingNotifications & (NotificationBits::ConnectRequested | NotificationBits::ConfigurationChanged)) != 0) {
            pPendingNotifications &= ~(NotificationBits::ConnectRequested | NotificationBits::ConfigurationChanged);
            ConnectInternal();
        }

        if ((pPendingNotifications & NotificationBits::ControlChanged) != 0) {
            pPendingNotifications &= ~NotificationBits::ControlChanged;
        }

        if ((pPendingNotifications & NotificationBits::WiFiEventReceived) != 0) {
            pPendingNotifications &= ~NotificationBits::WiFiEventReceived;
            UpdateConnectionState();
        }

        Control();
    }
}

void network::Control() {
    const TickType_t now = xTaskGetTickCount();
    const Configuration configuration = ConfigurationSnapshot();
    const bool stationConnected = WiFi.status() == WL_CONNECTED;

    if (!configuration.fallbackAPEnabled && pSoftAPActive) StopSoftAP();

    if ((now - pLastModeCheck) >= pdMS_TO_TICKS(MODE_CHECK_INTERVAL_MS)) {
        pLastModeCheck = now;
        UpdateConnectionState();
    }

    if (stationConnected) {
        if (!pStationConnected) {
            pStationConnected = true;
            pStationConnectionPending = false;
            pStationConnectedAt = now;
            pCurrentReconnectInterval = configuration.reconnectInitialInterval;
            UpdateConnectionState();
        }

        if (pSoftAPActive &&
            (configuration.fallbackAPRetention == 0 ||
             (now - pStationConnectedAt) >= SecondsToTicks(configuration.fallbackAPRetention))) {
            StopSoftAP();
        }
        return;
    }

    if (pStationConnected) {
        pStationConnected = false;
        pStationConnectionPending = false;
        if (configuration.fallbackAPEnabled) (void)StartSoftAP(configuration);
        pNextReconnectAt = now;
        UpdateConnectionState();
    }

    if (pStationConnectionPending) {
        if ((now - pStationConnectionStartedAt) < SecondsToTicks(configuration.connectionTimeout)) return;

        WiFi.disconnect(false, false);
        pStationConnectionPending = false;
        if (configuration.fallbackAPEnabled) (void)StartSoftAP(configuration);
        ScheduleReconnect(configuration, now);
        UpdateConnectionState();
        return;
    }

    if (configuration.ssid.isEmpty()) {
        if (configuration.fallbackAPEnabled) (void)StartSoftAP(configuration);
        return;
    }

    if (!configuration.reconnectEnabled) {
        if (configuration.fallbackAPEnabled) (void)StartSoftAP(configuration);
        return;
    }

    if (TimeReached(now, pNextReconnectAt) && !BeginStationConnection(configuration)) {
        if (configuration.fallbackAPEnabled) (void)StartSoftAP(configuration);
        ScheduleReconnect(configuration, now);
    }
}

network::APMode network::ConnectInternal() {
    const Configuration configuration = ConfigurationSnapshot();
    WiFi.disconnect(true, false);
    pStationConnected = false;
    pStationConnectionPending = false;
    pSoftAPActive = false;
    pCurrentReconnectInterval = configuration.reconnectInitialInterval;
    pNextReconnectAt = xTaskGetTickCount();

    if (configuration.ssid.isEmpty()) {
        if (configuration.fallbackAPEnabled) (void)StartSoftAP(configuration);
        UpdateConnectionState();
        return ConnectionMode();
    }

    if (!BeginStationConnection(configuration)) {
        if (configuration.fallbackAPEnabled) (void)StartSoftAP(configuration);
        ScheduleReconnect(configuration, xTaskGetTickCount());
    }

    UpdateConnectionState();
    return ConnectionMode();
}

bool network::BeginStationConnection(const Configuration& configuration) {
    if (configuration.ssid.isEmpty()) return false;

    WiFi.mode(pSoftAPActive ? WIFI_AP_STA : WIFI_STA);
    WiFi.setHostname(configuration.hostname.c_str());

    const bool configured = configuration.dhcpClient
        ? WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0))
        : WiFi.config(configuration.ipAddress, configuration.gateway, configuration.netmask, configuration.dns[0], configuration.dns[1]);
    if (!configured) return false;

    WiFi.begin(
        configuration.ssid.c_str(),
        configuration.passphrase.isEmpty() ? nullptr : configuration.passphrase.c_str()
    );
    pStationConnectionStartedAt = xTaskGetTickCount();
    pStationConnectionPending = true;
    return true;
}

bool network::StartSoftAP(const Configuration& configuration) {
    if (pSoftAPActive && (WiFi.getMode() & WIFI_MODE_AP) != 0) return true;

    WiFi.mode(configuration.ssid.isEmpty() ? WIFI_AP : WIFI_AP_STA);

    const String ssid = configuration.softAPSSID.isEmpty() ? (configuration.hostname.isEmpty() ? String("DeviceIQ") : configuration.hostname) : configuration.softAPSSID;

    WiFi.softAPsetHostname(configuration.hostname.c_str());
    const bool started = WiFi.softAP(
        ssid.c_str(),
        configuration.softAPPassword.isEmpty() ? nullptr : configuration.softAPPassword.c_str()
    );

    pSoftAPActive = started;
    if (!started) WiFi.mode(configuration.ssid.isEmpty() ? WIFI_OFF : WIFI_STA);
    UpdateConnectionState();
    return started;
}

void network::StopSoftAP() {
    if (!pSoftAPActive) return;
    WiFi.softAPdisconnect(false);
    WiFi.mode(WIFI_STA);
    pSoftAPActive = false;
    UpdateConnectionState();
}

void network::ScheduleReconnect(const Configuration& configuration, TickType_t now) {
    const uint16_t initialInterval = configuration.reconnectInitialInterval == 0 ? 1 : configuration.reconnectInitialInterval;
    const uint16_t maximumInterval = configuration.reconnectMaximumInterval < initialInterval
        ? initialInterval
        : configuration.reconnectMaximumInterval;

    if (pCurrentReconnectInterval < initialInterval) pCurrentReconnectInterval = initialInterval;
    pNextReconnectAt = now + SecondsToTicks(pCurrentReconnectInterval);

    const uint32_t doubled = static_cast<uint32_t>(pCurrentReconnectInterval) * 2U;
    pCurrentReconnectInterval = static_cast<uint16_t>(doubled > maximumInterval ? maximumInterval : doubled);
}

void network::UpdateConnectionState() {
    APMode currentMode = APMode::Offline;
    const wifi_mode_t wifiMode = WiFi.getMode();

    if ((wifiMode & WIFI_MODE_STA) != 0 && WiFi.status() == WL_CONNECTED) {
        currentMode = APMode::WifiClient;
    } else if ((wifiMode & WIFI_MODE_AP) != 0) {
        currentMode = APMode::SoftAP;
    }

    const IPAddress currentIPAddress = currentMode == APMode::WifiClient ? WiFi.localIP() : (currentMode == APMode::SoftAP ? WiFi.softAPIP() : IPAddress(0, 0, 0, 0));
    const IPAddress currentNetmask = currentMode == APMode::WifiClient ? WiFi.subnetMask() : (currentMode == APMode::SoftAP ? WiFi.softAPSubnetMask() : IPAddress(0, 0, 0, 0));
    const IPAddress currentGateway = currentMode == APMode::WifiClient ? WiFi.gatewayIP() : IPAddress(0, 0, 0, 0);
    const IPAddress currentDNS0 = currentMode == APMode::WifiClient ? WiFi.dnsIP(0) : IPAddress(0, 0, 0, 0);
    const IPAddress currentDNS1 = currentMode == APMode::WifiClient ? WiFi.dnsIP(1) : IPAddress(0, 0, 0, 0);
    const int32_t currentRSSI = currentMode == APMode::WifiClient ? WiFi.RSSI() : 0;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macBuffer[18];
    snprintf(macBuffer, sizeof(macBuffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const String currentMACAddress(macBuffer);

    callback_t callback;
    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return;

        const bool publishStatus = !pStatusPublished || pConnectionMode != currentMode;
        pConnectionMode = currentMode;
        pCurrentIPAddress = currentIPAddress;
        pCurrentNetmask = currentNetmask;
        pCurrentGateway = currentGateway;
        pCurrentDNS_Server[0] = currentDNS0;
        pCurrentDNS_Server[1] = currentDNS1;
        pRSSI = currentRSSI;
        pMACAddress = currentMACAddress;
        pStatusPublished = true;

        if (publishStatus) callback = pOnModeChanged;
    }

    if (callback) callback();
}

void network::Notify(uint32_t bits) noexcept {
    const TaskHandle_t task = pTaskHandle;
    if (task != nullptr) xTaskNotify(task, bits, eSetBits);
}

network::Configuration network::ConfigurationSnapshot() const {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return Configuration{};

    return Configuration{
        pConnectionTimeout,
        pReconnectEnabled,
        pReconnectInitialInterval,
        pReconnectMaximumInterval,
        pFallbackAPEnabled,
        pFallbackAPRetention,
        pDHCP_Client,
        pSSID,
        pPassphrase,
        pSoftAP_SSID,
        pSoftAP_Password,
        pHostname,
        pIP_Address,
        pNetmask,
        pGateway,
        {pDNS_Server[0], pDNS_Server[1]}
    };
}
