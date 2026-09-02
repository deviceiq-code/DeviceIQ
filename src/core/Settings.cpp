#include "Settings.h"
#include "Globals.h"
#include "components/Blinds.h"
#include "components/Button.h"
#include "components/Thermometer.h"

#include <ArduinoJson.h>

namespace {
    void CompactCredentialArrays(String& json) {
        String compact;
        compact.reserve(json.length());
        size_t offset = 0;

        while (offset < json.length()) {
            const int saltPosition = json.indexOf("\"Salt\"", offset);
            const int hashPosition = json.indexOf("\"Hash\"", offset);
            int keyPosition = -1;
            if (saltPosition >= 0 && hashPosition >= 0) keyPosition = min(saltPosition, hashPosition);
            else keyPosition = saltPosition >= 0 ? saltPosition : hashPosition;

            if (keyPosition < 0) {
                compact += json.substring(offset);
                break;
            }

            const int arrayStart = json.indexOf('[', keyPosition);
            const int arrayEnd = arrayStart < 0 ? -1 : json.indexOf(']', arrayStart);
            if (arrayStart < 0 || arrayEnd < 0) {
                compact += json.substring(offset);
                break;
            }

            compact += json.substring(offset, static_cast<size_t>(arrayStart) + 1);
            for (int index = arrayStart + 1; index < arrayEnd; ++index) {
                const char value = json[index];
                if (value == ' ' || value == '\t' || value == '\r' || value == '\n') continue;
                compact += value;
                if (value == ',') compact += ' ';
            }
            compact += ']';
            offset = static_cast<size_t>(arrayEnd) + 1;
        }

        json = std::move(compact);
    }
}

settings::settings() noexcept
    : pMutex(xSemaphoreCreateRecursiveMutexStatic(&pMutexStorage)),
      Log(pMutex),
      Network(pMutex),
      Update(pMutex),
      General(pMutex),
      Orchestrator(pMutex),
      WebServer(pMutex),
      TelnetServer(pMutex),
      MQTT(pMutex) {
    configASSERT(pMutex != nullptr);
}


void settings::network::Hostname(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();
    value.toLowerCase();
    value.replace(" ", "-");

    for (size_t i = 0; i < value.length(); ++i) {
        char c = value.charAt(i);
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-');
        if (!ok) value.setCharAt(i, '-');
    }

    while (value.indexOf("--") >= 0) value.replace("--", "-");
    while (value.length() > 0 && value.charAt(0) == '-') value.remove(0, 1);
    while (value.length() > 0 && value.charAt(value.length() - 1) == '-') value.remove(value.length() - 1);

    if (value.length() > 63) value.remove(63);
    if (value.length() == 0) value = "dev";

    pHostname = std::move(value);
}

void settings::sanitizeIpString(String& s) noexcept {
    s.trim();
    s.replace(',', '.');
    s.replace(" ", "");

    while (s.indexOf("..") >= 0) s.replace("..", ".");

    if (s.length() && s[0] == '.')   s.remove(0, 1);
    if (s.length() && s[s.length()-1] == '.') s.remove(s.length()-1);
}

void settings::network::IP_Address(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    sanitizeIpString(value);

    if (value.length() == 0) {
        pIP_Address = IPAddress(0,0,0,0);
        return;
    }

    IPAddress parsed;
    if (!parsed.fromString(value)) {
        pIP_Address = IPAddress(0,0,0,0);
        return;
    }

    if (parsed[0] == 255 && parsed[1] == 255 && parsed[2] == 255 && parsed[3] == 255) {
        pIP_Address = IPAddress(0,0,0,0);
        return;
    }

    pIP_Address = parsed;
}

void settings::orchestrator::IP_Address(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    sanitizeIpString(value);

    if (value.length() == 0) {
        pIP_Address = IPAddress(0,0,0,0);
        return;
    }

    IPAddress parsed;
    if (!parsed.fromString(value)) {
        pIP_Address = IPAddress(0,0,0,0);
        return;
    }

    if (parsed[0] == 255 && parsed[1] == 255 && parsed[2] == 255 && parsed[3] == 255) {
        pIP_Address = IPAddress(0,0,0,0);
        return;
    }

    pIP_Address = parsed;
}

void settings::network::Gateway(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    sanitizeIpString(value);

    if (value.length() == 0) {
        pGateway = IPAddress(0,0,0,0);
        return;
    }

    IPAddress parsed;
    if (!parsed.fromString(value)) {
        pGateway = IPAddress(0,0,0,0);
        return;
    }

    bool isBroadcast = (parsed[0] == 255 && parsed[1] == 255 && parsed[2] == 255 && parsed[3] == 255);
    bool isMulticast = (parsed[0] >= 224 && parsed[0] <= 239);

    if (isBroadcast || isMulticast || parsed == IPAddress(0,0,0,0)) {
        pGateway = IPAddress(0,0,0,0);
        return;
    }

    pGateway = parsed;
}

