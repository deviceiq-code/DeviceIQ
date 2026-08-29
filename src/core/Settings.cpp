#include "Settings.h"
#include "Globals.h"
#include "components/Blinds.h"
#include "components/Button.h"
#include "components/Thermometer.h"

#include <ArduinoJson.h>
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

    // TelnetServer
    TelnetServer.Port(Defaults.TelnetServer.Port);
    TelnetServer.Enabled(Defaults.TelnetServer.Enabled);

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

// bool settings::SaveComponentsState(const String& configfilename) noexcept {
//     const String path = configfilename.length() ? configfilename : String(Defaults.ConfigFileName);
//     String content;
//     if (FileSystem.Read(path.c_str(), content) != filesystem::Result::Ok || content.isEmpty()) return false;

//     JsonDocument doc;
//     const DeserializationError err = deserializeJson(doc, content);
//     if (err) return false;

//     JsonObject root = doc.as<JsonObject>();
//     if (root.isNull()) return false;

//     JsonArray components = root["Components"].as<JsonArray>();

//     if (components.isNull()) return false; // No components found

//     auto findComponentObj = [&](String name) -> JsonObject {
//         for (JsonObject obj : components) {
//             const char* jName = obj["Name"] | "";
//             if (name.equalsIgnoreCase(jName)) return obj;
//         }
//         return JsonObject();
//     };

//     for (Generic* comp : Components) {
//         JsonObject obj = findComponentObj(comp->Name());
//         if (obj.isNull()) continue;

//         switch (comp->Class()) {
//             case CLASS_RELAY : {
//                 obj["State"] = comp->as<Relay>()->State();
//             } break;
//             case CLASS_BLINDS : {
//                 obj["Position"] = comp->as<Blinds>()->State();
//             } break;
//         }
//     }

//     String serialized;
//     const size_t written = serializeJsonPretty(doc, serialized);
//     if (written == 0 || FileSystem.Write(path.c_str(), serialized) != filesystem::Result::Ok) return false;

//     pSaveComponentsStateFlag = false;
//     return true;
// }

// bool settings::InstallComponents(const String& configfilename) noexcept {
//     const String path = configfilename.length() ? configfilename : String(Defaults.ConfigFileName);
//     File f = devFileSystem->OpenFile(path, "r");
//     if (!f || !f.available()) {
//         if (f) f.close();
//         return false;
//     }

//     JsonDocument doc;
//     DeserializationError err = deserializeJson(doc, f);
//     f.close();
//     if (err) return false;

//     JsonObjectConst root = doc.as<JsonObjectConst>();
//     if (root.isNull()) return false;

//     JsonArrayConst components = root["Components"].as<JsonArrayConst>();
//     if (components.isNull()) {
//         Serial.println(F("No components found in configuration."));
//         return false;
//     }

//     Components.Clear();

//     auto configureComponentEvents = [&](Generic* NewComponent, JsonObjectConst comp) {
//         if (!NewComponent) return;

//         JsonObjectConst EventsInConfig = comp["Events"];
//         if (EventsInConfig.isNull()) return;

//         for (auto& ComponentEvent : NewComponent->Event) {
//             const String& eventName = ComponentEvent.first;

//             JsonVariantConst v = EventsInConfig[eventName.c_str()];
//             if (v.isNull() || !v.is<const char*>()) continue;

//             String actionFull = String(v.as<const char*>());
//             actionFull.trim();

//             int open = actionFull.indexOf('(');
//             int close = actionFull.lastIndexOf(')');

//             String cmd = (open > 0) ? actionFull.substring(0, open) : actionFull;
//             String param = (open >= 0 && close > open) ? actionFull.substring(open + 1, close) : "";
//             cmd.trim();
//             param.trim();

//             param.replace("%NAME%", NewComponent->Name());

//             callback_t previous = NewComponent->GetEventCallback(eventName);
//             callback_t appended = nullptr;

