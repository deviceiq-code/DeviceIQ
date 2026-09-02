#include "HTTPServer.h"

#include <ArduinoJson.h>
#include <esp_arduino_version.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp32-hal-cpu.h>
#include <cstdlib>

#include "core/DeviceControl.h"
#include "core/Globals.h"
#include "core/Users.h"
#include "core/Version.h"
#include "components/Blinds.h"
#include "components/Button.h"
#include "components/Relay.h"
#include "components/Thermometer.h"

namespace {
    const char* LoginFailureMessage(UserReturn result) {
        switch (result) {
            case UserReturn::InvalidCredentials: return "invalid credentials";
            case UserReturn::AuthenticationRateLimited: return "too many failed attempts, try again later";
            case UserReturn::SynchronizationError: return "authentication temporarily unavailable";
            default: return "authentication failed";
        }
    }

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

    String JsonEscaped(const String& value) {
        String result;
        result.reserve(value.length());
        for (size_t index = 0; index < value.length(); ++index) {
            const char character = value[index];
            if (character == '"' || character == '\\') result += '\\';
            result += character;
        }
        return result;
    }

    // ---- Generic settings-field extraction --------------------------------
    // Every Settings.* setter used below already self-validates or
    // self-corrects (see Settings.cpp), so these just need to reject the
    // wrong JSON type or a range the setter itself does not enforce
    // (idle timeouts, session limits, the log bitmasks).

    bool HasField(JsonObjectConst fields, const char* key) {
        return !fields[key].isNull();
    }

    template<typename T>
    bool JsonRanged(JsonObjectConst fields, const char* key, T minValue, T maxValue, T& out) {
        if (!fields[key].is<T>()) return false;
        const T raw = fields[key].as<T>();
        if (raw < minValue || raw > maxValue) return false;
        out = raw;
        return true;
    }

    bool JsonString(JsonObjectConst fields, const char* key, String& out) {
        if (!fields[key].is<const char*>()) return false;
        out = fields[key].as<const char*>();
        return true;
    }

    bool JsonBool(JsonObjectConst fields, const char* key, bool& out) {
        if (!fields[key].is<bool>()) return false;
        out = fields[key].as<bool>();
        return true;
    }

    // ---- Network ------------------------------------------------------------

    struct NetworkSnapshot {
        bool dhcpClient;
        String hostname;
        IPAddress ip;
        IPAddress gateway;
        IPAddress netmask;
        IPAddress dns[2];
        String ssid;
        String passphrase;
        uint16_t connectionTimeout;
        bool reconnectEnabled;
        uint16_t reconnectInitial;
        uint16_t reconnectMaximum;
        bool fallbackEnabled;
        String fallbackSSID;
        String fallbackPassword;
        uint16_t fallbackRetention;
    };

    NetworkSnapshot CaptureNetwork() {
        return {
            Settings.Network.DHCPClient(), Settings.Network.Hostname(), Settings.Network.IP_Address(),
            Settings.Network.Gateway(), Settings.Network.Netmask(),
            {Settings.Network.DNS(0), Settings.Network.DNS(1)},
            Settings.Network.SSID(), Settings.Network.Passphrase(), Settings.Network.ConnectionTimeout(),
            Settings.Network.ReconnectEnabled(), Settings.Network.ReconnectInitialInterval(), Settings.Network.ReconnectMaximumInterval(),
            Settings.Network.FallbackAPEnabled(), Settings.Network.FallbackAPSSID(), Settings.Network.FallbackAPPassword(), Settings.Network.FallbackAPRetention()
        };
    }

    void RestoreNetwork(const NetworkSnapshot& snapshot) {
        Settings.Network.DHCPClient(snapshot.dhcpClient);
        Settings.Network.Hostname(snapshot.hostname);
        Settings.Network.IP_Address(snapshot.ip.toString());
        Settings.Network.Gateway(snapshot.gateway.toString());
        Settings.Network.Netmask(snapshot.netmask.toString());
        Settings.Network.DNS(0, snapshot.dns[0].toString());
        Settings.Network.DNS(1, snapshot.dns[1].toString());
        Settings.Network.SSID(snapshot.ssid);
        Settings.Network.Passphrase(snapshot.passphrase);
        Settings.Network.ConnectionTimeout(snapshot.connectionTimeout);
        Settings.Network.ReconnectEnabled(snapshot.reconnectEnabled);
        Settings.Network.ReconnectInitialInterval(snapshot.reconnectInitial);
        Settings.Network.ReconnectMaximumInterval(snapshot.reconnectMaximum);
        Settings.Network.FallbackAPEnabled(snapshot.fallbackEnabled);
        Settings.Network.FallbackAPSSID(snapshot.fallbackSSID);
        Settings.Network.FallbackAPPassword(snapshot.fallbackPassword);
        Settings.Network.FallbackAPRetention(snapshot.fallbackRetention);
    }