bool settings::network::isValidNetmask(const IPAddress& mask) noexcept {
    uint32_t m = (uint32_t(mask[0]) << 24) | (uint32_t(mask[1]) << 16) | (uint32_t(mask[2]) << 8)  | uint32_t(mask[3]);
    return m != 0 && ((m | (m - 1)) == 0xFFFFFFFFu);
}

void settings::network::stripControlChars(String& s) noexcept {
    String out; out.reserve(s.length());
    for (size_t i = 0; i < s.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c >= 0x20 && c != 0x7F)) out += char(c);
    }
    s = out;
}

bool settings::network::isPrintableASCII(const String& s) noexcept {
    for (size_t i = 0; i < s.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

bool settings::network::isHex64(const String& s) noexcept {
    if (s.length() != 64) return false;
    for (size_t i = 0; i < 64; ++i) {
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

void settings::network::Netmask(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    sanitizeIpString(value);

    if (value.length() == 0) {
        pNetmask = IPAddress(255,255,255,0);
        return;
    }

    IPAddress parsed;
    if (!parsed.fromString(value)) {
        pNetmask = IPAddress(255,255,255,0);
        return;
    }

    if (!isValidNetmask(parsed)) {
        pNetmask = IPAddress(255,255,255,0);
        return;
    }

    pNetmask = parsed;
}

void settings::network::DNS(uint8_t index, String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    if (index >= 2) return;
    sanitizeIpString(value);

    if (value.length() == 0) { pDNS[index] = IPAddress(0,0,0,0); return; }

    IPAddress parsed;
    if (!parsed.fromString(value)) { pDNS[index] = IPAddress(0,0,0,0); return; }

    bool isBroadcast = (parsed[0]==255 && parsed[1]==255 && parsed[2]==255 && parsed[3]==255);
    bool isMulticast = (parsed[0] >= 224 && parsed[0] <= 239);
    if (isBroadcast || isMulticast) { pDNS[index] = IPAddress(0,0,0,0); return; }

    pDNS[index] = parsed;
}

void settings::network::SSID(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();
    stripControlChars(value);

    if (value.length() > 32) value.remove(32);

    pSSID = std::move(value);
}

void settings::network::Passphrase(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();

    if (value.length() == 0) {
        pPassphrase = String();
        return;
    }

    if (isHex64(value)) {
        pPassphrase = std::move(value);
        return;
    }

    if (value.length() >= 8 && value.length() <= 63 && isPrintableASCII(value)) {
        pPassphrase = std::move(value);
        return;
    }

    pPassphrase = String();
}

void settings::network::FallbackAPSSID(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();
    stripControlChars(value);
    if (value.length() > 32) value.remove(32);
    pFallbackAPSSID = std::move(value);
}

void settings::network::FallbackAPPassword(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();

    if (value.isEmpty() || (value.length() >= 8 && value.length() <= 63 && isPrintableASCII(value))) {
        pFallbackAPPassword = std::move(value);
        return;
    }

    pFallbackAPPassword = Defaults.Network.FallbackAPPassword;
}

void settings::update::ManifestURL(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();
    value.toLowerCase();

    constexpr size_t MIN_URL_LEN = 10;
    constexpr size_t MAX_URL_LEN = 200;

    if (value.length() < MIN_URL_LEN || value.length() > MAX_URL_LEN) {
        pManifestURL = String();
        return;
    }

    if (!value.startsWith("http://") && !value.startsWith("https://")) {
        pManifestURL = String();
        return;
    }
    for (size_t i = 0; i < value.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (c <= 0x20 || c >= 0x7F) {
            pManifestURL = String();
            return;
        }
    }

    pManifestURL = std::move(value);
}

void settings::update::PasswordLANOTA(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();

    constexpr size_t MIN_LEN = 6;
    constexpr size_t MAX_LEN = 64;

    if (value.length() < MIN_LEN || value.length() > MAX_LEN) {
        pPasswordLANOTA = String();
        return;
    }

    for (size_t i = 0; i < value.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (c < 0x20 || c > 0x7E) {
            pPasswordLANOTA = String();
            return;
        }
    }

    pPasswordLANOTA = std::move(value);
}

void settings::general::NTPServer(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();
    value.toLowerCase();

    constexpr size_t MIN_LEN = 3;
    constexpr size_t MAX_LEN = 128;

    if (value.length() == 0) {
        pNTPServer = "pool.ntp.org";
        return;
    }

    if (value.length() < MIN_LEN || value.length() > MAX_LEN) {
        pNTPServer = "pool.ntp.org";
        return;
    }

    for (size_t i = 0; i < value.length(); ++i) {
        char c = value.charAt(i);
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '.') || (c == '-');
        if (!ok) {
            pNTPServer = "pool.ntp.org";
            return;
        }
    }

    if (value.indexOf(' ') >= 0) {
        pNTPServer = "pool.ntp.org";
        return;
    }

    pNTPServer = std::move(value);
}

void settings::orchestrator::ServerID(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();
    value.toUpperCase();

    constexpr size_t REQUIRED_LEN = 15;
    if (value.length() != REQUIRED_LEN) {
        pServerID = String();
        return;
    }

    for (size_t i = 0; i < value.length(); ++i) {
        char c = value.charAt(i);
        bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!ok) {
            pServerID = String();
            return;
        }
    }

    pServerID = std::move(value);
}

void settings::webserver::WebHooksToken(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();

    constexpr size_t MIN_LEN = 1;
    constexpr size_t MAX_LEN = 64;

    if (value.length() < MIN_LEN || value.length() > MAX_LEN) {
        pWebHooksToken = String();
        return;
    }

    for (size_t i = 0; i < value.length(); ++i) {
        char c = value.charAt(i);
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-') || (c == '_');
        if (!ok) {
            pWebHooksToken = String();
            return;
        }
    }

    pWebHooksToken = std::move(value);
}

void settings::mqtt::Broker(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();
    value.toLowerCase();

    constexpr size_t MIN_LEN = 3;
    constexpr size_t MAX_LEN = 128;

    if (value.length() == 0) {
        pBroker = String();
        return;
    }

    if (value.length() < MIN_LEN || value.length() > MAX_LEN) {
        pBroker = String();
        return;
    }

    for (size_t i = 0; i < value.length(); ++i) {
        char c = value.charAt(i);
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '.') || (c == '-');
        if (!ok) {
            pBroker = String();
            return;
        }
    }

    if (value.indexOf(' ') >= 0) {
        pBroker = String();
        return;
    }

    pBroker = std::move(value);
}

void settings::mqtt::User(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();

    constexpr size_t MIN_LEN = 3;
    constexpr size_t MAX_LEN = 64;

    if (value.length() < MIN_LEN || value.length() > MAX_LEN) {
        pUser = String();
        return;
    }

    for (size_t i = 0; i < value.length(); ++i) {
        char c = value.charAt(i);
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '.') || (c == '_') || (c == '-');
        if (!ok) {
            pUser = String();
            return;
        }
    }

    pUser = std::move(value);
}