//             if (cmd.equalsIgnoreCase("log")) {
//                 String msg = param;
//                 auto logPtr = devLog;

//                 appended = [msg, logPtr] {
//                     if (logPtr) logPtr->Write(msg, LOGLEVEL_INFO);
//                 };
//             }
//             else if (cmd.equalsIgnoreCase("enable")) {
//                 String targetName = param;
//                 auto* target = Components[targetName];

//                 if (target) {
//                     auto* generic = target->as<Generic>();
//                     appended = [generic] {
//                         generic->Enabled(true);
//                     };
//                 }
//             }
//             else if (cmd.equalsIgnoreCase("disable")) {
//                 String targetName = param;
//                 auto* target = Components[targetName];

//                 if (target) {
//                     auto* generic = target->as<Generic>();
//                     appended = [generic] {
//                         generic->Enabled(false);
//                     };
//                 }
//             }
//             else if (cmd.equalsIgnoreCase("invert")) {
//                 String targetName = param;
//                 auto* target = Components[targetName];

//                 if (target && target->Class() == CLASS_RELAY) {
//                     auto* relay = target->as<Relay>();
//                     appended = [relay] {
//                         relay->Invert();
//                     };
//                 }
//             }
//             else if (cmd.equalsIgnoreCase("seton")) {
//                 String targetName = param;
//                 auto* target = Components[targetName];

//                 if (target && target->Class() == CLASS_RELAY) {
//                     auto* relay = target->as<Relay>();
//                     appended = [relay] {
//                         relay->State(true);
//                     };
//                 }
//             }
//             else if (cmd.equalsIgnoreCase("setoff")) {
//                 String targetName = param;
//                 auto* target = Components[targetName];

//                 if (target && target->Class() == CLASS_RELAY) {
//                     auto* relay = target->as<Relay>();
//                     appended = [relay] {
//                         relay->State(false);
//                     };
//                 }
//             }

//             if (!appended) continue;

//             NewComponent->SetEventCallback(eventName, [previous, appended] {
//                 if (previous) previous();
//                 appended();
//             });
//         }
//     };

//     auto installComponent = [&](JsonObjectConst comp, uint8_t& comp_id, bool installVirtual) {
//         const String comp_name = String(comp["Name"] | "");
//         const String comp_class = String(comp["Class"] | "");
//         const uint8_t comp_address = (uint8_t)(comp["Address"] | 0);
//         const bool comp_enabled = (bool)(comp["Enabled"] | false);
//         const String comp_bus = String(comp["Bus"] | "");

//         if (comp_name.isEmpty()) {
//             if (devLog) devLog->Write("Component: Empty name for component #" + String(comp_id) + " - component not installed", LOGLEVEL_WARNING);
//             return;
//         }

//         if (comp_class.isEmpty()) {
//             if (devLog) devLog->Write("Component: Empty class for component #" + String(comp_id) + " - component not installed", LOGLEVEL_WARNING);
//             return;
//         }

//         if (comp_bus.isEmpty()) {
//             if (devLog) devLog->Write("Component: Empty bus for component #" + String(comp_id) + " - component not installed", LOGLEVEL_WARNING);
//             return;
//         }

//         auto itBus = AvailableComponentBuses.find(comp_bus);
//         if (itBus == AvailableComponentBuses.end()) {
//             if (devLog) devLog->Write("Component: Unknoun bus name '" + comp_bus + "' for component #" + String(comp_id) + " - component not installed", LOGLEVEL_WARNING);
//             return;
//         }

//         auto itClass = AvailableComponentClasses.find(comp_class);
//         if (itClass == AvailableComponentClasses.end()) {
//             if (devLog) devLog->Write("Component: Unknoun class name '" + comp_class + "' for component #" + String(comp_id) + " - component not installed", LOGLEVEL_WARNING);
//             return;
//         }

//         const Classes c = itClass->second;