    bool ApplyNetworkFields(JsonObjectConst fields, String& error) {
        const NetworkSnapshot snapshot = CaptureNetwork();
        bool boolValue = false;
        String stringValue;
        uint16_t uint16Value = 0;

        if (HasField(fields, "DHCP Client")) {
            if (!JsonBool(fields, "DHCP Client", boolValue)) { error = "DHCP Client must be a boolean."; RestoreNetwork(snapshot); return false; }
            Settings.Network.DHCPClient(boolValue);
        }
        if (HasField(fields, "Hostname")) {
            if (!JsonString(fields, "Hostname", stringValue)) { error = "Hostname must be a string."; RestoreNetwork(snapshot); return false; }
            Settings.Network.Hostname(stringValue);
        }
        if (HasField(fields, "IP Address")) {
            if (!JsonString(fields, "IP Address", stringValue)) { error = "IP Address must be a string."; RestoreNetwork(snapshot); return false; }
            Settings.Network.IP_Address(stringValue);
        }
        if (HasField(fields, "Gateway")) {
            if (!JsonString(fields, "Gateway", stringValue)) { error = "Gateway must be a string."; RestoreNetwork(snapshot); return false; }
            Settings.Network.Gateway(stringValue);
        }
        if (HasField(fields, "Netmask")) {
            if (!JsonString(fields, "Netmask", stringValue)) { error = "Netmask must be a string."; RestoreNetwork(snapshot); return false; }
            Settings.Network.Netmask(stringValue);
        }
        if (HasField(fields, "DNS 1")) {
            if (!JsonString(fields, "DNS 1", stringValue)) { error = "DNS 1 must be a string."; RestoreNetwork(snapshot); return false; }
            Settings.Network.DNS(0, stringValue);
        }
        if (HasField(fields, "DNS 2")) {
            if (!JsonString(fields, "DNS 2", stringValue)) { error = "DNS 2 must be a string."; RestoreNetwork(snapshot); return false; }
            Settings.Network.DNS(1, stringValue);
        }
        if (HasField(fields, "SSID")) {
            if (!JsonString(fields, "SSID", stringValue)) { error = "SSID must be a string."; RestoreNetwork(snapshot); return false; }
            Settings.Network.SSID(stringValue);
        }
        if (HasField(fields, "Passphrase")) {
            if (!JsonString(fields, "Passphrase", stringValue)) { error = "Passphrase must be a string."; RestoreNetwork(snapshot); return false; }
            Settings.Network.Passphrase(stringValue);
        }
        if (HasField(fields, "Connection Timeout")) {
            if (!JsonRanged<uint16_t>(fields, "Connection Timeout", 1, 65535, uint16Value)) { error = "Connection Timeout must be 1-65535."; RestoreNetwork(snapshot); return false; }
            Settings.Network.ConnectionTimeout(uint16Value);
        }
        if (HasField(fields, "Reconnect Enabled")) {
            if (!JsonBool(fields, "Reconnect Enabled", boolValue)) { error = "Reconnect Enabled must be a boolean."; RestoreNetwork(snapshot); return false; }
            Settings.Network.ReconnectEnabled(boolValue);
        }
        if (HasField(fields, "Reconnect Initial Interval")) {
            if (!JsonRanged<uint16_t>(fields, "Reconnect Initial Interval", 1, 65535, uint16Value)) { error = "Reconnect Initial Interval must be 1-65535."; RestoreNetwork(snapshot); return false; }
            Settings.Network.ReconnectInitialInterval(uint16Value);
        }
        if (HasField(fields, "Reconnect Maximum Interval")) {
            if (!JsonRanged<uint16_t>(fields, "Reconnect Maximum Interval", 1, 65535, uint16Value)) { error = "Reconnect Maximum Interval must be 1-65535."; RestoreNetwork(snapshot); return false; }
            Settings.Network.ReconnectMaximumInterval(uint16Value);
        }
        if (HasField(fields, "Fallback AP Enabled")) {
            if (!JsonBool(fields, "Fallback AP Enabled", boolValue)) { error = "Fallback AP Enabled must be a boolean."; RestoreNetwork(snapshot); return false; }
            Settings.Network.FallbackAPEnabled(boolValue);
        }
        if (HasField(fields, "Fallback AP SSID")) {
            if (!JsonString(fields, "Fallback AP SSID", stringValue)) { error = "Fallback AP SSID must be a string."; RestoreNetwork(snapshot); return false; }
            Settings.Network.FallbackAPSSID(stringValue);
        }
        if (HasField(fields, "Fallback AP Password")) {
            if (!JsonString(fields, "Fallback AP Password", stringValue)) { error = "Fallback AP Password must be a string."; RestoreNetwork(snapshot); return false; }
            Settings.Network.FallbackAPPassword(stringValue);
        }
        if (HasField(fields, "Fallback AP Retention")) {
            if (!JsonRanged<uint16_t>(fields, "Fallback AP Retention", 0, 65535, uint16Value)) { error = "Fallback AP Retention must be 0-65535."; RestoreNetwork(snapshot); return false; }
            Settings.Network.FallbackAPRetention(uint16Value);
        }

        return true;
    }

    void GetNetworkJson(JsonObject out) {
        out["DHCP Client"] = Settings.Network.DHCPClient();
        out["Hostname"] = Settings.Network.Hostname();
        out["IP Address"] = Settings.Network.IP_Address().toString();
        out["Gateway"] = Settings.Network.Gateway().toString();
        out["Netmask"] = Settings.Network.Netmask().toString();
        out["Current IP"] = Network.IP_Address().toString();
        out["Current Gateway"] = Network.Gateway().toString();
        out["Current Netmask"] = Network.CurrentNetmask().toString();
        out["DNS 1"] = Settings.Network.DNS(0).toString();
        out["DNS 2"] = Settings.Network.DNS(1).toString();
        out["SSID"] = Settings.Network.SSID();
        out["Passphrase"] = Settings.Network.Passphrase();
        out["Connection Timeout"] = Settings.Network.ConnectionTimeout();
        out["Reconnect Enabled"] = Settings.Network.ReconnectEnabled();
        out["Reconnect Initial Interval"] = Settings.Network.ReconnectInitialInterval();
        out["Reconnect Maximum Interval"] = Settings.Network.ReconnectMaximumInterval();
        out["Fallback AP Enabled"] = Settings.Network.FallbackAPEnabled();
        out["Fallback AP SSID"] = Settings.Network.FallbackAPSSID();
        out["Fallback AP Password"] = Settings.Network.FallbackAPPassword();
        out["Fallback AP Retention"] = Settings.Network.FallbackAPRetention();
    }

    // ---- Telnet ---------------------------------------------------------

    struct TelnetSnapshot {
        bool enabled;
        uint16_t port;
        uint32_t idleTimeoutMs;
        uint8_t maxSessions;
    };

    TelnetSnapshot CaptureTelnet() {
        return {Settings.TelnetServer.Enabled(), Settings.TelnetServer.Port(), Settings.TelnetServer.IdleTimeoutMs(), Settings.TelnetServer.MaxSessions()};
    }

    void RestoreTelnet(const TelnetSnapshot& snapshot) {
        Settings.TelnetServer.Enabled(snapshot.enabled);
        Settings.TelnetServer.Port(snapshot.port);
        Settings.TelnetServer.IdleTimeoutMs(snapshot.idleTimeoutMs);
        Settings.TelnetServer.MaxSessions(snapshot.maxSessions);
    }

    bool ApplyTelnetFields(JsonObjectConst fields, String& error) {
        const TelnetSnapshot snapshot = CaptureTelnet();
        bool boolValue = false;
        uint16_t uint16Value = 0;
        uint32_t uint32Value = 0;

        if (HasField(fields, "Enabled")) {
            if (!JsonBool(fields, "Enabled", boolValue)) { error = "Enabled must be a boolean."; RestoreTelnet(snapshot); return false; }
            Settings.TelnetServer.Enabled(boolValue);
        }
        if (HasField(fields, "Port")) {
            if (!JsonRanged<uint16_t>(fields, "Port", 1, 65535, uint16Value)) { error = "Port must be 1-65535."; RestoreTelnet(snapshot); return false; }
            Settings.TelnetServer.Port(uint16Value);
        }
        if (HasField(fields, "Idle Timeout")) {
            if (!JsonRanged<uint32_t>(fields, "Idle Timeout", 0, 86400000, uint32Value)) { error = "Idle Timeout must be 0-86400000 ms."; RestoreTelnet(snapshot); return false; }
            Settings.TelnetServer.IdleTimeoutMs(uint32Value);
        }
        if (HasField(fields, "Max Sessions")) {
            if (!JsonRanged<uint16_t>(fields, "Max Sessions", telnetserver::MIN_SESSIONS, telnetserver::MAX_SESSIONS, uint16Value)) {
                error = "Max Sessions must be " + String(telnetserver::MIN_SESSIONS) + "-" + String(telnetserver::MAX_SESSIONS) + ".";
                RestoreTelnet(snapshot);
                return false;
            }
            Settings.TelnetServer.MaxSessions(static_cast<uint8_t>(uint16Value));
        }

        return true;
    }