void settings::mqtt::Password(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();

    constexpr size_t MIN_LEN = 6;
    constexpr size_t MAX_LEN = 64;

    if (value.length() < MIN_LEN || value.length() > MAX_LEN) {
        pPassword = String();
        return;
    }

    for (size_t i = 0; i < value.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (c < 0x20 || c > 0x7E) {
            pPassword = String();
            return;
        }
    }

    pPassword = std::move(value);
}

void settings::mqtt::DiscoveryPrefix(String value) noexcept {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    value.trim();

    if (value.isEmpty() || value.length() > 64) {
        pDiscoveryPrefix = Defaults.MQTT.DiscoveryPrefix;
        return;
    }

    for (size_t index = 0; index < value.length(); ++index) {
        const char current = value.charAt(index);
        const bool valid = (current >= 'A' && current <= 'Z') ||
            (current >= 'a' && current <= 'z') ||
            (current >= '0' && current <= '9') || current == '_' || current == '-';
        if (!valid) {
            pDiscoveryPrefix = Defaults.MQTT.DiscoveryPrefix;
            return;
        }
    }

    pDiscoveryPrefix = std::move(value);
}

void settings::LoadDefaults() {
    Lock lock(pMutex);
    if (!lock.IsLocked()) return;
    // Log
    Log.Endpoint(Defaults.Log.Endpoint);
    Log.LogLevel(Defaults.Log.Level);
    Log.SyslogServerHost(Defaults.Log.SyslogServer);
    Log.SyslogServerPort(Defaults.Log.SyslogPort);

    // Network
    Network.DHCPClient(Defaults.Network.DHCPClient);
    Network.Hostname(Defaults.Network.Hostname());
    Network.IP_Address(Defaults.Network.IP_Address);
    Network.Gateway(Defaults.Network.Gateway);
    Network.Netmask(Defaults.Network.Netmask);
    Network.DNS(0, Defaults.Network.DNS[0]);
    Network.DNS(1, Defaults.Network.DNS[1]);
    Network.SSID(Defaults.Network.SSID);
    Network.Passphrase(Defaults.Network.Passphrase);
    Network.ConnectionTimeout(Defaults.Network.ConnectionTimeout);
    Network.ReconnectEnabled(Defaults.Network.ReconnectEnabled);
    Network.ReconnectInitialInterval(Defaults.Network.ReconnectInitialInterval);
    Network.ReconnectMaximumInterval(Defaults.Network.ReconnectMaximumInterval);
    Network.FallbackAPEnabled(Defaults.Network.FallbackAPEnabled);
    Network.FallbackAPSSID(Defaults.Network.FallbackAPSSID);
    Network.FallbackAPPassword(Defaults.Network.FallbackAPPassword);
    Network.FallbackAPRetention(Defaults.Network.FallbackAPRetention);

    // Update
    Update.ManifestURL(Defaults.Update.ManifestURL);
    Update.AllowInsecure(Defaults.Update.AllowInsecure);
    Update.EnableLANOTA(Defaults.Update.EnableLANOTA);
    Update.PasswordLANOTA(Defaults.Update.PasswordLANOTA);
    Update.CheckInterval(Defaults.Update.CheckInterval);
    Update.AutoReboot(Defaults.Update.AutoReboot);
    Update.Debug(Defaults.Update.Debug);
    Update.CheckAtStartup(Defaults.Update.CheckAtStartup);

    // General
    General.NTPUpdate(Defaults.General.NTPUpdate);
    General.NTPServer(Defaults.General.NTPServer);
    General.TimeZone(Defaults.General.TimeZone);
    General.SaveStatePooling(Defaults.General.SaveStatePooling);

    // Orchestrator
    Orchestrator.Assigned(Defaults.Orchestrator.Assigned);
    Orchestrator.ServerID(Defaults.Orchestrator.ServerID);
    Orchestrator.IP_Address(Defaults.Orchestrator.IP_Address);
    Orchestrator.Port(Defaults.Orchestrator.Port);

    // WebServer
    WebServer.Port(Defaults.WebServer.Port);
    WebServer.Enabled(Defaults.WebServer.Enabled);
    WebServer.WebHooksToken(Defaults.WebServer.WebHooksToken);
    WebServer.IdleTimeoutMs(Defaults.WebServer.IdleTimeoutMs);
    WebServer.MaxSessions(Defaults.WebServer.MaxSessions);

    // TelnetServer
    TelnetServer.Port(Defaults.TelnetServer.Port);
    TelnetServer.Enabled(Defaults.TelnetServer.Enabled);
    TelnetServer.IdleTimeoutMs(Defaults.TelnetServer.IdleTimeoutMs);
    TelnetServer.MaxSessions(Defaults.TelnetServer.MaxSessions);

    // MQTT
    MQTT.Enabled(Defaults.MQTT.Enabled);
    MQTT.Broker(Defaults.MQTT.Broker);
    MQTT.Port(Defaults.MQTT.Port);
    MQTT.User(Defaults.MQTT.User);
    MQTT.Password(Defaults.MQTT.Password);
    MQTT.DiscoveryEnabled(Defaults.MQTT.DiscoveryEnabled);
    MQTT.DiscoveryPrefix(Defaults.MQTT.DiscoveryPrefix);
}