//         bool isVirtualClass = false;
//         switch (c) {
//             case CLASS_BLINDS: isVirtualClass = true; break;
//             default: isVirtualClass = false; break;
//         }

//         if (installVirtual != isVirtualClass) return;

//         Generic* NewComponent = nullptr;

//         switch (c) {
//             case CLASS_GENERIC: {
//                 // Reserved
//             } break;

//             case CLASS_BLINDS: {
//                 int16_t relayUpIndex = Components.IndexOf(String(comp["Relay Up"] | ""));
//                 int16_t relayDnIndex = Components.IndexOf(String(comp["Relay Down"] | ""));

//                 if (relayUpIndex > -1 && relayDnIndex > -1) {
//                     Generic* upGeneric = Components.At(relayUpIndex);
//                     Generic* dnGeneric = Components.At(relayDnIndex);

//                     if (upGeneric && dnGeneric &&
//                         upGeneric->Class() == CLASS_RELAY &&
//                         dnGeneric->Class() == CLASS_RELAY) {

//                         Relay* relayUp = upGeneric->as<Relay>();
//                         Relay* relayDn = dnGeneric->as<Relay>();

//                         NewComponent = new Blinds(comp_name, comp_id, relayUp, relayDn);

//                         Blinds* tmp_blinds = NewComponent->as<Blinds>();

//                         if (tmp_blinds) {
//                             tmp_blinds->StepMs(comp["Step Ms"] | Defaults.Components.Blinds.StepMs);
//                             tmp_blinds->OpenAccel(comp["Open Acceleration"] | Defaults.Components.Blinds.OpenAccel);
//                             tmp_blinds->CloseAccel(comp["Close Acceleration"] | Defaults.Components.Blinds.CloseAccel);
//                             tmp_blinds->CalibrationMultiplier(comp["Calibration Multiplier"].as<uint8_t>() | Defaults.Components.Blinds.CalibrationMultiplier);
//                             tmp_blinds->Position((comp["Position"].as<uint8_t>() | 0), true);

//                             tmp_blinds->Event["Changed"]([this, tmp_blinds] {
//                             if (devMQTT) {
//                                 devMQTT->Publish(Network.Hostname() + "/Get/Blinds:" + tmp_blinds->Name() + ":Name", tmp_blinds->Name());
//                                 devMQTT->Publish(Network.Hostname() + "/Get/Blinds:" + tmp_blinds->Name() + ":CurrentPosition", String(tmp_blinds->CurrentPosition()));
//                                 devMQTT->Publish(Network.Hostname() + "/Get/Blinds:" + tmp_blinds->Name() + ":TargetPosition", String(tmp_blinds->TargetPosition()));
//                                 devMQTT->Publish(Network.Hostname() + "/Get/Blinds:" + tmp_blinds->Name() + ":PositionState", String(tmp_blinds->PositionState()));
//                             }

//                             pSaveComponentsStateFlag = true;
//                         });
//                         }
//                     } else {
//                         if (devLog) devLog->Write("Component: Blinds '" + comp_name + "' not created: relay up/down are invalid", LOGLEVEL_WARNING);
//                     }
//                 } else {
//                     if (devLog) devLog->Write("Component: Blinds '" + comp_name + "' not created: relay up/down not found", LOGLEVEL_WARNING);
//                 }
//             } break;

//             case CLASS_BUTTON: {
//                 NewComponent = new Button(
//                     comp_name,
//                     comp_id,
//                     itBus->second,
//                     comp_address,
//                     (comp["Report"].as<String>().equalsIgnoreCase("EdgesOnly")
//                         ? ButtonReportModes::BUTTONREPORTMODE_EDGESONLY
//                         : ButtonReportModes::BUTTONREPORTMODE_CLICKSONLY)
//                 );

//                 if (NewComponent) {
//                     auto* n = NewComponent->as<Button>();