    void GetTelnetJson(JsonObject out) {
        out["Enabled"] = Settings.TelnetServer.Enabled();
        out["Port"] = Settings.TelnetServer.Port();
        out["Idle Timeout"] = Settings.TelnetServer.IdleTimeoutMs();
        out["Max Sessions"] = Settings.TelnetServer.MaxSessions();
    }

    // ---- NTP --------------------------------------------------------------

    struct NTPSnapshot {
        bool enabled;
        String server;
        int8_t timeZone;
    };

    NTPSnapshot CaptureNTP() {
        return {Settings.General.NTPUpdate(), Settings.General.NTPServer(), Settings.General.TimeZone()};
    }

    void RestoreNTP(const NTPSnapshot& snapshot) {
        Settings.General.NTPUpdate(snapshot.enabled);
        Settings.General.NTPServer(snapshot.server);
        Settings.General.TimeZone(snapshot.timeZone);
    }

    bool ApplyNTPFields(JsonObjectConst fields, String& error) {
        const NTPSnapshot snapshot = CaptureNTP();
        bool boolValue = false;
        String stringValue;
        int intValue = 0;

        if (HasField(fields, "Enabled")) {
            if (!JsonBool(fields, "Enabled", boolValue)) { error = "Enabled must be a boolean."; RestoreNTP(snapshot); return false; }
            Settings.General.NTPUpdate(boolValue);
        }
        if (HasField(fields, "Server")) {
            if (!JsonString(fields, "Server", stringValue)) { error = "Server must be a string."; RestoreNTP(snapshot); return false; }
            Settings.General.NTPServer(stringValue);
        }
        if (HasField(fields, "Time Zone")) {
            if (!JsonRanged<int>(fields, "Time Zone", -12, 14, intValue)) { error = "Time Zone must be -12 to 14."; RestoreNTP(snapshot); return false; }
            Settings.General.TimeZone(intValue);
        }

        return true;
    }

    void GetNTPJson(JsonObject out) {
        out["Enabled"] = Settings.General.NTPUpdate();
        out["Server"] = Settings.General.NTPServer();
        out["Time Zone"] = Settings.General.TimeZone();
    }

    // ---- MQTT ---------------------------------------------------------------

    struct MQTTSnapshot {
        bool enabled;
        String broker;
        uint16_t port;
        String user;
        String password;
        bool discoveryEnabled;
        String discoveryPrefix;
    };

    MQTTSnapshot CaptureMQTT() {
        return {
            Settings.MQTT.Enabled(), Settings.MQTT.Broker(), Settings.MQTT.Port(), Settings.MQTT.User(),
            Settings.MQTT.Password(), Settings.MQTT.DiscoveryEnabled(), Settings.MQTT.DiscoveryPrefix()
        };
    }

    void RestoreMQTT(const MQTTSnapshot& snapshot) {
        Settings.MQTT.Enabled(snapshot.enabled);
        Settings.MQTT.Broker(snapshot.broker);
        Settings.MQTT.Port(snapshot.port);
        Settings.MQTT.User(snapshot.user);
        Settings.MQTT.Password(snapshot.password);
        Settings.MQTT.DiscoveryEnabled(snapshot.discoveryEnabled);
        Settings.MQTT.DiscoveryPrefix(snapshot.discoveryPrefix);
    }

    bool ApplyMQTTFields(JsonObjectConst fields, String& error) {
        const MQTTSnapshot snapshot = CaptureMQTT();
        bool boolValue = false;
        String stringValue;
        uint16_t uint16Value = 0;

        if (HasField(fields, "Enabled")) {
            if (!JsonBool(fields, "Enabled", boolValue)) { error = "Enabled must be a boolean."; RestoreMQTT(snapshot); return false; }
            Settings.MQTT.Enabled(boolValue);
        }
        if (HasField(fields, "Broker")) {
            if (!JsonString(fields, "Broker", stringValue)) { error = "Broker must be a string."; RestoreMQTT(snapshot); return false; }
            Settings.MQTT.Broker(stringValue);
        }
        if (HasField(fields, "Port")) {
            if (!JsonRanged<uint16_t>(fields, "Port", 1, 65535, uint16Value)) { error = "Port must be 1-65535."; RestoreMQTT(snapshot); return false; }
            Settings.MQTT.Port(uint16Value);
        }
        if (HasField(fields, "User")) {
            if (!JsonString(fields, "User", stringValue)) { error = "User must be a string."; RestoreMQTT(snapshot); return false; }
            Settings.MQTT.User(stringValue);
        }
        if (HasField(fields, "Password")) {
            if (!JsonString(fields, "Password", stringValue)) { error = "Password must be a string."; RestoreMQTT(snapshot); return false; }
            Settings.MQTT.Password(stringValue);
        }
        if (HasField(fields, "Discovery Enabled")) {
            if (!JsonBool(fields, "Discovery Enabled", boolValue)) { error = "Discovery Enabled must be a boolean."; RestoreMQTT(snapshot); return false; }
            Settings.MQTT.DiscoveryEnabled(boolValue);
        }
        if (HasField(fields, "Discovery Prefix")) {
            if (!JsonString(fields, "Discovery Prefix", stringValue)) { error = "Discovery Prefix must be a string."; RestoreMQTT(snapshot); return false; }
            Settings.MQTT.DiscoveryPrefix(stringValue);
        }

        return true;
    }

    void GetMQTTJson(JsonObject out) {
        out["Enabled"] = Settings.MQTT.Enabled();
        out["Broker"] = Settings.MQTT.Broker();
        out["Port"] = Settings.MQTT.Port();
        out["User"] = Settings.MQTT.User();
        out["Password"] = Settings.MQTT.Password();
        out["Discovery Enabled"] = Settings.MQTT.DiscoveryEnabled();
        out["Discovery Prefix"] = Settings.MQTT.DiscoveryPrefix();
    }

    // ---- Log ------------------------------------------------------------
    // Endpoint/Level are raw bitmasks with no validation at the Settings
    // layer at all (unlike every other field here) - the bounds below are
    // the only thing keeping an out-of-range mask out of the config file.

    struct LogSnapshot {
        uint8_t endpoint;
        uint8_t level;
        String syslogServer;
        uint16_t syslogPort;
    };

    LogSnapshot CaptureLog() {
        return {Settings.Log.Endpoint(), Settings.Log.LogLevel(), Settings.Log.SyslogServerHost(), Settings.Log.SyslogServerPort()};
    }

