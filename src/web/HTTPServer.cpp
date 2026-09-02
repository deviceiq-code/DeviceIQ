#include "HTTPServer.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <esp_arduino_version.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp32-hal-cpu.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>

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

    // Mirrors UserCommands.cpp's UserReturnMessage.
    const char* UserManagementMessage(UserReturn result) {
        switch (result) {
            case UserReturn::NoError: return "ok";
            case UserReturn::UserExists: return "a user with that name already exists";
            case UserReturn::UserNotFound: return "user not found";
            case UserReturn::MaxUsersReached: return "maximum number of users reached";
            case UserReturn::NoAdminRemaining: return "at least one admin user must remain";
            case UserReturn::InvalidUsername: return "invalid username (3-32 characters: lowercase letters, digits, '.', '_', '-')";
            case UserReturn::PasswordError: return "invalid password (8-64 characters) or unable to hash it";
            case UserReturn::SynchronizationError: return "user store temporarily unavailable";
            default: return "unexpected error";
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
            if (uint16Value == Settings.Webhooks.Port()) { error = "Port must be different from the Webhooks port."; RestoreWeb(snapshot); return false; }
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

    // ---- Webhooks -----------------------------------------------------------

    struct WebhooksSnapshot {
        bool enabled;
        String token;
        uint16_t port;
    };

    WebhooksSnapshot CaptureWebhooks() {
        return {Settings.Webhooks.Enabled(), Settings.Webhooks.Token(), Settings.Webhooks.Port()};
    }

    void RestoreWebhooks(const WebhooksSnapshot& snapshot) {
        Settings.Webhooks.Enabled(snapshot.enabled);
        Settings.Webhooks.Token(snapshot.token);
        Settings.Webhooks.Port(snapshot.port);
    }

    bool IsValidWebhookToken(const String& value) {
        if (value.length() < 15 || value.length() > 30) return false;
        for (size_t i = 0; i < value.length(); ++i) {
            const char c = value.charAt(i);
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
        }
        return true;
    }

    bool ApplyWebhooksFields(JsonObjectConst fields, String& error) {
        const WebhooksSnapshot snapshot = CaptureWebhooks();
        bool boolValue = false;
        String stringValue;
        uint16_t uint16Value = 0;

        if (HasField(fields, "Enabled")) {
            if (!JsonBool(fields, "Enabled", boolValue)) { error = "Enabled must be a boolean."; RestoreWebhooks(snapshot); return false; }
            Settings.Webhooks.Enabled(boolValue);
        }
        if (HasField(fields, "Token")) {
            if (!JsonString(fields, "Token", stringValue) || !IsValidWebhookToken(stringValue)) {
                error = "Token must be 15-30 letters and numbers.";
                RestoreWebhooks(snapshot);
                return false;
            }
            Settings.Webhooks.Token(stringValue);
        }
        if (HasField(fields, "Port")) {
            if (!JsonRanged<uint16_t>(fields, "Port", 1, 65535, uint16Value)) { error = "Port must be 1-65535."; RestoreWebhooks(snapshot); return false; }
            if (uint16Value == Settings.WebServer.Port()) { error = "Port must be different from the Web Server port."; RestoreWebhooks(snapshot); return false; }
            Settings.Webhooks.Port(uint16Value);
        }

        return true;
    }

    void GetWebhooksJson(JsonObject out) {
        out["Enabled"] = Settings.Webhooks.Enabled();
        out["Token"] = Settings.Webhooks.Token();
        out["Port"] = Settings.Webhooks.Port();
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

    // ---- Configuration reset ---------------------------------------------
    // Shared by HandleConfigResetPost and an acknowledged downgrade in
    // HandleOTAUpdatePost. A minimal, schema-valid configuration with no
    // Settings/Users sections and no Components: the normal boot path
    // (settings::Load(), settings::InstallComponents()) falls back to
    // Defaults for every field and reseeds the default admin/user accounts,
    // exactly as it does for a brand new device. The schema version must
    // match the ComponentSchemaVersion constant in ComponentConfig.cpp.
    bool ResetConfigurationToDefaults() {
        const String minimalConfig = "{\"ComponentSchemaVersion\":1,\"Components\":{}}";
        if (FileSystem.WriteAtomic(Defaults.ConfigFileName, minimalConfig) != filesystem::Result::Ok) return false;

        const filesystem::Result stateRemoval = FileSystem.Remove(Defaults.StateFileName);
        if (stateRemoval != filesystem::Result::Ok && stateRemoval != filesystem::Result::NotFound) {
            Logger.Log("Web Server: could not clear persisted component state during configuration reset", logger::LogLevels::Warning);
        }
        return true;
    }

    // ---- Users ------------------------------------------------------------
    // User accounts take effect immediately (Settings.Users is the live
    // authentication store, not a separate runtime copy applied at boot).
    // Rolling back an already-applied mutation on a Save() failure isn't
    // generally possible here (a removed password's hash can't be
    // reconstructed), so a save failure is reported as a warning alongside
    // success rather than as a failure - mirrors UserCommands.cpp exactly.
    void RespondUserMutation(WebServer& server, const String& adminUsername, const char* action, const String& detail) {
        const bool saved = Settings.Save();
        Logger.Log(
            "Web Server: user " + String(action) + " " + String(saved ? "accepted" : "accepted but not saved") + " by " +
                adminUsername + "@" + server.client().remoteIP().toString() + (detail.isEmpty() ? "" : ": " + detail),
            saved ? logger::LogLevels::Information : logger::LogLevels::Error
        );

        if (!saved) {
            server.send(200, "application/json", "{\"success\":true,\"warning\":\"could not save to disk; this change will be lost on restart\"}");
            return;
        }
        server.send(200, "application/json", "{\"success\":true}");
    }

    // ---- Clock ------------------------------------------------------------
    // Howard Hinnant's days_from_civil algorithm (public domain): converts a
    // proleptic Gregorian calendar date directly to a day count since the
    // 1970-01-01 epoch, with no dependency on timegm()/mktime() or the C
    // library's notion of the local timezone (not reliably configured on
    // this device, and timegm() isn't available in this toolchain at all).
    int64_t DaysFromCivil(int year, unsigned month, unsigned day) {
        year -= month <= 2;
        const int64_t era = (year >= 0 ? year : year - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(year - era * 400);
        const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int64_t>(doe) - 719468;
    }

    time_t UTCEpochFromFields(int year, int month, int day, int hour, int minute, int second) {
        const int64_t days = DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
        return static_cast<time_t>(days * 86400 + hour * 3600 + minute * 60 + second);
    }

    // ---- OTA ------------------------------------------------------------
    // Combined firmware+filesystem package produced by scripts/package_ota.py.
    // Layout: this 32-byte header, then <firmwareLength> bytes of
    // firmware.bin, then <filesystemLength> bytes of littlefs.bin. Integers
    // are little-endian, matching both the ESP32-S3 and the desktop
    // machines the package is built on.
    #pragma pack(push, 1)
    struct OTAPackageHeader {
        char magic[8];
        uint8_t headerVersion;
        uint8_t softwareMajor;
        uint8_t softwareMinor;
        uint8_t softwareRevision;
        uint32_t firmwareLength;
        uint32_t firmwareCRC32;
        uint32_t filesystemLength;
        uint32_t filesystemCRC32;
        uint32_t reserved;
    };
    #pragma pack(pop)
    static_assert(sizeof(OTAPackageHeader) == 32, "OTAPackageHeader must be 32 bytes to match scripts/package_ota.py");

    constexpr char OTAPackageMagic[8] = {'D', 'I', 'Q', 'O', 'T', 'A', '0', '1'};
    constexpr uint8_t OTAPackageHeaderVersion = 1;

    // Standard zlib/IEEE 802.3 CRC32 (matches Python's zlib.crc32 and the
    // browser-side JS implementation in update.html): a plain table-based
    // implementation, self-contained rather than built on the ROM's
    // esp_rom_crc32_le - that route produced checksums that didn't match
    // the client/Python side on real hardware.
    uint32_t StandardCRC32(const uint8_t* data, size_t length) {
        static uint32_t table[256];
        static bool tableReady = false;
        if (!tableReady) {
            for (uint32_t n = 0; n < 256; ++n) {
                uint32_t c = n;
                for (uint8_t k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
                table[n] = c;
            }
            tableReady = true;
        }

        uint32_t crc = 0xFFFFFFFFUL;
        for (size_t i = 0; i < length; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFUL;
    }

    int VersionCompare(uint8_t major, uint8_t minor, uint8_t revision) {
        if (major != Version::Software::Major) return major > Version::Software::Major ? 1 : -1;
        if (minor != Version::Software::Minor) return minor > Version::Software::Minor ? 1 : -1;
        if (revision != Version::Software::Revision) return revision > Version::Software::Revision ? 1 : -1;
        return 0;
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
    server.on("/restarting.html", HTTP_GET, [this, &server]() { HandleRestarting(server); });
    server.on("/dashboard.html", HTTP_GET, [this, &server]() { HandleDashboard(server); });
    server.on("/setup.html", HTTP_GET, [this, &server]() { HandleSetup(server); });
    server.on("/update.html", HTTP_GET, [this, &server]() { HandleUpdate(server); });
    server.on("/users.html", HTTP_GET, [this, &server]() { HandleUsers(server); });
    server.on("/component.html", HTTP_GET, [this, &server]() { HandleComponent(server); });
    server.on("/api/users", HTTP_GET, [this, &server]() { HandleUsersGet(server); });
    server.on("/api/users/add", HTTP_POST, [this, &server]() { HandleUsersAddPost(server); });
    server.on("/api/users/remove", HTTP_POST, [this, &server]() { HandleUsersRemovePost(server); });
    server.on("/api/users/rename", HTTP_POST, [this, &server]() { HandleUsersRenamePost(server); });
    server.on("/api/users/set-admin", HTTP_POST, [this, &server]() { HandleUsersSetAdminPost(server); });
    server.on("/api/users/set-password", HTTP_POST, [this, &server]() { HandleUsersSetPasswordPost(server); });
    server.on(
        "/api/ota/update", HTTP_POST,
        [this, &server]() { HandleOTAUpdatePost(server); },
        [this, &server]() { HandleOTAUpload(server); }
    );
    server.on("/about.html", HTTP_GET, [this, &server]() { HandleAbout(server); });
    server.on("/api/about", HTTP_GET, [this, &server]() { HandleAboutGet(server); });
    server.on("/log.html", HTTP_GET, [this, &server]() { HandleLog(server); });
    server.on("/api/log", HTTP_GET, [this, &server]() { HandleLogGet(server); });
    server.on("/api/log/export", HTTP_GET, [this, &server]() { HandleLogExportGet(server); });
    server.on("/api/log/clear", HTTP_POST, [this, &server]() { HandleLogClearPost(server); });
    server.on("/style.css", HTTP_GET, [this, &server]() { HandleStyle(server); });
    server.on("/notifications.js", HTTP_GET, [this, &server]() { HandleScript(server); });
    server.on("/api/login", HTTP_POST, [this, &server]() { HandleLoginPost(server); });
    server.on("/api/logout", HTTP_POST, [this, &server]() { HandleLogoutPost(server); });
    server.on("/api/session", HTTP_GET, [this, &server]() { HandleSessionGet(server); });
    server.on("/api/components", HTTP_GET, [this, &server]() { HandleComponentsGet(server); });
    server.on("/api/components/set", HTTP_POST, [this, &server]() { HandleComponentsSetPost(server); });
    server.on("/api/components/remove", HTTP_POST, [this, &server]() { HandleComponentsRemovePost(server); });
    server.on("/api/components/catalog", HTTP_GET, [this, &server]() { HandleComponentsCatalogGet(server); });
    server.on("/api/components/add", HTTP_POST, [this, &server]() { HandleComponentsAddPost(server); });
    server.on("/api/components/update", HTTP_POST, [this, &server]() { HandleComponentsUpdatePost(server); });
    server.on("/api/settings", HTTP_GET, [this, &server]() { HandleSettingsGet(server); });
    server.on("/api/settings", HTTP_POST, [this, &server]() { HandleSettingsPost(server); });
    server.on("/api/reboot", HTTP_POST, [this, &server]() { HandleRebootPost(server); });
    server.on("/api/config/export", HTTP_GET, [this, &server]() { HandleConfigExportGet(server); });
    server.on(
        "/api/config/import", HTTP_POST,
        [this, &server]() { HandleConfigImportPost(server); },
        [this, &server]() { HandleConfigImportUpload(server); }
    );
    server.on("/api/config/import/apply", HTTP_POST, [this, &server]() { HandleConfigImportApplyPost(server); });
    server.on("/api/config/reset", HTTP_POST, [this, &server]() { HandleConfigResetPost(server); });
    server.on("/api/clock/set", HTTP_POST, [this, &server]() { HandleClockSetPost(server); });
    server.onNotFound([this, &server]() { HandleNotFound(server); });
}

void httpserver::HandleIndex(WebServer& server) {
    ServeFile(server, "/index.html", "text/html");
}

void httpserver::HandleRestarting(WebServer& server) {
    // No auth required: shown right after triggering a restart, when there
    // may be no session left to check, and its only job is to poll until
    // the server answers again at all.
    ServeFile(server, "/restarting.html", "text/html");
}

void httpserver::HandleDashboard(WebServer& server) {
    ServeProtectedFile(server, "/dashboard.html", "text/html", false);
}

void httpserver::HandleSetup(WebServer& server) {
    ServeProtectedFile(server, "/setup.html", "text/html", true);
}

void httpserver::HandleUpdate(WebServer& server) {
    ServeProtectedFile(server, "/update.html", "text/html", true);
}

void httpserver::HandleUsers(WebServer& server) {
    ServeProtectedFile(server, "/users.html", "text/html", true);
}

void httpserver::HandleComponent(WebServer& server) {
    ServeProtectedFile(server, "/component.html", "text/html", true);
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

void httpserver::HandleLog(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "Redirecting to /");
        return;
    }

    // Mirrors the Telnet CLI's "log" command, which is admin-only in full
    // (view included, not just mutations). There is also nothing to show
    // when File isn't one of the configured log endpoints.
    if (!session->admin || (Settings.Log.Endpoint() & logger::Endpoints::File) == 0) {
        server.sendHeader("Location", "/dashboard.html");
        server.send(302, "text/plain", "Redirecting to /dashboard.html");
        return;
    }

    ServeFile(server, "/log.html", "text/html");
}

void httpserver::HandleLogGet(WebServer& server) {
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
    const filesystem::Result readResult = FileSystem.Read(Defaults.LogFileName, content);
    if (readResult == filesystem::Result::NotFound || (readResult == filesystem::Result::Ok && content.isEmpty())) {
        server.send(200, "text/plain", "");
        return;
    }
    if (readResult != filesystem::Result::Ok) {
        server.send(500, "application/json", "{\"error\":\"unable to read the log file\"}");
        return;
    }

    // Mirrors the Telnet CLI's "log view [line_count|all]": walk backward
    // from the end counting newlines, same as LogCommands.cpp.
    const String linesParam = server.arg("lines");
    const bool showAll = linesParam == "all";
    uint32_t requestedLines = 500;
    if (!linesParam.isEmpty() && !showAll) {
        const long parsed = linesParam.toInt();
        if (parsed > 0) requestedLines = static_cast<uint32_t>(parsed);
    }

    size_t start = 0;
    if (!showAll) {
        size_t cursor = content.length();
        while (cursor > 0 && (content[cursor - 1] == '\n' || content[cursor - 1] == '\r')) --cursor;
        start = cursor;
        uint32_t linesFound = 0;
        while (start > 0) {
            --start;
            if (content[start] != '\n') continue;
            ++linesFound;
            if (linesFound == requestedLines) {
                ++start;
                break;
            }
        }
    }

    server.send(200, "text/plain", content.substring(start));
}

void httpserver::HandleLogExportGet(WebServer& server) {
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
    const filesystem::Result readResult = FileSystem.Read(Defaults.LogFileName, content);
    if (readResult != filesystem::Result::Ok && readResult != filesystem::Result::NotFound) {
        server.send(500, "application/json", "{\"error\":\"unable to read the log file\"}");
        return;
    }

    Logger.Log("Web Server: log exported by " + session->username + "@" + server.client().remoteIP().toString(), logger::LogLevels::Information);

    server.sendHeader("Content-Disposition", "attachment; filename=\"device.log\"");
    server.send(200, "text/plain", content);
}

void httpserver::HandleLogClearPost(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    const filesystem::Result result = FileSystem.Remove(Defaults.LogFileName);
    const bool success = result == filesystem::Result::Ok || result == filesystem::Result::NotFound;

    // Logged after clearing: if delivery to file is enabled, this becomes
    // the first entry of the fresh log, so clearing it can never itself go
    // unrecorded - mirrors the Telnet CLI's "log clear".
    Logger.Log(
        "Web Server: log clear " + String(success ? "accepted" : "rejected") + " for " + session->username + "@" + server.client().remoteIP().toString(),
        success ? logger::LogLevels::Information : logger::LogLevels::Warning
    );

    if (!success) {
        server.send(500, "application/json", "{\"error\":\"unable to clear the log file\"}");
        return;
    }

    server.send(200, "application/json", "{\"success\":true}");
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

    const bool logFileEnabled = (Settings.Log.Endpoint() & logger::Endpoints::File) != 0;

    server.send(
        200, "application/json",
        "{\"authenticated\":true,\"username\":\"" + JsonEscaped(session->username) + "\",\"admin\":" + (session->admin ? "true" : "false") +
            ",\"productFamily\":\"" + JsonEscaped(Version::ProductFamily) + "\",\"productName\":\"" + JsonEscaped(Version::ProductName) +
            "\",\"softwareVersion\":\"" + JsonEscaped(Version::Software::Info()) + "\",\"logFileEnabled\":" + (logFileEnabled ? "true" : "false") + "}"
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

void httpserver::HandleComponentsRemovePost(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    char* end = nullptr;
    const String idArg = server.arg("id");
    const long id = std::strtol(idArg.c_str(), &end, 10);
    if (end == idArg.c_str() || id < 1 || id > INT16_MAX) {
        server.send(400, "application/json", "{\"error\":\"missing or invalid id\"}");
        return;
    }

    // Reuses the exact same logic as the Telnet CLI's "comp remove
    // <id>" - removes the entry from config.json only; the running
    // component isn't torn down live, so this requires a restart to
    // actually take effect (matching the CLI's own output).
    String parameters[telnetserver::MAX_COMMAND_PARAMETERS];
    parameters[0] = "remove";
    // Selectors are resolved by ResolveConfiguredComponent() the same way
    // the Telnet CLI's "comp" command resolves them: a bare number is
    // looked up as a component *name*, and only a "#"-prefixed number is
    // treated as an ID (see SelectorID()) - so a numeric ID coming from
    // the web API has to be given that prefix explicitly.
    parameters[1] = "#" + idArg;

    String output;
    const bool success = Settings.ExecuteComponentCommand(parameters, output);
    const IPAddress remoteIP = server.client().remoteIP();

    Logger.Log(
        "Web Server: component remove " + String(success ? "accepted" : "rejected") + " for " + session->username + "@" +
            remoteIP.toString() + ": id " + idArg,
        success ? logger::LogLevels::Information : logger::LogLevels::Warning
    );

    if (!success) {
        output.trim();
        server.send(422, "application/json", "{\"error\":\"" + JsonEscaped(output) + "\"}");
        return;
    }

    server.send(200, "application/json", "{\"success\":true,\"restart\":true}");
}

void httpserver::HandleComponentsCatalogGet(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    JsonDocument catalog;
    if (!Settings.ReadComponentsCatalog(catalog)) {
        server.send(500, "application/json", "{\"error\":\"unable to read component configuration\"}");
        return;
    }

    JsonObjectConst components = catalog["Components"].as<JsonObjectConst>();
    const String idArg = server.arg("id");
    JsonDocument response;

    if (idArg.isEmpty()) {
        // Every configured component, including private Blinds members
        // (never exposed via /api/components) - used to populate
        // selectors like "Relay Up" when adding or editing a Blinds group.
        JsonArray items = response["components"].to<JsonArray>();
        for (JsonPairConst entry : components) {
            JsonObjectConst item = entry.value().as<JsonObjectConst>();
            JsonObjectConst setup = item["Setup"].as<JsonObjectConst>();
            JsonObject out = items.add<JsonObject>();
            out["id"] = String(entry.key().c_str()).toInt();
            out["name"] = setup["Name"] | "";
            out["class"] = setup["Class"] | "";
        }
    } else {
        char* end = nullptr;
        const long id = std::strtol(idArg.c_str(), &end, 10);
        if (end == idArg.c_str() || id < 1 || id > INT16_MAX) {
            server.send(400, "application/json", "{\"error\":\"invalid id\"}");
            return;
        }

        JsonObjectConst item = components[String(id)].as<JsonObjectConst>();
        if (item.isNull()) {
            server.send(404, "application/json", "{\"error\":\"component not found\"}");
            return;
        }

        response["id"] = id;
        response["setup"] = item["Setup"];
        response["enabled"] = item["Properties"]["Enabled"] | true;
    }

    String payload;
    serializeJson(response, payload);
    server.send(200, "application/json", payload);
}

void httpserver::HandleComponentsAddPost(WebServer& server) {
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

    const String componentClass = request["class"] | "";
    if (componentClass != "Relay" && componentClass != "Button" && componentClass != "Blinds" && componentClass != "Thermometer") {
        server.send(400, "application/json", "{\"error\":\"unsupported component class\"}");
        return;
    }

    // A newly created component must already be a fully valid catalog
    // entry - ExecuteComponentCommand("add", ...) validates the whole
    // catalog before it will save anything, and rejects a Relay/Button/
    // Thermometer with no address yet, or a Blinds group with no member
    // relays yet. So identity fields that validation depends on (name,
    // and either the address or the member relays) have to be supplied
    // in this same call; everything else (type, drive mode, timings,
    // ...) can safely follow via a separate /api/components/update,
    // since those don't affect validation.
    String name = request["name"] | "";
    name.trim();
    if (name.isEmpty()) {
        server.send(400, "application/json", "{\"error\":\"name is required\"}");
        return;
    }

    String identityParameters[2];
    if (componentClass == "Blinds") {
        const long relayUp = request["relayUp"] | 0L;
        const long relayDown = request["relayDown"] | 0L;
        if (relayUp < 1 || relayUp > INT16_MAX || relayDown < 1 || relayDown > INT16_MAX) {
            server.send(400, "application/json", "{\"error\":\"relayUp and relayDown are required\"}");
            return;
        }
        identityParameters[0] = "relayup=" + String(relayUp);
        identityParameters[1] = "relaydown=" + String(relayDown);
    } else {
        const long address = request["address"] | -1L;
        if (address < 0 || address > UINT8_MAX) {
            server.send(400, "application/json", "{\"error\":\"address is required\"}");
            return;
        }
        identityParameters[0] = "address=" + String(address);
    }

    JsonDocument catalog;
    if (!Settings.ReadComponentsCatalog(catalog)) {
        server.send(500, "application/json", "{\"error\":\"unable to read component configuration\"}");
        return;
    }

    // Picks the ID explicitly (the lowest unused one) rather than parsing
    // it back out of ExecuteComponentCommand's human-readable output.
    JsonObjectConst components = catalog["Components"].as<JsonObjectConst>();
    int32_t newID = 0;
    for (int32_t candidate = 1; candidate <= INT16_MAX; ++candidate) {
        if (components[String(candidate)].isNull()) {
            newID = candidate;
            break;
        }
    }
    if (newID == 0) {
        server.send(422, "application/json", "{\"error\":\"maximum component count reached\"}");
        return;
    }

    String parameters[telnetserver::MAX_COMMAND_PARAMETERS];
    parameters[0] = "add";
    parameters[1] = componentClass;
    parameters[2] = "id=" + String(newID);
    parameters[3] = "name=" + name;
    parameters[4] = identityParameters[0];
    if (!identityParameters[1].isEmpty()) parameters[5] = identityParameters[1];

    String output;
    const bool success = Settings.ExecuteComponentCommand(parameters, output);
    output.trim();
    const IPAddress remoteIP = server.client().remoteIP();

    Logger.Log(
        "Web Server: component add " + String(success ? "accepted" : "rejected") + " for " + session->username + "@" +
            remoteIP.toString() + ": " + componentClass + (success ? " (#" + String(newID) + ")" : ": " + output),
        success ? logger::LogLevels::Information : logger::LogLevels::Warning
    );

    if (!success) {
        server.send(422, "application/json", "{\"error\":\"" + JsonEscaped(output) + "\"}");
        return;
    }

    JsonDocument response;
    response["success"] = true;
    response["id"] = newID;
    String payload;
    serializeJson(response, payload);
    server.send(200, "application/json", payload);
}

void httpserver::HandleComponentsUpdatePost(WebServer& server) {
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

    const long id = request["id"] | 0L;
    const JsonObjectConst fields = request["fields"].as<JsonObjectConst>();
    if (id < 1 || id > INT16_MAX || fields.isNull()) {
        server.send(400, "application/json", "{\"error\":\"missing id or fields\"}");
        return;
    }

    const String identity = session->username + "@" + server.client().remoteIP().toString();

    // Applied one field at a time (comp set/rename each take a single
    // property=value), stopping at the first rejection - fields already
    // applied stay applied (same "report, don't roll back" approach as
    // every other component mutation here, since none of it takes effect
    // on the running device until an explicit restart anyway).
    for (JsonPairConst field : fields) {
        const String property = field.key().c_str();
        const String value = field.value().as<String>();

        String parameters[telnetserver::MAX_COMMAND_PARAMETERS];
        // See the identical note in HandleComponentsRemovePost: a bare
        // numeric selector is matched against component *names*, not IDs,
        // by ResolveConfiguredComponent()/SelectorID() - it needs the "#"
        // prefix to be treated as an ID.
        parameters[1] = "#" + String(id);
        if (property.equalsIgnoreCase("name")) {
            parameters[0] = "rename";
            parameters[2] = "name=" + value;
        } else {
            parameters[0] = "set";
            parameters[2] = property + "=" + value;
        }

        String output;
        const bool success = Settings.ExecuteComponentCommand(parameters, output);
        output.trim();

        if (!success) {
            Logger.Log(
                "Web Server: component update rejected for " + identity + ": #" + String(id) + " " + property + "=" + value + ": " + output,
                logger::LogLevels::Warning
            );
            server.send(422, "application/json", "{\"error\":\"" + JsonEscaped(property + ": " + output) + "\"}");
            return;
        }
    }

    Logger.Log("Web Server: component update accepted for " + identity + ": #" + String(id), logger::LogLevels::Information);

    server.send(200, "application/json", "{\"success\":true,\"restart\":true}");
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
    GetWebhooksJson(document["Webhooks"].to<JsonObject>());

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
    } else if (section == "Webhooks") {
        applied = ApplyWebhooksFields(fields, error);
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
    else if (section == "Webhooks") GetWebhooksJson(values);

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
        pConfigImportReady = false;
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
        pConfigImportReady = false;
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        pConfigUploadBuffer.clear();
        pConfigImportReady = false;
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    if (!pConfigUploadValid || pConfigUploadBuffer.isEmpty()) {
        pConfigUploadBuffer.clear();
        pConfigImportReady = false;
        server.send(400, "application/json", "{\"error\":\"file missing or too large\"}");
        return;
    }

    JsonDocument document;
    const bool validJson = !deserializeJson(document, pConfigUploadBuffer) && document.is<JsonObjectConst>();
    if (!validJson) {
        pConfigUploadBuffer.clear();
        pConfigImportReady = false;
        server.send(400, "application/json", "{\"error\":\"file is not a valid configuration (invalid JSON)\"}");
        return;
    }

    // Validation only - the buffer is held in memory until the admin
    // confirms via HandleConfigImportApplyPost, or a new upload replaces it.
    pConfigImportReady = true;
    server.send(200, "application/json", "{\"valid\":true}");
}

void httpserver::HandleConfigImportApplyPost(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    if (!pConfigImportReady || pConfigUploadBuffer.isEmpty()) {
        server.send(409, "application/json", "{\"error\":\"no validated configuration to import; upload a file first\"}");
        return;
    }

    const filesystem::Result result = FileSystem.WriteAtomic(Defaults.ConfigFileName, pConfigUploadBuffer);
    pConfigUploadBuffer.clear();
    pConfigImportReady = false;

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

void httpserver::HandleConfigResetPost(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    if (!ResetConfigurationToDefaults()) {
        server.send(500, "application/json", "{\"error\":\"unable to reset configuration\"}");
        return;
    }

    Logger.Log(
        "Web Server: configuration reset to factory defaults by " + session->username + "@" + server.client().remoteIP().toString() + "; restarting",
        logger::LogLevels::Warning
    );

    server.send(200, "application/json", "{\"success\":true}");
    server.client().flush();

    DeviceRestart();
}

void httpserver::HandleClockSetPost(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    // NTP would just overwrite a manual change on its next sync, and
    // disagreeing sources of truth for the clock are confusing - the web
    // UI only shows this control while NTP is off, and this mirrors that
    // server-side.
    if (Settings.General.NTPUpdate()) {
        server.send(409, "application/json", "{\"error\":\"disable NTP before setting the clock manually\"}");
        return;
    }

    JsonDocument request;
    if (deserializeJson(request, server.arg("plain")) || !request["datetime"].is<const char*>()) {
        server.send(400, "application/json", "{\"error\":\"missing or invalid datetime\"}");
        return;
    }

    const String datetimeValue = request["datetime"].as<String>();
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    const int parsedFields = sscanf(datetimeValue.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);

    if (parsedFields < 5 || year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        server.send(400, "application/json", "{\"error\":\"datetime must look like YYYY-MM-DDTHH:MM:SS\"}");
        return;
    }

    // The admin enters local wall-clock time, but Clock stores UTC and only
    // adds the configured time zone offset when formatting for display
    // (rtc::GetTimeInfo) - so that offset has to be subtracted here to get
    // back to the UTC epoch SetEpoch() expects.
    const time_t enteredAsUTC = UTCEpochFromFields(year, month, day, hour, minute, second);
    const time_t utcEpoch = enteredAsUTC - static_cast<time_t>(Clock.TimeZone()) * 3600;

    Clock.SetEpoch(utcEpoch);

    Logger.Log(
        "Web Server: clock set manually by " + session->username + "@" + server.client().remoteIP().toString() + " to " + Clock.GetDateTime(),
        logger::LogLevels::Information
    );

    server.send(200, "application/json", "{\"success\":true}");
}

void httpserver::HandleUsersGet(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    JsonDocument document;
    JsonArray users = document["users"].to<JsonArray>();
    // Passwords are one-way hashed (PBKDF2) - there is no plaintext value to
    // ever return here, unlike the reversible config secrets (WiFi/MQTT/
    // Webhooks) shown elsewhere in Setup.
    Settings.Users.ForEachStored([&users](const String& username, bool admin, const uint8_t*, const uint8_t*) {
        JsonObject entry = users.add<JsonObject>();
        entry["username"] = username;
        entry["admin"] = admin;
    });

    String payload;
    serializeJson(document, payload);
    server.send(200, "application/json", payload);
}

void httpserver::HandleUsersAddPost(WebServer& server) {
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

    const String username = request["username"] | "";
    const String password = request["password"] | "";
    const bool admin = request["admin"] | false;

    const UserReturn result = Settings.Users.Add(username, password, admin);
    if (result != UserReturn::NoError) {
        server.send(422, "application/json", "{\"error\":\"" + JsonEscaped(UserManagementMessage(result)) + "\"}");
        return;
    }

    RespondUserMutation(server, session->username, "add", username);
}

void httpserver::HandleUsersRemovePost(WebServer& server) {
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

    const String username = request["username"] | "";

    const UserReturn result = Settings.Users.Remove(username);
    if (result != UserReturn::NoError) {
        server.send(422, "application/json", "{\"error\":\"" + JsonEscaped(UserManagementMessage(result)) + "\"}");
        return;
    }

    RespondUserMutation(server, session->username, "remove", username);
}

void httpserver::HandleUsersRenamePost(WebServer& server) {
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

    const String username = request["username"] | "";
    const String newUsername = request["newUsername"] | "";

    const UserReturn result = Settings.Users.Rename(username, newUsername);
    if (result != UserReturn::NoError) {
        server.send(422, "application/json", "{\"error\":\"" + JsonEscaped(UserManagementMessage(result)) + "\"}");
        return;
    }

    RespondUserMutation(server, session->username, "rename", username + " -> " + newUsername);
}

void httpserver::HandleUsersSetAdminPost(WebServer& server) {
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
    if (deserializeJson(request, server.arg("plain")) || !request["admin"].is<bool>()) {
        server.send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    const String username = request["username"] | "";
    const bool admin = request["admin"];

    const UserReturn result = Settings.Users.SetAdmin(username, admin);
    if (result != UserReturn::NoError) {
        server.send(422, "application/json", "{\"error\":\"" + JsonEscaped(UserManagementMessage(result)) + "\"}");
        return;
    }

    RespondUserMutation(server, session->username, "set-admin", username);
}

void httpserver::HandleUsersSetPasswordPost(WebServer& server) {
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

    const String username = request["username"] | "";
    const String password = request["password"] | "";

    const UserReturn result = Settings.Users.SetPassword(username, password);
    if (result != UserReturn::NoError) {
        server.send(422, "application/json", "{\"error\":\"" + JsonEscaped(UserManagementMessage(result)) + "\"}");
        return;
    }

    RespondUserMutation(server, session->username, "set-password", username);
}

void httpserver::HandleOTAUpload(WebServer& server) {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        if (pOTABuffer != nullptr) {
            heap_caps_free(pOTABuffer);
            pOTABuffer = nullptr;
        }
        pOTAReceived = 0;

        Session* session = AuthenticatedSession(server);
        pOTAUploadValid = session != nullptr && session->admin;
        if (pOTAUploadValid) {
            pOTABuffer = static_cast<uint8_t*>(heap_caps_malloc(MAX_OTA_UPLOAD_BYTES, MALLOC_CAP_SPIRAM));
            if (pOTABuffer == nullptr) {
                pOTAUploadValid = false;
                Logger.Log("Web Server: unable to allocate OTA upload buffer from PSRAM", logger::LogLevels::Error);
            }
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (pOTAUploadValid && pOTAReceived + upload.currentSize <= MAX_OTA_UPLOAD_BYTES) {
            memcpy(pOTABuffer + pOTAReceived, upload.buf, upload.currentSize);
            pOTAReceived += upload.currentSize;
        } else {
            pOTAUploadValid = false;
        }
    }
    // UPLOAD_FILE_END / UPLOAD_FILE_ABORTED: the accumulated buffer is
    // validated and applied in HandleOTAUpdatePost, once the whole request
    // body has been consumed.
}

void httpserver::HandleOTAUpdatePost(WebServer& server) {
    Session* session = AuthenticatedSession(server);
    if (session == nullptr) {
        if (pOTABuffer != nullptr) { heap_caps_free(pOTABuffer); pOTABuffer = nullptr; }
        server.send(401, "application/json", "{\"error\":\"unauthenticated\"}");
        return;
    }
    if (!session->admin) {
        if (pOTABuffer != nullptr) { heap_caps_free(pOTABuffer); pOTABuffer = nullptr; }
        server.send(403, "application/json", "{\"error\":\"admin session required\"}");
        return;
    }

    // Validation failures below touch nothing on flash - just report the
    // error and free the buffer, no restart needed.
    auto fail = [&](int code, const String& message) {
        if (pOTABuffer != nullptr) { heap_caps_free(pOTABuffer); pOTABuffer = nullptr; }
        pOTAReceived = 0;
        pOTAUploadValid = false;
        server.send(code, "application/json", "{\"error\":\"" + JsonEscaped(message) + "\"}");
    };

    if (!pOTAUploadValid || pOTABuffer == nullptr || pOTAReceived < sizeof(OTAPackageHeader)) {
        fail(400, "file missing, too large, or too small");
        return;
    }

    OTAPackageHeader header;
    memcpy(&header, pOTABuffer, sizeof(header));

    if (memcmp(header.magic, OTAPackageMagic, sizeof(OTAPackageMagic)) != 0 || header.headerVersion != OTAPackageHeaderVersion) {
        fail(400, "not a valid DeviceIQ OTA package");
        return;
    }

    const size_t expectedTotal = sizeof(OTAPackageHeader) + static_cast<size_t>(header.firmwareLength) + static_cast<size_t>(header.filesystemLength);
    if (expectedTotal != pOTAReceived) {
        fail(400, "package is truncated or corrupt (size mismatch)");
        return;
    }

    uint8_t* firmwareData = pOTABuffer + sizeof(OTAPackageHeader);
    uint8_t* filesystemData = firmwareData + header.firmwareLength;

    if (StandardCRC32(firmwareData, header.firmwareLength) != header.firmwareCRC32 ||
        StandardCRC32(filesystemData, header.filesystemLength) != header.filesystemCRC32) {
        fail(400, "package is corrupt (checksum mismatch)");
        return;
    }

    const bool isDowngrade = VersionCompare(header.softwareMajor, header.softwareMinor, header.softwareRevision) < 0;
    const String packageVersion = String(header.softwareMajor) + "." + String(header.softwareMinor) + "." + String(header.softwareRevision);

    if (isDowngrade && server.arg("confirmDowngrade") != "1") {
        fail(
            409,
            "downgrade from " + Version::Software::Info() + " to " + packageVersion + " requires confirmation"
        );
        return;
    }

    const String identity = session->username + "@" + server.client().remoteIP().toString();

    // Past this point every failure is treated as committed: the
    // filesystem partition may already be unmounted or partially erased,
    // so DeviceRestart() is called regardless of outcome, letting the
    // normal boot sequence (filesystem::Start() formats on a bad mount)
    // recover cleanly instead of leaving the device running unmounted.
    auto failDuringApply = [&](const String& message) {
        Logger.Log("Web Server: OTA update failed for " + identity + ": " + message + "; restarting", logger::LogLevels::Error);
        server.send(500, "application/json", "{\"error\":\"" + JsonEscaped(message) + "\"}");
        server.client().flush();
        if (pOTABuffer != nullptr) { heap_caps_free(pOTABuffer); pOTABuffer = nullptr; }
        pOTAReceived = 0;
        pOTAUploadValid = false;
        DeviceRestart();
    };

    // The packaged filesystem image is built from this firmware's own
    // data/ directory, so it carries the checked-in example config.json,
    // not the device's live configuration. Captured here, while the live
    // filesystem is still mounted, so it can be restored once the new
    // image is flashed - otherwise every OTA update would silently reset
    // WiFi/MQTT/webhook/user settings back to those placeholder values.
    // Skipped on a downgrade, which already resets to defaults below.
    String configBackup;
    const bool hasConfigBackup = !isDowngrade && FileSystem.Read(Defaults.ConfigFileName, configBackup) == filesystem::Result::Ok;

    // The filesystem partition has no A/B redundancy, so it is written
    // first: if it fails, the firmware update below (and the boot
    // partition switch on its own Update.end()) never happens, keeping the
    // firmware/filesystem pairing consistent even on failure.
    FileSystem.Stop();

    if (!Update.begin(header.filesystemLength, U_SPIFFS)) {
        failDuringApply(String("filesystem update failed: ") + Update.errorString());
        return;
    }
    if (Update.write(filesystemData, header.filesystemLength) != header.filesystemLength || !Update.end(true)) {
        failDuringApply(String("filesystem update failed: ") + Update.errorString());
        return;
    }

    // Remount the freshly-flashed filesystem just long enough to put the
    // right config.json back before the firmware flash below and the
    // restart that follows it.
    if (!FileSystem.Start()) {
        Logger.Log("Web Server: OTA update for " + identity + " could not remount the filesystem to restore configuration", logger::LogLevels::Error);
    } else if (isDowngrade) {
        if (!ResetConfigurationToDefaults()) {
            Logger.Log("Web Server: OTA downgrade to " + packageVersion + " applied for " + identity + ", but configuration reset failed", logger::LogLevels::Error);
        }
    } else if (hasConfigBackup && FileSystem.WriteAtomic(Defaults.ConfigFileName, configBackup) != filesystem::Result::Ok) {
        Logger.Log("Web Server: OTA update for " + identity + " could not restore the prior configuration; the device will boot with the package's default configuration", logger::LogLevels::Error);
    }
    FileSystem.Stop();

    if (!Update.begin(header.firmwareLength, U_FLASH)) {
        failDuringApply(String("firmware update failed: ") + Update.errorString());
        return;
    }
    if (Update.write(firmwareData, header.firmwareLength) != header.firmwareLength || !Update.end(true)) {
        failDuringApply(String("firmware update failed: ") + Update.errorString());
        return;
    }

    heap_caps_free(pOTABuffer);
    pOTABuffer = nullptr;
    pOTAReceived = 0;
    pOTAUploadValid = false;

    Logger.Log(
        "Web Server: OTA " + String(isDowngrade ? "downgrade" : "update") + " from " + Version::Software::Info() + " to " + packageVersion +
            " applied by " + identity + (isDowngrade ? "; configuration reset to defaults" : "") + "; restarting",
        logger::LogLevels::Warning
    );

    server.send(200, "application/json", "{\"success\":true,\"downgrade\":" + String(isDowngrade ? "true" : "false") + "}");
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