bool settings::Load(const String& configfilename) noexcept {
    const String path = configfilename.length() ? configfilename : String(Defaults.ConfigFileName);
    String content;
    const filesystem::Result readResult = FileSystem.Read(path.c_str(), content);

    if (readResult != filesystem::Result::Ok || content.isEmpty()) {
        const bool firstRun = readResult == filesystem::Result::NotFound;

        {
            Lock lock(pMutex);
            if (!lock.IsLocked()) return false;
            LoadDefaults();
            pFirstRun = firstRun;
        }

        if (firstRun && Users.Count() == 0) {
            Users.Add(Defaults.Users.Admin.Username, Defaults.Users.Admin.Password, true);
            Users.Add(Defaults.Users.User.Username, Defaults.Users.User.Password, false);
        }

        return false;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, content);
    if (err) {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return false;
        LoadDefaults();
        pFirstRun = false;
        return false;
    }

    JsonObjectConst root = doc.as<JsonObjectConst>();
    if (root.isNull()) {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return false;
        LoadDefaults();
        pFirstRun = false;
        return false;
    }

    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return false;
        LoadDefaults();

        // Log
        if (root["Log"].is<JsonObjectConst>()) {
            JsonObjectConst log = root["Log"].as<JsonObjectConst>();
            Log.Endpoint((uint8_t)(log["Endpoint"] | Defaults.Log.Endpoint));
            Log.LogLevel((uint8_t)(log["Level"] | Defaults.Log.Level));
            Log.SyslogServerHost(String(log["Syslog Server"] | Defaults.Log.SyslogServer));
            Log.SyslogServerPort((uint16_t)(log["Syslog Port"] | Defaults.Log.SyslogPort));
        }

        // Network
        if (root["Network"].is<JsonObjectConst>()) {
            JsonObjectConst net = root["Network"].as<JsonObjectConst>();
            Network.DHCPClient((bool)(net["DHCP Client"] | Defaults.Network.DHCPClient));
            Network.Hostname(String(net["Hostname"] | Defaults.Network.Hostname()));
            Network.IP_Address(String(net["IP Address"] | Defaults.Network.IP_Address));
            Network.Gateway(String(net["Gateway"] | Defaults.Network.Gateway));
            Network.Netmask(String(net["Netmask"] | Defaults.Network.Netmask));

            if (net["DNS Servers"].is<JsonArrayConst>()) {
                JsonArrayConst dns = net["DNS Servers"].as<JsonArrayConst>();
                for (uint8_t idx = 0; idx < dns.size() && idx < 2; idx++) {
                    Network.DNS(idx, dns[idx].as<String>());
                }
            }

            Network.SSID(String(net["SSID"] | Defaults.Network.SSID));
            Network.Passphrase(String(net["Passphrase"] | Defaults.Network.Passphrase));
            Network.ConnectionTimeout((uint16_t)(net["Connection Timeout"] | Defaults.Network.ConnectionTimeout));
            Network.ReconnectEnabled((bool)(net["Reconnect Enabled"] | (net["Online Checking"] | Defaults.Network.ReconnectEnabled)));
            Network.ReconnectInitialInterval((uint16_t)(net["Reconnect Initial Interval"] | (net["Online Checking Timeout"] | Defaults.Network.ReconnectInitialInterval)));
            Network.ReconnectMaximumInterval((uint16_t)(net["Reconnect Maximum Interval"] | Defaults.Network.ReconnectMaximumInterval));
            Network.FallbackAPEnabled((bool)(net["Fallback AP Enabled"] | Defaults.Network.FallbackAPEnabled));
            Network.FallbackAPSSID(String(net["Fallback AP SSID"] | Defaults.Network.FallbackAPSSID));
            Network.FallbackAPPassword(String(net["Fallback AP Password"] | Defaults.Network.FallbackAPPassword));
            Network.FallbackAPRetention((uint16_t)(net["Fallback AP Retention"] | Defaults.Network.FallbackAPRetention));
        }

        // Update
        if (root["Update"].is<JsonObjectConst>()) {
            JsonObjectConst up = root["Update"].as<JsonObjectConst>();
            Update.ManifestURL(String(up["Manifest URL"] | Defaults.Update.ManifestURL));
            Update.AllowInsecure((bool)(up["Allow Insecure"] | Defaults.Update.AllowInsecure));
            Update.EnableLANOTA((bool)(up["Enable LAN OTA"] | Defaults.Update.EnableLANOTA));
            Update.PasswordLANOTA(String(up["Password LAN OTA"] | Defaults.Update.PasswordLANOTA));
            Update.CheckInterval((uint16_t)(up["Check Interval"] | Defaults.Update.CheckInterval));
            Update.AutoReboot((bool)(up["Auto Reboot"] | Defaults.Update.AutoReboot));
            Update.Debug((bool)(up["Debug"] | Defaults.Update.Debug));
            Update.CheckAtStartup((bool)(up["Check At Startup"] | Defaults.Update.CheckAtStartup));
        }

        // General
        if (root["General"].is<JsonObjectConst>()) {
            JsonObjectConst gen = root["General"].as<JsonObjectConst>();
            General.NTPUpdate((bool)(gen["NTP Update"] | Defaults.General.NTPUpdate));
            General.NTPServer(String(gen["NTP Server"] | Defaults.General.NTPServer));
            General.TimeZone((int)(gen["Time Zone"] | Defaults.General.TimeZone));
            General.SaveStatePooling((uint32_t)(gen["Save State Pooling"] | Defaults.General.SaveStatePooling));
        }

        // Orchestrator
        if (root["Orchestrator"].is<JsonObjectConst>()) {
            JsonObjectConst orch = root["Orchestrator"].as<JsonObjectConst>();
            Orchestrator.Assigned((bool)(orch["Assigned"] | Defaults.Orchestrator.Assigned));
            Orchestrator.ServerID(String(orch["Server ID"] | Defaults.Orchestrator.ServerID));
            Orchestrator.IP_Address(String(orch["IP Address"] | Defaults.Orchestrator.IP_Address));
            Orchestrator.Port((uint16_t)(orch["Port"] | Defaults.Orchestrator.Port));
        }

        // Web Server
        if (root["Web Server"].is<JsonObjectConst>()) {
            JsonObjectConst wh = root["Web Server"].as<JsonObjectConst>();
            WebServer.Port((uint16_t)(wh["Port"] | Defaults.WebServer.Port));
            WebServer.Enabled((bool)(wh["Enabled"] | Defaults.WebServer.Enabled));
            WebServer.WebHooksToken(String(wh["Token"] | Defaults.WebServer.WebHooksToken));
            WebServer.IdleTimeoutMs((uint32_t)(wh["Idle Timeout"] | Defaults.WebServer.IdleTimeoutMs));
            WebServer.MaxSessions((uint8_t)(wh["Max Sessions"] | Defaults.WebServer.MaxSessions));

            if (WebServer.WebHooksToken().isEmpty()) WebServer.Enabled(false); // Token must be >= 1 char
        }

        // MQTT
        if (root["MQTT"].is<JsonObjectConst>()) {
            JsonObjectConst mq = root["MQTT"].as<JsonObjectConst>();
            MQTT.Enabled((bool)(mq["Enabled"] | Defaults.MQTT.Enabled));
            MQTT.Broker(String(mq["Broker"] | Defaults.MQTT.Broker));
            MQTT.Port((uint16_t)(mq["Port"] | Defaults.MQTT.Port));
            MQTT.User(String(mq["User"] | Defaults.MQTT.User));
            MQTT.Password(String(mq["Password"] | Defaults.MQTT.Password));
            MQTT.DiscoveryEnabled((bool)(mq["Discovery Enabled"] | Defaults.MQTT.DiscoveryEnabled));
            MQTT.DiscoveryPrefix(String(mq["Discovery Prefix"] | Defaults.MQTT.DiscoveryPrefix));
        }

        // Telnet
        if (root["Telnet"].is<JsonObjectConst>()) {
            JsonObjectConst tn = root["Telnet"].as<JsonObjectConst>();
            TelnetServer.Enabled((bool)(tn["Enabled"] | Defaults.TelnetServer.Enabled));
            TelnetServer.Port((uint16_t)(tn["Port"] | Defaults.TelnetServer.Port));
            TelnetServer.IdleTimeoutMs((uint32_t)(tn["Idle Timeout"] | Defaults.TelnetServer.IdleTimeoutMs));
            TelnetServer.MaxSessions((uint8_t)(tn["Max Sessions"] | Defaults.TelnetServer.MaxSessions));
        }

        pFirstRun = false;
    }

    // Users
    if (doc["Users"].is<JsonArrayConst>()) {
        for (JsonObjectConst item : doc["Users"].as<JsonArrayConst>()) {
            String username = item["Username"] | "";
            bool admin = item["Admin"] | false;

            uint8_t salt[PASS_SALTLEN] = {0};
            uint8_t hash[PASS_HASHLEN] = {0};

            if (item["Salt"].is<JsonArrayConst>()) {
                size_t i = 0;
                for (JsonVariantConst v : item["Salt"].as<JsonArrayConst>()) {
                    if (i >= PASS_SALTLEN) break;
                    salt[i++] = v.as<uint8_t>();
                }
            }

            if (item["Hash"].is<JsonArrayConst>()) {
                size_t i = 0;
                for (JsonVariantConst v : item["Hash"].as<JsonArrayConst>()) {
                    if (i >= PASS_HASHLEN) break;
                    hash[i++] = v.as<uint8_t>();
                }
            }
        }
    }

    if (Users.Count() == 0) {
        Users.Add(Defaults.Users.Admin.Username, Defaults.Users.Admin.Password, true);
        Users.Add(Defaults.Users.User.Username, Defaults.Users.User.Password, false);
        Save();
    }

    return true;
}