    void RestoreLog(const LogSnapshot& snapshot) {
        Settings.Log.Endpoint(snapshot.endpoint);
        Settings.Log.LogLevel(snapshot.level);
        Settings.Log.SyslogServerHost(snapshot.syslogServer);
        Settings.Log.SyslogServerPort(snapshot.syslogPort);
    }

    bool ApplyLogFields(JsonObjectConst fields, String& error) {
        const LogSnapshot snapshot = CaptureLog();
        String stringValue;
        uint16_t uint16Value = 0;

        if (HasField(fields, "Endpoint")) {
            if (!JsonRanged<uint16_t>(fields, "Endpoint", 0, 7, uint16Value)) { error = "Endpoint must be a bitmask 0-7 (1=serial, 2=syslog, 4=file)."; RestoreLog(snapshot); return false; }
            Settings.Log.Endpoint(static_cast<uint8_t>(uint16Value));
        }
        if (HasField(fields, "Level")) {
            if (!JsonRanged<uint16_t>(fields, "Level", 0, 255, uint16Value) || (uint16Value > 15 && uint16Value != 255)) {
                error = "Level must be a bitmask 0-15 (1=error, 2=warning, 4=information, 8=debug), or 255 for all.";
                RestoreLog(snapshot);
                return false;
            }
            Settings.Log.LogLevel(static_cast<uint8_t>(uint16Value));
        }
        if (HasField(fields, "Syslog Server")) {
            if (!JsonString(fields, "Syslog Server", stringValue)) { error = "Syslog Server must be a string."; RestoreLog(snapshot); return false; }
            Settings.Log.SyslogServerHost(stringValue);
        }
        if (HasField(fields, "Syslog Port")) {
            if (!JsonRanged<uint16_t>(fields, "Syslog Port", 1, 65535, uint16Value)) { error = "Syslog Port must be 1-65535."; RestoreLog(snapshot); return false; }
            Settings.Log.SyslogServerPort(uint16Value);
        }

        return true;
    }

    void GetLogJson(JsonObject out) {
        out["Endpoint"] = Settings.Log.Endpoint();
        out["Level"] = Settings.Log.LogLevel();
        out["Syslog Server"] = Settings.Log.SyslogServerHost();
        out["Syslog Port"] = Settings.Log.SyslogServerPort();
    }

    // ---- Web --------------------------------------------------------------

    struct WebSnapshot {
        bool enabled;
        uint16_t port;
        uint32_t idleTimeoutMs;
        uint8_t maxSessions;
    };

    WebSnapshot CaptureWeb() {
        return {Settings.WebServer.Enabled(), Settings.WebServer.Port(), Settings.WebServer.IdleTimeoutMs(), Settings.WebServer.MaxSessions()};
    }

    void RestoreWeb(const WebSnapshot& snapshot) {
        Settings.WebServer.Enabled(snapshot.enabled);
        Settings.WebServer.Port(snapshot.port);
        Settings.WebServer.IdleTimeoutMs(snapshot.idleTimeoutMs);
        Settings.WebServer.MaxSessions(snapshot.maxSessions);
    }

    bool ApplyWebFields(JsonObjectConst fields, String& error) {
        const WebSnapshot snapshot = CaptureWeb();
        bool boolValue = false;
        uint16_t uint16Value = 0;
        uint32_t uint32Value = 0;

        if (HasField(fields, "Enabled")) {
            if (!JsonBool(fields, "Enabled", boolValue)) { error = "Enabled must be a boolean."; RestoreWeb(snapshot); return false; }
            Settings.WebServer.Enabled(boolValue);
        }
        if (HasField(fields, "Port")) {
            if (!JsonRanged<uint16_t>(fields, "Port", 1, 65535, uint16Value)) { error = "Port must be 1-65535."; RestoreWeb(snapshot); return false; }
            Settings.WebServer.Port(uint16Value);
        }
        if (HasField(fields, "Idle Timeout")) {
            if (!JsonRanged<uint32_t>(fields, "Idle Timeout", 0, 86400000, uint32Value)) { error = "Idle Timeout must be 0-86400000 ms."; RestoreWeb(snapshot); return false; }
            Settings.WebServer.IdleTimeoutMs(uint32Value);
        }
        if (HasField(fields, "Max Sessions")) {
            if (!JsonRanged<uint16_t>(fields, "Max Sessions", httpserver::MIN_SESSIONS, httpserver::MAX_SESSIONS, uint16Value)) {
                error = "Max Sessions must be " + String(httpserver::MIN_SESSIONS) + "-" + String(httpserver::MAX_SESSIONS) + ".";
                RestoreWeb(snapshot);
                return false;
            }
            Settings.WebServer.MaxSessions(static_cast<uint8_t>(uint16Value));
        }

        return true;
    }

    void GetWebJson(JsonObject out) {
        out["Enabled"] = Settings.WebServer.Enabled();
        out["Port"] = Settings.WebServer.Port();
        out["Idle Timeout"] = Settings.WebServer.IdleTimeoutMs();
        out["Max Sessions"] = Settings.WebServer.MaxSessions();
    }

    // ---- About ----------------------------------------------------------
    // Mirrors the Telnet CLI's ver/hwinfo (any authenticated session) and
    // mem/fs (admin only) commands, as JSON instead of formatted text.

    const char* FlashModeName(FlashMode_t mode) {
        switch (mode) {
            case FM_QIO: return "QIO";
            case FM_QOUT: return "QOUT";
            case FM_DIO: return "DIO";
            case FM_DOUT: return "DOUT";
            case FM_FAST_READ: return "Fast Read";
            case FM_SLOW_READ: return "Slow Read";
            default: return "Unknown";
        }
    }

    const char* ResetReasonName(esp_reset_reason_t reason) {
        switch (reason) {
            case ESP_RST_POWERON: return "Power on";
            case ESP_RST_EXT: return "External pin";
            case ESP_RST_SW: return "Software restart";
            case ESP_RST_PANIC: return "Exception/panic";
            case ESP_RST_INT_WDT: return "Interrupt watchdog";
            case ESP_RST_TASK_WDT: return "Task watchdog";
            case ESP_RST_WDT: return "Other watchdog";
            case ESP_RST_DEEPSLEEP: return "Deep-sleep wakeup";
            case ESP_RST_BROWNOUT: return "Brownout";
            case ESP_RST_SDIO: return "SDIO";
            default: return "Unknown";
        }
    }

    String AboutMacAddress(esp_mac_type_t type) {
        uint8_t mac[6]{};
        if (esp_read_mac(mac, type) != ESP_OK) return "Unavailable";
        char text[18];
        snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return String(text);
    }

    void AppendMemoryJson(JsonObject out, uint32_t total, uint32_t available, uint32_t minimum, uint32_t largest) {
        out["total"] = total;
        out["available"] = available;
        out["minFree"] = minimum;
        out["largestBlock"] = largest;
    }