//                     if (n->ReportMode() == ButtonReportModes::BUTTONREPORTMODE_CLICKSONLY) {
//                         n->Event["Clicked"]([this, n] {
//                             if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Button:" + n->Name(), "Clicked");
//                         });
//                         n->Event["DoubleClicked"]([this, n] {
//                             if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Button:" + n->Name(), "DoubleClicked");
//                         });
//                         n->Event["TripleClicked"]([this, n] {
//                             if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Button:" + n->Name(), "TripleClicked");
//                         });
//                         n->Event["LongClicked"]([this, n] {
//                             if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Button:" + n->Name(), "LongClicked");
//                         });
//                     } else if (n->ReportMode() == ButtonReportModes::BUTTONREPORTMODE_EDGESONLY) {
//                         n->Event["Pressed"]([this, n] {
//                             if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Button:" + n->Name(), "Pressed");
//                         });
//                         n->Event["Released"]([this, n] {
//                             if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Button:" + n->Name(), "Released");
//                         });
//                     }
//                 }
//             } break;

//             case CLASS_CURRENTMETER: {
//                 NewComponent = new Currentmeter(comp_name, comp_id, itBus->second, comp_address);

//                 if (NewComponent) {
//                     auto* n = NewComponent->as<Currentmeter>();
//                     n->Event["Changed"]([this, n] {
//                         if (devMQTT) {
//                             devMQTT->Publish(Network.Hostname() + "/Get/Currentmeter:" + n->Name() + ":DC", String(n->CurrentDC()));
//                             devMQTT->Publish(Network.Hostname() + "/Get/Currentmeter:" + n->Name() + ":AC", String(n->CurrentAC()));
//                         }
//                     });
//                 }
//             } break;

//             case CLASS_RELAY: {
//                 NewComponent = new Relay(comp_name, comp_id, itBus->second, comp_address, DeviceIQ_Components::RelayTypes::RELAYTYPE_NORMALLYCLOSED);

//                 if (NewComponent) {
//                     auto* n = NewComponent->as<Relay>();
//                     n->State((bool)(comp["State"] | false));

//                     n->Event["Changed"]([this, n] {
//                         if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Relay:" + n->Name() + ":State", n->State() ? "on" : "off");
//                         pSaveComponentsStateFlag = true;
//                     });
//                 }
//             } break;

//             case CLASS_PIR: {
//                 NewComponent = new PIR(comp_name, comp_id, itBus->second, comp_address);

//                 if (NewComponent) {
//                     auto* n = NewComponent->as<PIR>();
//                     n->DebounceTime((uint32_t)(comp["Debounce"] | 200));

//                     n->Event["MotionDetected"]([this, n] {
//                         if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/PIR:" + n->Name(), "M");
//                     });
//                     n->Event["MotionCleared"]([this, n] {
//                         if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/PIR:" + n->Name(), "C");
//                     });
//                 }
//             } break;

//             case CLASS_DOORBELL: {
//                 NewComponent = new Doorbell(comp_name, comp_id, itBus->second, comp_address);

//                 if (NewComponent) {
//                     auto* n = NewComponent->as<Doorbell>();
//                     n->Timeout((uint32_t)(comp["Timeout"] | 1000));

//                     n->Event["Ring"]([this, n] {
//                         if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Doorbell:" + n->Name(), "1");
//                     });
//                     n->Event["DoubleRing"]([this, n] {
//                         if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Doorbell:" + n->Name(), "2");
//                     });
//                     n->Event["LongRing"]([this, n] {
//                         if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Doorbell:" + n->Name(), "L");
//                     });
//                 }
//             } break;

//             case CLASS_CONTACTSENSOR: {
//                 NewComponent = new ContactSensor(comp_name, comp_id, itBus->second, comp_address, ((bool)(comp["InvertClose"] | false)));

//                 if (NewComponent) {
//                     auto* n = NewComponent->as<ContactSensor>();
//                     n->Event["Opened"]([this, n] {
//                         if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/ContactSensor:" + n->Name(), "Opened");
//                     });
//                     n->Event["Closed"]([this, n] {
//                         if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/ContactSensor:" + n->Name(), "Closed");
//                     });
//                 }
//             } break;

