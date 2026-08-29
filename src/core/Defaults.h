#pragma once

#include <Arduino.h>
#include <IPAddress.h>

struct defaults {
    struct users {
        struct admin {
            const char* Username = "admin";
            const char* Password = "admin1234";
        } Admin;
        struct user {
            const char* Username = "user";
            const char* Password = "user1234";
        } User;
    } Users;
    struct log {
        const uint8_t Endpoint = 0b00000101; // Serial + File
        const uint8_t Level = 0b11111111; // All
        const char* SyslogServer = "syslog.svr";
        const uint16_t SyslogPort = 514;
        const uint16_t ShowMaxLines = 20;
        const size_t MaxFileSize = 64 * 1024;
    } Log;
    struct network {
        const bool DHCPClient = true;
        String Hostname() const { uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA); char buf[12]; snprintf(buf, sizeof(buf), "dev-%02X%02X%02X", mac[3], mac[4], mac[5]); return String(buf); }
        const char* IP_Address = "0.0.0.0";
        const char* Gateway = "0.0.0.0";
        const char* Netmask = "255.255.255.0";
        const char* DNS[2] = { "8.8.8.8", "8.8.4.4" };
        const char* SSID = "IOT-2";
        const char* Passphrase = "1921682GenesisIOT-2";
        const uint16_t ConnectionTimeout = 30;
        const bool ReconnectEnabled = true;
        const uint16_t ReconnectInitialInterval = 5;
        const uint16_t ReconnectMaximumInterval = 60;
        const bool FallbackAPEnabled = true;
        const char* FallbackAPSSID = ""; // Empty uses the device hostname.
        const char* FallbackAPPassword = "DeviceIQ-Setup";
        const uint16_t FallbackAPRetention = 300;
    } Network;
    struct update {
        const char* ManifestURL = "https://server.dts-network.com:8081/update-dpk.json";
        const bool AllowInsecure = true;
        const bool EnableLANOTA = false;
        const char* PasswordLANOTA = "";
        const uint16_t CheckInterval = 3600;
        const bool AutoReboot = true;
        const bool Debug = false;
        const bool CheckAtStartup = true;
    } Update;
    struct general {
        const uint16_t SaveStatePooling = 20;
        const bool NTPUpdate = true;
        const char* NTPServer = "pool.ntp.org";
        const int8_t TimeZone = -3;
    } General;
    struct orchestrator {
        const char* Provider = "Orchestrator";
        const bool Assigned = false;
        const char* ServerID = "";
        const char* IP_Address = "";
        const uint16_t Port = 30030;
    } Orchestrator;
    struct webserver {
        const uint16_t Port = 80;
        const bool Enabled = true;
        const char* WebHooksToken = "default_token";
    } WebServer;
    struct telnetserver {
        const uint16_t Port = 23;
        const bool Enabled = true;
    } TelnetServer;
    struct mqtt {
        const bool Enabled = false;
        const char* Broker = "";
        const uint16_t Port = 1883;
        const char* User = "";
        const char* Password = "";
        const bool DiscoveryEnabled = true;
        const char* DiscoveryPrefix = "homeassistant";
    } MQTT;
    struct components {
        const bool Enabled = true;
        struct blinds {
            const uint32_t OpenStepTimeMs = 250;
            const uint32_t CloseStepTimeMs = 250;
            const float OpenCorrectionFactor = 0.0f;
            const float CloseCorrectionFactor = 0.0f;
            const uint32_t EndstopMarginMs = 0;
        } Blinds;
    } Components;
    const char* ConfigFileName = "/config.json";
    const char* LogFileName = "/device.log";
    const uint32_t InitialTimeAndDate = 1708136755;
};

extern const defaults Defaults;