    void GetAboutJson(JsonObject out, bool isAdmin) {
        JsonObject version = out["version"].to<JsonObject>();
        version["product"] = Version::ProductName;
        version["family"] = Version::ProductFamily;
        version["serial"] = Version::SerialNumber();
        version["hardware"] = Version::Hardware::Info();
        version["software"] = Version::Software::Info();

        JsonObject hardware = out["hardware"].to<JsonObject>();
        hardware["model"] = ESP.getChipModel();
        hardware["revision"] = ESP.getChipRevision();
        hardware["cores"] = ESP.getChipCores();
        hardware["cpuFrequencyMHz"] = ESP.getCpuFreqMHz();
        hardware["crystalFrequencyMHz"] = getXtalFrequencyMhz();
        hardware["apbFrequencyMHz"] = getApbFrequency() / 1000000UL;
        hardware["temperatureC"] = temperatureRead();
        hardware["wifiMac"] = AboutMacAddress(ESP_MAC_WIFI_STA);
        hardware["bluetoothMac"] = AboutMacAddress(ESP_MAC_BT);

        JsonObject flash = out["flash"].to<JsonObject>();
        flash["sizeBytes"] = ESP.getFlashChipSize();
        flash["speedMHz"] = ESP.getFlashChipSpeed() / 1000000UL;
        flash["mode"] = FlashModeName(ESP.getFlashChipMode());

        JsonObject firmware = out["firmware"].to<JsonObject>();
        firmware["sketchSizeBytes"] = ESP.getSketchSize();
        firmware["otaFreeBytes"] = ESP.getFreeSketchSpace();
        firmware["sketchMD5"] = ESP.getSketchMD5();

        JsonObject runtime = out["runtime"].to<JsonObject>();
        runtime["espIdf"] = ESP.getSdkVersion();
        runtime["arduinoEsp32"] = String(ESP_ARDUINO_VERSION_MAJOR) + "." + String(ESP_ARDUINO_VERSION_MINOR) + "." + String(ESP_ARDUINO_VERSION_PATCH);
        runtime["resetReason"] = ResetReasonName(esp_reset_reason());
        runtime["uptimeSeconds"] = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
        runtime["freeRtosTasks"] = uxTaskGetNumberOfTasks();

        out["admin"] = isAdmin;
        if (!isAdmin) return;

        const uint32_t heapTotal = ESP.getHeapSize();
        const uint32_t heapAvailable = ESP.getFreeHeap();
        const uint32_t psramTotal = ESP.getPsramSize();
        const uint32_t psramAvailable = ESP.getFreePsram();
        const uint32_t largestBlock = ESP.getMaxAllocHeap() >= ESP.getMaxAllocPsram() ? ESP.getMaxAllocHeap() : ESP.getMaxAllocPsram();
        const uint32_t fragmentation = heapAvailable == 0 || ESP.getMaxAllocHeap() >= heapAvailable
            ? 0
            : 100U - static_cast<uint32_t>((static_cast<uint64_t>(ESP.getMaxAllocHeap()) * 100ULL) / heapAvailable);

        JsonObject memory = out["memory"].to<JsonObject>();
        AppendMemoryJson(memory["heap"].to<JsonObject>(), heapTotal, heapAvailable, ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
        AppendMemoryJson(memory["psram"].to<JsonObject>(), psramTotal, psramAvailable, ESP.getMinFreePsram(), ESP.getMaxAllocPsram());
        AppendMemoryJson(memory["total"].to<JsonObject>(), heapTotal + psramTotal, heapAvailable + psramAvailable, ESP.getMinFreeHeap() + ESP.getMinFreePsram(), largestBlock);
        memory["fragmentationPercent"] = fragmentation;
        memory["stackMinimumBytes"] = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);

        JsonObject fileSystemInfo = out["filesystem"].to<JsonObject>();
        filesystem::Statistics statistics;
        if (!FileSystem.GetStatistics(statistics)) {
            fileSystemInfo["mounted"] = false;
        } else {
            const size_t available = statistics.totalBytes >= statistics.usedBytes ? statistics.totalBytes - statistics.usedBytes : 0;
            const uint32_t usage = statistics.totalBytes == 0
                ? 0
                : static_cast<uint32_t>((static_cast<uint64_t>(statistics.usedBytes) * 100ULL) / statistics.totalBytes);

            fileSystemInfo["mounted"] = true;
            fileSystemInfo["totalBytes"] = statistics.totalBytes;
            fileSystemInfo["usedBytes"] = statistics.usedBytes;
            fileSystemInfo["availableBytes"] = available;
            fileSystemInfo["usagePercent"] = usage;
            fileSystemInfo["files"] = statistics.files;
            fileSystemInfo["directories"] = statistics.directories;
            fileSystemInfo["fileDataBytes"] = statistics.fileBytes;
            fileSystemInfo["largestFileBytes"] = statistics.largestFileBytes;
            fileSystemInfo["configSizeBytes"] = FileSystem.Size(Defaults.ConfigFileName);
            fileSystemInfo["logSizeBytes"] = FileSystem.Size(Defaults.LogFileName);
        }
    }
}

httpserver::httpserver() noexcept : pMutex(xSemaphoreCreateMutexStatic(&pMutexStorage)) {
    configASSERT(pMutex != nullptr);
    pPort = Defaults.WebServer.Port;
    pEnabled = Defaults.WebServer.Enabled;
    pIdleTimeoutMs = Defaults.WebServer.IdleTimeoutMs;
    pMaxSessions = Defaults.WebServer.MaxSessions;
}

httpserver::~httpserver() {
    Stop();
}

bool httpserver::Start() {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return false;
    if (!pEnabled || pTaskHandle != nullptr) return true;

    return xTaskCreate(TaskEntry, "HTTPServer", TASK_STACK_SIZE, this, TASK_PRIORITY, &pTaskHandle) == pdPASS;
}

void httpserver::Stop() {
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
        {
            Lock lock(pMutex);
            if (!lock.IsLocked() || pTaskHandle == nullptr) return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void httpserver::Enabled(bool value) noexcept {
    Lock lock(pMutex);
    if (lock.IsLocked() && pTaskHandle == nullptr) pEnabled = value;
}

bool httpserver::Enabled() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() && pEnabled;
}

void httpserver::Port(uint16_t value) noexcept {
    Lock lock(pMutex);
    if (lock.IsLocked() && pTaskHandle == nullptr) pPort = value == 0 ? Defaults.WebServer.Port : value;
}

uint16_t httpserver::Port() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() ? pPort : 0;
}

void httpserver::IdleTimeout(uint32_t valueMs) noexcept {
    Lock lock(pMutex);
    if (lock.IsLocked()) pIdleTimeoutMs = valueMs;
}

uint32_t httpserver::IdleTimeout() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() ? pIdleTimeoutMs : 0;
}

void httpserver::MaxSessions(size_t value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    if (value < MIN_SESSIONS) value = MIN_SESSIONS;
    if (value > MAX_SESSIONS) value = MAX_SESSIONS;
    pMaxSessions = value;
}