//             case CLASS_THERMOMETER: {
//                 auto it = AvailableThermometerTypes.find(String(comp["Type"] | "DS18B20"));
//                 if (it != AvailableThermometerTypes.end()) {
//                     NewComponent = new Thermometer(comp_name, comp_id, itBus->second, comp_address, it->second);
//                 }

//                 if (NewComponent) {
//                     auto* n = NewComponent->as<Thermometer>();
//                     n->Event["TemperatureChanged"]([this, n] {
//                         if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Thermometer:" + n->Name() + ":Temperature", String(n->Temperature()));
//                     });
//                     n->Event["HumidityChanged"]([this, n] {
//                         if (devMQTT) devMQTT->Publish(Network.Hostname() + "/Get/Thermometer:" + n->Name() + ":Humidity", String(n->Humidity()));
//                     });
//                     n->Event["Changed"]([this, n] {
//                         if (devMQTT) {
//                             devMQTT->Publish(Network.Hostname() + "/Get/Thermometer:" + n->Name() + ":Temperature", String(n->Temperature()));
//                             devMQTT->Publish(Network.Hostname() + "/Get/Thermometer:" + n->Name() + ":Humidity", String(n->Humidity()));
//                         }
//                     });
//                 }
//             } break;
//         }

//         if (!NewComponent) return;

//         NewComponent->Enabled(comp_enabled);

//         configureComponentEvents(NewComponent, comp);

//         int16_t dup = Components.IndexOf(comp_name);
//         if (dup >= 0) Components.Remove(dup);

//         Components.Add(NewComponent);

//         if (devLog) {
//             devLog->Write(
//                 "Component: #" + String(comp_id) + " " + comp_class + "\\" + comp_name +
//                 String(NewComponent->IsVirtual() ? " (virtual)" : "") +
//                 " installed",
//                 LOGLEVEL_WARNING
//             );
//         }

//         comp_id++;
//     };

//     uint8_t comp_id = 0;

//     for (JsonObjectConst comp : components) {
//         installComponent(comp, comp_id, false);
//     }

//     for (JsonObjectConst comp : components) {
//         installComponent(comp, comp_id, true);
//     }

//     return true;
// }

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
        }

    }

    // Preserve the configuration catalog and merge only runtime state by ID.
    // This prevents state persistence from undoing add/remove/rename changes
    // that are waiting for a reboot.
    {
        constexpr uint8_t ComponentSchemaVersion = 1;
        doc["ComponentSchemaVersion"] = ComponentSchemaVersion;
        JsonObject components = doc["Components"].to<JsonObject>();
        const JsonObjectConst existingComponents = existingDoc["Components"].as<JsonObjectConst>();

        if ((existingDoc["ComponentSchemaVersion"] | 0) == ComponentSchemaVersion && !existingComponents.isNull()) {
            components.set(existingComponents);

            for (size_t index = 0; index < ComponentController.Count(); ++index) {
                const component* runtimeComponent = ComponentController.At(index);
                if (runtimeComponent == nullptr || !runtimeComponent->IsPublic() || !runtimeComponent->HasPersistentState()) continue;

                JsonObject configured = components[String(runtimeComponent->ID())].as<JsonObject>();
                if (!configured.isNull()) {
                    JsonObject properties = configured["Properties"].to<JsonObject>();
                    if (runtimeComponent->Class() == component::Classes::Relay) {
                        properties["State"] = static_cast<const relay&>(*runtimeComponent).State();
                    } else if (runtimeComponent->Class() == component::Classes::Blinds) {
                        properties["Position"] = static_cast<const blinds&>(*runtimeComponent).Position();
                    }
                }
            }
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

    return FileSystem.Write(path.c_str(), serialized) == filesystem::Result::Ok;
}