void settings::RestoreToFactoryDefaults() {
    FileSystem.Remove(Defaults.ConfigFileName);

    esp_sleep_enable_timer_wakeup(200 * 1000);
    esp_deep_sleep_start();
}

bool settings::Save(const String& configfilename) const noexcept {
    const String path = configfilename.length() ? configfilename : String(Defaults.ConfigFileName);

    JsonDocument existingDoc;
    String existingContent;
    if (FileSystem.Read(path.c_str(), existingContent) == filesystem::Result::Ok &&
        !existingContent.isEmpty()) {
        (void)deserializeJson(existingDoc, existingContent);
    }

    JsonDocument doc;

    {
        Lock lock(pMutex);
        if (!lock.IsLocked()) return false;

        // Log
        {
            JsonObject log = doc["Log"].to<JsonObject>();
            log["Endpoint"] = Log.Endpoint();
            log["Level"] = Log.LogLevel();
            log["Syslog Server"] = Log.SyslogServerHost();
            log["Syslog Port"] = Log.SyslogServerPort();
        }

        // Network
        {
            JsonObject net = doc["Network"].to<JsonObject>();
            net["DHCP Client"] = Network.DHCPClient();
            net["Hostname"] = Network.Hostname();
            net["IP Address"] = Network.IP_Address().toString();
            net["Gateway"] = Network.Gateway().toString();
            net["Netmask"] = Network.Netmask().toString();

            JsonArray dns = net["DNS Servers"].to<JsonArray>();
            dns.add(Network.DNS(0).toString());
            dns.add(Network.DNS(1).toString());

            net["SSID"] = Network.SSID();
            net["Passphrase"] = Network.Passphrase();
            net["Connection Timeout"] = Network.ConnectionTimeout();
            net["Reconnect Enabled"] = Network.ReconnectEnabled();
            net["Reconnect Initial Interval"] = Network.ReconnectInitialInterval();
            net["Reconnect Maximum Interval"] = Network.ReconnectMaximumInterval();
            net["Fallback AP Enabled"] = Network.FallbackAPEnabled();
            net["Fallback AP SSID"] = Network.FallbackAPSSID();
            net["Fallback AP Password"] = Network.FallbackAPPassword();
            net["Fallback AP Retention"] = Network.FallbackAPRetention();
        }

        // Update
        {
            JsonObject up = doc["Update"].to<JsonObject>();
            up["Manifest URL"] = Update.ManifestURL();
            up["Allow Insecure"] = Update.AllowInsecure();
            up["Enable LAN OTA"] = Update.EnableLANOTA();
            up["Password LAN OTA"] = Update.PasswordLANOTA();
            up["Check Interval"] = Update.CheckInterval();
            up["Auto Reboot"] = Update.AutoReboot();
            up["Debug"] = Update.Debug();
            up["Check At Startup"] = Update.CheckAtStartup();
        }

        // General
        {
            JsonObject gen = doc["General"].to<JsonObject>();
            gen["NTP Update"] = General.NTPUpdate();
            gen["NTP Server"] = General.NTPServer();
            gen["Time Zone"] = General.TimeZone();
            gen["Save State Pooling"] = General.SaveStatePooling();
        }

        // Orchestrator
        {
            JsonObject orch = doc["Orchestrator"].to<JsonObject>();
            orch["Assigned"] = Orchestrator.Assigned();
            orch["Server ID"] = Orchestrator.ServerID();
            orch["IP Address"] = Orchestrator.IP_Address().toString();
            orch["Port"] = Orchestrator.Port();
        }

        // Web Server
        {
            JsonObject wh = doc["Web Server"].to<JsonObject>();
            wh["Port"] = WebServer.Port();
            wh["Enabled"] = WebServer.Enabled();
            wh["Token"] = WebServer.WebHooksToken();
            wh["Idle Timeout"] = WebServer.IdleTimeoutMs();
            wh["Max Sessions"] = WebServer.MaxSessions();
        }

        // MQTT
        {
            JsonObject mq = doc["MQTT"].to<JsonObject>();
            mq["Enabled"] = MQTT.Enabled();
            mq["Broker"] = MQTT.Broker();
            mq["Port"] = MQTT.Port();
            mq["User"] = MQTT.User();
            mq["Password"] = MQTT.Password();
            mq["Discovery Enabled"] = MQTT.DiscoveryEnabled();
            mq["Discovery Prefix"] = MQTT.DiscoveryPrefix();
        }

        // Telnet
        {
            JsonObject tn = doc["Telnet"].to<JsonObject>();
            tn["Enabled"] = TelnetServer.Enabled();
            tn["Port"] = TelnetServer.Port();
            tn["Idle Timeout"] = TelnetServer.IdleTimeoutMs();
            tn["Max Sessions"] = TelnetServer.MaxSessions();
        }

    }

    // Preserve the configuration catalog as-is. Live component runtime state
    // (Relay.State, Blinds.Position) is persisted separately to state.json by
    // SaveComponentsState(), not merged back into this file, so this never
    // undoes add/remove/rename changes that are waiting for a reboot.
    {
        constexpr uint8_t ComponentSchemaVersion = 1;
        doc["ComponentSchemaVersion"] = ComponentSchemaVersion;
        JsonObject components = doc["Components"].to<JsonObject>();
        const JsonObjectConst existingComponents = existingDoc["Components"].as<JsonObjectConst>();

        if ((existingDoc["ComponentSchemaVersion"] | 0) == ComponentSchemaVersion && !existingComponents.isNull()) {
            components.set(existingComponents);
        } else {
            for (size_t index = 0; index < ComponentController.Count(); ++index) {
                const component* source = ComponentController.At(index);
                if (source == nullptr) continue;

                JsonObject item = components[String(source->ID())].to<JsonObject>();
                JsonObject setup = item["Setup"].to<JsonObject>();
                setup["Name"] = source->Name();
                setup["Class"] = component::ClassName(source->Class());
                setup["Bus"] = component::BusName(source->Bus());
                setup["Address"] = source->Address();

                JsonObject properties = item["Properties"].to<JsonObject>();
                properties["Enabled"] = source->Enabled();
                item["Events"].to<JsonObject>();

                if (source->Class() == component::Classes::Relay) {
                    const relay& relayComponent = static_cast<const relay&>(*source);
                    setup["Type"] = relayComponent.Type() == relay::RelayTypes::NormallyOpen ? "NormallyOpen" : "NormallyClosed";
                    setup["DriveMode"] = relayComponent.DriveMode() == relay::DriveModes::ActiveHigh ? "ActiveHigh" : "ActiveLow";
                    properties["State"] = source->IsPublic() ? relayComponent.State() : false;
                } else if (source->Class() == component::Classes::Button) {
                    const button& buttonComponent = static_cast<const button&>(*source);
                    setup["ActiveLevel"] = buttonComponent.ActiveLevel() == button::ActiveLevels::High ? "High" : "Low";
                    setup["InputMode"] = buttonComponent.InputMode() == button::InputModes::PullUp ? "PullUp" : buttonComponent.InputMode() == button::InputModes::PullDown ? "PullDown" : "Floating";
                    setup["DebounceTimeMs"] = buttonComponent.DebounceTime();
                    setup["LongClickTimeMs"] = buttonComponent.LongClickTime();
                    setup["MultiClickTimeMs"] = buttonComponent.MultiClickTime();
                } else if (source->Class() == component::Classes::Thermometer) {
                    const thermometer& thermometerComponent = static_cast<const thermometer&>(*source);
                    setup["Type"] = thermometer::TypeName(thermometerComponent.Type());
                    setup["PollingIntervalMs"] = thermometerComponent.PollingInterval();
                } else if (source->Class() == component::Classes::Blinds) {
                    const blinds& blindsComponent = static_cast<const blinds&>(*source);
                    setup.remove("Address");
                    setup["RelayUp"] = blindsComponent.RelayUp().ID();
                    setup["RelayDown"] = blindsComponent.RelayDown().ID();
                    if (blindsComponent.ButtonUp() != nullptr) setup["ButtonUp"] = blindsComponent.ButtonUp()->ID();
                    if (blindsComponent.ButtonDown() != nullptr) setup["ButtonDown"] = blindsComponent.ButtonDown()->ID();
                    setup["OpenStepTimeMs"] = blindsComponent.OpenStepTime();
                    setup["CloseStepTimeMs"] = blindsComponent.CloseStepTime();
                    setup["OpenCorrectionFactor"] = blindsComponent.OpenCorrectionFactor();
                    setup["CloseCorrectionFactor"] = blindsComponent.CloseCorrectionFactor();
                    setup["EndstopMarginMs"] = blindsComponent.EndstopMargin();
                    setup["ReversalDelayMs"] = blindsComponent.ReversalDelay();
                    properties["Position"] = blindsComponent.Position();
                }
            }
        }
    }

    // Components
    // {
    //     JsonArray components = doc["Components"].to<JsonArray>();

    //     for (auto* m : Settings.Components) {
    //         if (m == nullptr) continue;

    //         JsonObject item = components.add<JsonObject>();

    //         // Preserva campos existentes do arquivo, se houver
    //         if (existingDoc["Components"].is<JsonArrayConst>()) {
    //             for (JsonObjectConst existingItem : existingDoc["Components"].as<JsonArrayConst>()) {
    //                 String existingName = existingItem["Name"] | "";
    //                 if (existingName.equalsIgnoreCase(m->Name())) {
    //                     item.set(existingItem);
    //                     break;
    //                 }
    //             }
    //         }

    //         // Campos comuns
    //         item["Name"] = m->Name();
    //         item["Class"] = EnumToString(AvailableComponentClasses, m->Class());
    //         item["Bus"] = EnumToString(AvailableComponentBuses, m->Bus());
    //         item["Enabled"] = m->Enabled();

    //         if (!item["Events"].is<JsonObject>()) {
    //             item["Events"] = JsonObject();
    //         }

    //         switch (m->Class()) {
    //             case CLASS_BUTTON: {
    //                 item["Address"] = m->Address();
    //                 item["Report"] =
    //                     (m->as<Button>()->ReportMode() == ButtonReportModes::BUTTONREPORTMODE_EDGESONLY)
    //                     ? "EdgesOnly"
    //                     : "ClicksOnly";
    //             } break;

    //             case CLASS_RELAY: {
    //                 item["Address"] = m->Address();
    //                 item["State"] = m->as<Relay>()->State();
    //             } break;

    //             case CLASS_CURRENTMETER:
    //             case CLASS_DOORBELL:
    //             case CLASS_PIR:
    //             case CLASS_CONTACTSENSOR:
    //             case CLASS_THERMOMETER: {
    //                 item["Address"] = m->Address();
    //             } break;

    //             case CLASS_BLINDS: {
    //                 auto* b = m->as<Blinds>();

    //                 item.remove("Address");
    //                 item["Relay Up"] = (b->RelayUp() != nullptr) ? b->RelayUp()->Name() : "";
    //                 item["Relay Down"] = (b->RelayDown() != nullptr) ? b->RelayDown()->Name() : "";
    //                 item["Position"] = b->Position();
    //                 item["Step Ms"] = b->StepMs();
    //                 item["Open Acceleration"] = b->OpenAccel();
    //                 item["Close Acceleration"] = b->CloseAccel();
    //                 item["Calibration Multiplier"] = b->CalibrationMultiplier();
    //             } break;

    //             default: {
    //                 item["Address"] = m->Address();
    //             } break;
    //         }
    //     }
    // }

    // Users
    {
        JsonArray users = doc["Users"].to<JsonArray>();

        const UserReturn result = Users.ForEachStored(
            [&](const String& username,
                bool admin,
                const uint8_t (&salt)[PASS_SALTLEN],
                const uint8_t (&hash)[PASS_HASHLEN]) {
                JsonObject item = users.add<JsonObject>();

                item["Username"] = username;
                item["Admin"] = admin;

                JsonArray jsonSalt = item["Salt"].to<JsonArray>();
                for (uint8_t value : salt) jsonSalt.add(value);

                JsonArray jsonHash = item["Hash"].to<JsonArray>();
                for (uint8_t value : hash) jsonHash.add(value);
            }
        );

        if (result != UserReturn::NoError) return false;
    }

    String serialized;
    const size_t written = serializeJsonPretty(doc, serialized);
    if (written == 0) return false;
    CompactCredentialArrays(serialized);

    return FileSystem.WriteAtomic(path.c_str(), serialized) == filesystem::Result::Ok;
}