size_t httpserver::MaxSessions() const noexcept {
    Lock lock(pMutex);
    return lock.IsLocked() ? pMaxSessions : 0;
}

void httpserver::TaskEntry(void* parameter) {
    static_cast<httpserver*>(parameter)->Task();
}

void httpserver::Task() {
    uint16_t port = 80;
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

void httpserver::RegisterRoutes(WebServer& server) {
    // Only "Authorization" is collected by default; the session cookie needs
    // to be requested explicitly, before begin().
    static const char* HeaderKeys[] = {"Cookie"};
    server.collectHeaders(HeaderKeys, 1);

    server.on("/", HTTP_GET, [this, &server]() { HandleIndex(server); });
    server.on("/dashboard.html", HTTP_GET, [this, &server]() { HandleDashboard(server); });
    server.on("/setup.html", HTTP_GET, [this, &server]() { HandleSetup(server); });
    server.on("/about.html", HTTP_GET, [this, &server]() { HandleAbout(server); });
    server.on("/api/about", HTTP_GET, [this, &server]() { HandleAboutGet(server); });
    server.on("/style.css", HTTP_GET, [this, &server]() { HandleStyle(server); });
    server.on("/notifications.js", HTTP_GET, [this, &server]() { HandleScript(server); });
    server.on("/api/login", HTTP_POST, [this, &server]() { HandleLoginPost(server); });
    server.on("/api/logout", HTTP_POST, [this, &server]() { HandleLogoutPost(server); });
    server.on("/api/session", HTTP_GET, [this, &server]() { HandleSessionGet(server); });
    server.on("/api/components", HTTP_GET, [this, &server]() { HandleComponentsGet(server); });
    server.on("/api/components/set", HTTP_POST, [this, &server]() { HandleComponentsSetPost(server); });
    server.on("/api/settings", HTTP_GET, [this, &server]() { HandleSettingsGet(server); });
    server.on("/api/settings", HTTP_POST, [this, &server]() { HandleSettingsPost(server); });
    server.on("/api/reboot", HTTP_POST, [this, &server]() { HandleRebootPost(server); });
    server.on("/api/config/export", HTTP_GET, [this, &server]() { HandleConfigExportGet(server); });
    server.on(
        "/api/config/import", HTTP_POST,
        [this, &server]() { HandleConfigImportPost(server); },
        [this, &server]() { HandleConfigImportUpload(server); }
    );
    server.onNotFound([this, &server]() { HandleNotFound(server); });
}

void httpserver::HandleIndex(WebServer& server) {
    ServeFile(server, "/index.html", "text/html");
}

void httpserver::HandleDashboard(WebServer& server) {
    ServeProtectedFile(server, "/dashboard.html", "text/html", false);
}

void httpserver::HandleSetup(WebServer& server) {
    ServeProtectedFile(server, "/setup.html", "text/html", true);
}

void httpserver::HandleAbout(WebServer& server) {
    ServeProtectedFile(server, "/about.html", "text/html", false);
}

void httpserver::HandleAboutGet(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }

    JsonDocument document;
    GetAboutJson(document.to<JsonObject>(), session->admin);

    String payload;
    serializeJson(document, payload);
    server.send(200, "application/json", payload);
}

void httpserver::HandleStyle(WebServer& server) {
    ServeFile(server, "/style.css", "text/css");
}

void httpserver::HandleScript(WebServer& server) {
    ServeFile(server, "/notifications.js", "application/javascript");
}

void httpserver::HandleLoginPost(WebServer& server) {
    const String username = server.arg("username");
    const String password = server.arg("password");
    const IPAddress remoteIP = server.client().remoteIP();

    if (username.isEmpty() || password.isEmpty()) {
        server.send(400, "application/json", "{\"error\":\"missing username or password\"}");
        return;
    }

    const UserReturn authentication = Settings.Users.Authenticate(username, password, remoteIP);
    if (authentication != UserReturn::AuthenticationSuccess) {
        Logger.Log(
            "Web Server: Logon failed for " + username + "@" + remoteIP.toString() + " - " + String(LoginFailureMessage(authentication)),
            logger::LogLevels::Warning
        );
        server.send(401, "application/json", "{\"error\":\"" + JsonEscaped(LoginFailureMessage(authentication)) + "\"}");
        return;
    }

    UserInfo info;
    if (Settings.Users.Find(username, &info) != UserReturn::NoError) {
        Logger.Log("Web Server: Unable to load user record after successful authentication", logger::LogLevels::Error);
        server.send(500, "application/json", "{\"error\":\"internal error\"}");
        return;
    }

    Session* session = CreateSession(info.username, info.admin);
    if (session == nullptr) {
        server.send(503, "application/json", "{\"error\":\"too many active sessions\"}");
        return;
    }

    Logger.Log("Web Server: Logon successful for " + info.username + "@" + remoteIP.toString(), logger::LogLevels::Information);
    server.sendHeader("Set-Cookie", "session=" + String(session->token) + "; Path=/; HttpOnly; SameSite=Strict");
    server.send(200, "application/json", "{\"success\":true,\"username\":\"" + JsonEscaped(info.username) + "\",\"admin\":" + (info.admin ? "true" : "false") + "}");
}

void httpserver::HandleLogoutPost(WebServer& server) {
    const String token = SessionTokenFromCookie(server);
    if (!token.isEmpty()) DestroySession(token);

    server.sendHeader("Set-Cookie", "session=; Path=/; HttpOnly; Max-Age=0");
    server.send(200, "application/json", "{\"success\":true}");
}

void httpserver::HandleSessionGet(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"authenticated\":false}");
        return;
    }

    server.send(
        200, "application/json",
        "{\"authenticated\":true,\"username\":\"" + JsonEscaped(session->username) + "\",\"admin\":" + (session->admin ? "true" : "false") +
            ",\"productFamily\":\"" + JsonEscaped(Version::ProductFamily) + "\",\"productName\":\"" + JsonEscaped(Version::ProductName) +
            "\",\"softwareVersion\":\"" + JsonEscaped(Version::Software::Info()) + "\"}"
    );
}

void httpserver::HandleComponentsGet(WebServer& server) {
    if (AuthenticatedSession(server) == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }

    JsonDocument document;
    JsonArray components = document["components"].to<JsonArray>();

    // Private members (a Blinds group's underlying relays/buttons) are
    // never public, so they're skipped here and never listed on their own -
    // the owning Blinds entry already reports position and motion for the
    // whole group.
    for (size_t index = 0; index < ComponentController.Count(); ++index) {
        const component* item = ComponentController.At(index);
        if (item == nullptr || !item->IsPublic()) continue;

        JsonObject entry = components.add<JsonObject>();
        entry["id"] = item->ID();
        entry["name"] = item->Name();
        entry["class"] = component::ClassName(item->Class());
        entry["enabled"] = item->Enabled();

        if (item->Class() == component::Classes::Relay) {
            entry["state"] = static_cast<const relay&>(*item).State();
        } else if (item->Class() == component::Classes::Button) {
            entry["state"] = static_cast<const button&>(*item).State();
        } else if (item->Class() == component::Classes::Blinds) {
            const blinds& value = static_cast<const blinds&>(*item);
            entry["state"] = blinds::MotionName(value.State());
            entry["position"] = value.Position();
            entry["targetPosition"] = value.TargetPosition();
        } else if (item->Class() == component::Classes::Thermometer) {
            const thermometer& value = static_cast<const thermometer&>(*item);
            entry["available"] = value.Available();
            entry["hasHumidity"] = value.HasHumidity();
            if (value.Available()) {
                entry["temperature"] = value.Temperature();
                if (value.HasHumidity()) entry["humidity"] = value.Humidity();
            }
        }
    }

    String payload;
    serializeJson(document, payload);
    server.send(200, "application/json", payload);
}

void httpserver::HandleComponentsSetPost(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }

    // Mirrors the Telnet CLI's "comp set", which also requires an admin
    // session for any component mutation.
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    char* end = nullptr;
    const long id = std::strtol(server.arg("id").c_str(), &end, 10);
    const String property = server.arg("property");
    const String value = server.arg("value");
    const IPAddress remoteIP = server.client().remoteIP();

    if (end == server.arg("id").c_str() || id < 1 || id > INT16_MAX || property.isEmpty()) {
        server.send(400, "application/json", "{\"error\":\"missing or invalid id/property\"}");
        return;
    }

    component* target = ComponentController.FindByID(static_cast<int16_t>(id));
    if (target == nullptr || !target->IsPublic()) {
        server.send(404, "application/json", "{\"error\":\"component not found\"}");
        return;
    }

    const ComponentPropertyResult result = target->SetProperty(property, value, pdMS_TO_TICKS(100));
    Logger.Log(
        "Web Server: component set " + String(result == ComponentPropertyResult::Accepted ? "accepted" : "rejected") +
            " for " + session->username + "@" + remoteIP.toString() + ": " + target->Name() + "." + property + "=" + value,
        result == ComponentPropertyResult::Accepted ? logger::LogLevels::Information : logger::LogLevels::Warning
    );

    if (result != ComponentPropertyResult::Accepted) {
        server.send(422, "application/json", "{\"error\":\"" + JsonEscaped(PropertyResultMessage(result)) + "\"}");
        return;
    }

    server.send(200, "application/json", "{\"success\":true}");
}

void httpserver::HandleSettingsGet(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    // Setup is served only to admin sessions already (ServeProtectedFile),
    // but the API is checked independently too, and returns real password
    // values on that same basis - matching the CLI, which does the same for
    // an admin Telnet session.
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    JsonDocument document;
    GetNetworkJson(document["Network"].to<JsonObject>());
    GetTelnetJson(document["Telnet"].to<JsonObject>());
    GetNTPJson(document["NTP"].to<JsonObject>());
    GetMQTTJson(document["MQTT"].to<JsonObject>());
    GetLogJson(document["Log"].to<JsonObject>());
    GetWebJson(document["Web"].to<JsonObject>());

    String payload;
    serializeJson(document, payload);
    server.send(200, "application/json", payload);
}

void httpserver::HandleSettingsPost(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    JsonDocument request;
    if (deserializeJson(request, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    const String section = request["section"] | "";
    const JsonObjectConst fields = request["fields"].as<JsonObjectConst>();
    if (section.isEmpty() || fields.isNull()) {
        server.send(400, "application/json", "{\"error\":\"missing section or fields\"}");
        return;
    }

    String error;
    bool applied = false;
    bool restartRequired = false;

    if (section == "Network") {
        applied = ApplyNetworkFields(fields, error);
        restartRequired = true;
    } else if (section == "Telnet") {
        applied = ApplyTelnetFields(fields, error);
        restartRequired = HasField(fields, "Enabled") || HasField(fields, "Port");
    } else if (section == "NTP") {
        applied = ApplyNTPFields(fields, error);
        restartRequired = HasField(fields, "Enabled");
    } else if (section == "MQTT") {
        applied = ApplyMQTTFields(fields, error);
        restartRequired = true;
    } else if (section == "Log") {
        applied = ApplyLogFields(fields, error);
        restartRequired = true;
    } else if (section == "Web") {
        applied = ApplyWebFields(fields, error);
        restartRequired = HasField(fields, "Enabled") || HasField(fields, "Port");
    } else {
        server.send(400, "application/json", "{\"error\":\"unknown section\"}");
        return;
    }

    const IPAddress remoteIP = server.client().remoteIP();

    if (!applied) {
        Logger.Log(
            "Web Server: settings update rejected for " + session->username + "@" + remoteIP.toString() + ": " + section + ": " + error,
            logger::LogLevels::Warning
        );
        server.send(422, "application/json", "{\"error\":\"" + JsonEscaped(error) + "\"}");
        return;
    }

    if (!Settings.Save()) {
        Logger.Log("Web Server: settings update for " + section + " could not be saved to disk", logger::LogLevels::Error);
        server.send(500, "application/json", "{\"error\":\"unable to save configuration\"}");
        return;
    }

    Logger.Log("Web Server: settings update accepted for " + session->username + "@" + remoteIP.toString() + ": " + section, logger::LogLevels::Information);

    // Fields that apply live (idle timeout, session limits, time zone) are
    // pushed to the running instance now that the save succeeded, mirroring
    // the equivalent Telnet CLI commands. Re-pushing an unchanged value is
    // harmless, so this runs unconditionally rather than tracking exactly
    // which field changed.
    if (section == "Telnet") {
        TelnetServer.IdleTimeout(Settings.TelnetServer.IdleTimeoutMs());
        TelnetServer.MaxSessions(Settings.TelnetServer.MaxSessions());
    } else if (section == "Web") {
        HTTPServer.IdleTimeout(Settings.WebServer.IdleTimeoutMs());
        HTTPServer.MaxSessions(Settings.WebServer.MaxSessions());
    } else if (section == "NTP") {
        Clock.TimeZone(Settings.General.TimeZone());
    }

    JsonDocument response;
    response["success"] = true;
    response["restart"] = restartRequired;
    JsonObject values = response["values"].to<JsonObject>();
    if (section == "Network") GetNetworkJson(values);
    else if (section == "Telnet") GetTelnetJson(values);
    else if (section == "NTP") GetNTPJson(values);
    else if (section == "MQTT") GetMQTTJson(values);
    else if (section == "Log") GetLogJson(values);
    else if (section == "Web") GetWebJson(values);

    String payload;
    serializeJson(response, payload);
    server.send(200, "application/json", payload);
}

void httpserver::HandleRebootPost(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    Logger.Log("Web Server: reboot requested by " + session->username + "@" + server.client().remoteIP().toString(), logger::LogLevels::Information);

    // Respond before DeviceRestart() blocks and restarts, so the browser
    // gets its confirmation instead of a dropped connection.
    server.send(200, "application/json", "{\"success\":true}");
    server.client().flush();

    DeviceRestart();
}

void httpserver::HandleConfigExportGet(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    String content;
    if (FileSystem.Read(Defaults.ConfigFileName, content) != filesystem::Result::Ok) {
        server.send(500, "application/json", "{\"error\":\"unable to read configuration\"}");
        return;
    }

    Logger.Log("Web Server: configuration exported by " + session->username + "@" + server.client().remoteIP().toString(), logger::LogLevels::Information);

    server.sendHeader("Content-Disposition", "attachment; filename=\"config.json\"");
    server.send(200, "application/json", content);
}

void httpserver::HandleConfigImportUpload(WebServer& server) {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        pConfigUploadBuffer.clear();
        pConfigUploadBuffer.reserve(4096);
        Session* session = AuthenticatedSession(server);
        pConfigUploadValid = session != nullptr && session->admin;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (pConfigUploadValid && pConfigUploadBuffer.length() + upload.currentSize <= MAX_CONFIG_UPLOAD_BYTES) {
            pConfigUploadBuffer.concat(reinterpret_cast<const char*>(upload.buf), upload.currentSize);
        } else {
            pConfigUploadValid = false;
        }
    }
    // UPLOAD_FILE_END / UPLOAD_FILE_ABORTED: the accumulated buffer is
    // validated and applied in HandleConfigImportPost, once the whole
    // request body has been consumed.
}

void httpserver::HandleConfigImportPost(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        pConfigUploadBuffer.clear();
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        pConfigUploadBuffer.clear();
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    if (!pConfigUploadValid || pConfigUploadBuffer.isEmpty()) {
        pConfigUploadBuffer.clear();
        server.send(400, "application/json", "{\"error\":\"file missing or too large\"}");
        return;
    }

    JsonDocument document;
    const bool validJson = !deserializeJson(document, pConfigUploadBuffer) && document.is<JsonObjectConst>();
    if (!validJson) {
        pConfigUploadBuffer.clear();
        server.send(400, "application/json", "{\"error\":\"file is not a valid configuration (invalid JSON)\"}");
        return;
    }

    const filesystem::Result result = FileSystem.WriteAtomic(Defaults.ConfigFileName, pConfigUploadBuffer);
    pConfigUploadBuffer.clear();

    if (result != filesystem::Result::Ok) {
        server.send(500, "application/json", "{\"error\":\"unable to save configuration\"}");
        return;
    }

    // The imported catalog and settings become the seed of truth again;
    // stale runtime state (keyed by component ID) from before the import
    // could otherwise silently override it on the next boot.
    const filesystem::Result stateRemoval = FileSystem.Remove(Defaults.StateFileName);
    if (stateRemoval != filesystem::Result::Ok && stateRemoval != filesystem::Result::NotFound) {
        Logger.Log("Web Server: could not clear persisted component state after configuration import", logger::LogLevels::Warning);
    }

    Logger.Log(
        "Web Server: configuration imported by " + session->username + "@" + server.client().remoteIP().toString() + "; restarting",
        logger::LogLevels::Information
    );

    server.send(200, "application/json", "{\"success\":true}");
    server.client().flush();

    DeviceRestart();
}

void httpserver::HandleNotFound(WebServer& server) {
    server.send(404, "text/plain", "Not found");
}

void httpserver::ServeFile(WebServer& server, const char* path, const char* contentType) {
    String content;
    if (FileSystem.Read(path, content) != filesystem::Result::Ok) {
        server.send(404, "text/plain", "Not found");
        return;
    }
    server.send(200, contentType, content);
}

void httpserver::ServeProtectedFile(WebServer& server, const char* path, const char* contentType, bool requireAdmin) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "Redirecting to /");
        return;
    }

    if (requireAdmin && !session->admin) {
        server.sendHeader("Location", "/dashboard.html");
        server.send(302, "text/plain", "Redirecting to /dashboard.html");
        return;
    }

    ServeFile(server, path, contentType);
}

httpserver::Session* httpserver::FindSession(const String& token) noexcept {
    if (token.isEmpty()) return nullptr;

    // A value of 0 disables the idle timeout entirely.
    const uint32_t idleTimeoutMs = IdleTimeout();
    const TickType_t now = xTaskGetTickCount();
    for (Session& session : pSessions) {
        if (!session.active || token != session.token) continue;

        if (idleTimeoutMs != 0 && static_cast<TickType_t>(now - session.lastActivityAt) >= pdMS_TO_TICKS(idleTimeoutMs)) {
            session.active = false;
            session.username.clear();
            return nullptr;
        }

        return &session;
    }

    return nullptr;
}

httpserver::Session* httpserver::CreateSession(const String& username, bool admin) noexcept {
    const TickType_t now = xTaskGetTickCount();
    const size_t maxSessions = MaxSessions();

    Session* target = nullptr;
    Session* oldest = nullptr;
    for (size_t index = 0; index < maxSessions; ++index) {
        Session& session = pSessions[index];
        if (!session.active) {
            target = &session;
            break;
        }
        if (oldest == nullptr || static_cast<TickType_t>(session.lastActivityAt - oldest->lastActivityAt) < 0) oldest = &session;
    }
    if (target == nullptr) target = oldest;
    if (target == nullptr) return nullptr;

    const String token = GenerateToken();
    token.toCharArray(target->token, sizeof(target->token));
    target->username = username;
    target->admin = admin;
    target->active = true;
    target->lastActivityAt = now;
    return target;
}

void httpserver::DestroySession(const String& token) noexcept {
    Session* session = FindSession(token);
    if (session == nullptr) return;
    session->active = false;
    session->username.clear();
    session->token[0] = '\0';
}

String httpserver::SessionTokenFromCookie(WebServer& server) {
    if (!server.hasHeader("Cookie")) return String();

    const String cookies = server.header("Cookie");
    const String key = "session=";
    const int start = cookies.indexOf(key);
    if (start < 0) return String();

    const int valueStart = start + key.length();
    const int end = cookies.indexOf(';', valueStart);
    return end < 0 ? cookies.substring(valueStart) : cookies.substring(valueStart, end);
}

httpserver::Session* httpserver::AuthenticatedSession(WebServer& server) noexcept {
    Session* session = FindSession(SessionTokenFromCookie(server));
    if (session != nullptr) session->lastActivityAt = xTaskGetTickCount();
    return session;
}

String httpserver::GenerateToken() {
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));

    char hex[sizeof(bytes) * 2 + 1];
    for (size_t index = 0; index < sizeof(bytes); ++index) {
        snprintf(hex + index * 2, 3, "%02x", bytes[index]);
    }
    return String(hex);
}
