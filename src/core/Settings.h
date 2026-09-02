#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <utility>
#include <IPAddress.h>

#include "Defaults.h"
#include "Users.h"

class settings {
    private:
        class Lock {
            public:
                explicit Lock(SemaphoreHandle_t mutex) noexcept : pMutex(mutex), pLocked(mutex != nullptr && xSemaphoreTakeRecursive(mutex, portMAX_DELAY) == pdTRUE) {}
                ~Lock() { if (pLocked) xSemaphoreGiveRecursive(pMutex); }
                Lock(const Lock&) = delete;
                Lock& operator=(const Lock&) = delete;
                [[nodiscard]] bool IsLocked() const noexcept { return pLocked; }
            private:
                SemaphoreHandle_t pMutex;
                bool pLocked;
        };

        StaticSemaphore_t pMutexStorage{};
        SemaphoreHandle_t pMutex = nullptr;
        bool pFirstRun = false;
        bool pSaveComponentsStateFlag = false;
        static void sanitizeIpString(String& s) noexcept;
    public:
        class log {
            private:
                SemaphoreHandle_t pMutex;
                uint8_t pEndpoint{};
                uint8_t pLogLevel{};
                String pSyslogServerHost;
                uint16_t pSyslogServerPort{};
            public:
                explicit log(SemaphoreHandle_t mutex) noexcept : pMutex(mutex) {}
                [[nodiscard]] uint8_t Endpoint() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pEndpoint : 0; }
                void Endpoint(uint8_t value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pEndpoint = value; }

                [[nodiscard]] uint8_t LogLevel() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pLogLevel : 0; }
                void LogLevel(uint8_t value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pLogLevel = value; }

                [[nodiscard]] String SyslogServerHost() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pSyslogServerHost : String(); }
                void SyslogServerHost(String value) noexcept { value.trim(); value.toLowerCase(); Lock lock(pMutex); if (lock.IsLocked()) pSyslogServerHost = std::move(value); }

                [[nodiscard]] uint16_t SyslogServerPort() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pSyslogServerPort : 0; }
                void SyslogServerPort(uint16_t value) { Lock lock(pMutex); if (lock.IsLocked()) pSyslogServerPort = (value == 0) ? Defaults.Log.SyslogPort : value; }
        } Log;
        class network {
            private:
                SemaphoreHandle_t pMutex;
                bool pDHCPClient{};
                String pHostname;
                IPAddress pIP_Address{0,0,0,0};
                IPAddress pGateway{0,0,0,0};
                IPAddress pNetmask{255,255,255,0};
                IPAddress pDNS[2]{ IPAddress(0,0,0,0), IPAddress(0,0,0,0) };
                String pSSID;
                String pPassphrase;
                uint16_t pConnectionTimeout{};
                bool pReconnectEnabled{};
                uint16_t pReconnectInitialInterval{};
                uint16_t pReconnectMaximumInterval{};
                bool pFallbackAPEnabled{};
                String pFallbackAPSSID;
                String pFallbackAPPassword;
                uint16_t pFallbackAPRetention{};

                static bool isValidNetmask(const IPAddress& mask) noexcept;
                static void stripControlChars(String& s) noexcept;
                static bool isPrintableASCII(const String& s) noexcept;
                static bool isHex64(const String& s) noexcept;
            public:
                explicit network(SemaphoreHandle_t mutex) noexcept : pMutex(mutex) {}
                [[nodiscard]] bool DHCPClient() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pDHCPClient : false; }
                void DHCPClient(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pDHCPClient = value; }

                [[nodiscard]] String Hostname() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pHostname : String(); }
                void Hostname(String value) noexcept;

                [[nodiscard]] IPAddress IP_Address() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pIP_Address : IPAddress(0,0,0,0); }
                void IP_Address(String value) noexcept;

                [[nodiscard]] IPAddress Gateway() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pGateway : IPAddress(0,0,0,0); }
                void Gateway(String value) noexcept;

                [[nodiscard]] IPAddress Netmask() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pNetmask : IPAddress(0,0,0,0); }
                void Netmask(String value) noexcept;

                [[nodiscard]] IPAddress DNS(uint8_t index) const noexcept { Lock lock(pMutex); return (lock.IsLocked() && index < 2) ? pDNS[index] : IPAddress(0,0,0,0); }
                void DNS(uint8_t index, String value) noexcept;

                [[nodiscard]] String SSID() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pSSID : String(); }
                void SSID(String value) noexcept;

                [[nodiscard]] String Passphrase() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pPassphrase : String(); }
                void Passphrase(String value) noexcept;

                [[nodiscard]] uint16_t ConnectionTimeout() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pConnectionTimeout : 0; }
                void ConnectionTimeout(uint16_t value) { Lock lock(pMutex); if (lock.IsLocked()) pConnectionTimeout = (value == 0) ? Defaults.Network.ConnectionTimeout : value; }

                [[nodiscard]] bool ReconnectEnabled() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pReconnectEnabled : false; }
                void ReconnectEnabled(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pReconnectEnabled = value; }

                [[nodiscard]] uint16_t ReconnectInitialInterval() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pReconnectInitialInterval : 0; }
                void ReconnectInitialInterval(uint16_t value) { Lock lock(pMutex); if (lock.IsLocked()) pReconnectInitialInterval = (value == 0) ? Defaults.Network.ReconnectInitialInterval : value; }

                [[nodiscard]] uint16_t ReconnectMaximumInterval() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pReconnectMaximumInterval : 0; }
                void ReconnectMaximumInterval(uint16_t value) { Lock lock(pMutex); if (lock.IsLocked()) pReconnectMaximumInterval = (value == 0) ? Defaults.Network.ReconnectMaximumInterval : value; }

                [[nodiscard]] bool FallbackAPEnabled() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pFallbackAPEnabled : false; }
                void FallbackAPEnabled(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pFallbackAPEnabled = value; }

                [[nodiscard]] String FallbackAPSSID() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pFallbackAPSSID : String(); }
                void FallbackAPSSID(String value) noexcept;

                [[nodiscard]] String FallbackAPPassword() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pFallbackAPPassword : String(); }
                void FallbackAPPassword(String value) noexcept;

                [[nodiscard]] uint16_t FallbackAPRetention() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pFallbackAPRetention : 0; }
                void FallbackAPRetention(uint16_t value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pFallbackAPRetention = value; }
        } Network;
        class update {
            private:
                SemaphoreHandle_t pMutex;
                String pManifestURL;
                bool pAllowInsecure{};
                bool pEnableLANOTA{};
                String pPasswordLANOTA;
                uint16_t pCheckInterval{};
                bool pAutoReboot{};
                bool pDebug{};
                bool pCheckAtStartup{};
            public:
                explicit update(SemaphoreHandle_t mutex) noexcept : pMutex(mutex) {}
                [[nodiscard]] String ManifestURL() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pManifestURL : String(); }
                void ManifestURL(String value) noexcept;

                [[nodiscard]] bool AllowInsecure() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pAllowInsecure : false; }
                void AllowInsecure(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pAllowInsecure = value; }

                [[nodiscard]] bool EnableLANOTA() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pEnableLANOTA : false; }
                void EnableLANOTA(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pEnableLANOTA = value; }

                [[nodiscard]] String PasswordLANOTA() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pPasswordLANOTA : String(); }
                void PasswordLANOTA(String value) noexcept;

                [[nodiscard]] uint16_t CheckInterval() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pCheckInterval : 0; }
                void CheckInterval(uint16_t value) { Lock lock(pMutex); if (lock.IsLocked()) pCheckInterval = value; }

                [[nodiscard]] bool AutoReboot() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pAutoReboot : false; }
                void AutoReboot(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pAutoReboot = value; }

                [[nodiscard]] bool Debug() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pDebug : false; }
                void Debug(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pDebug = value; }

                [[nodiscard]] bool CheckAtStartup() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pCheckAtStartup : false; }
                void CheckAtStartup(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pCheckAtStartup = value; }
        } Update;
        class general {
            private:
                SemaphoreHandle_t pMutex;
                bool pNTPUpdate{};
                String pNTPServer;
                int8_t pTimeZone{};
                uint16_t pSaveStatePooling{};
            public:
                explicit general(SemaphoreHandle_t mutex) noexcept : pMutex(mutex) {}
                [[nodiscard]] bool NTPUpdate() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pNTPUpdate : false; }
                void NTPUpdate(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pNTPUpdate = value; }

                [[nodiscard]] String NTPServer() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pNTPServer : String(); }
                void NTPServer(String value) noexcept;

                [[nodiscard]] int8_t TimeZone() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pTimeZone : 0; }
                void TimeZone(int value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pTimeZone = static_cast<int8_t>(constrain(value, -12, 14)); }

                [[nodiscard]] uint16_t SaveStatePooling() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pSaveStatePooling : 0; }
                void SaveStatePooling(uint16_t value) { Lock lock(pMutex); if (lock.IsLocked()) pSaveStatePooling = (value <= 1) ? Defaults.General.SaveStatePooling : value; }
        } General;
        class orchestrator {
            private:
                SemaphoreHandle_t pMutex;
                bool pAssigned{};
                String pServerID;
                IPAddress pIP_Address{0,0,0,0};
                uint16_t pPort{};
            public:
                explicit orchestrator(SemaphoreHandle_t mutex) noexcept : pMutex(mutex) {}
                [[nodiscard]] bool Assigned() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pAssigned : false; }
                void Assigned(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pAssigned = value; }

                [[nodiscard]] String ServerID() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pServerID : String(); }
                void ServerID(String value) noexcept;

                [[nodiscard]] IPAddress IP_Address() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pIP_Address : IPAddress(0,0,0,0); }
                void IP_Address(String value) noexcept;

                [[nodiscard]] uint16_t Port() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pPort : 0; }
                void Port(uint16_t value) { Lock lock(pMutex); if (lock.IsLocked()) pPort = (value == 0) ? Defaults.Orchestrator.Port : value; }
        } Orchestrator;
        class webserver {
            private:
                SemaphoreHandle_t pMutex;
                uint16_t pPort{};
                bool pEnabled{};
                uint32_t pIdleTimeoutMs{};
                uint8_t pMaxSessions{};
            public:
                explicit webserver(SemaphoreHandle_t mutex) noexcept : pMutex(mutex) {}
                [[nodiscard]] uint16_t Port() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pPort : 0; }
                void Port(uint16_t value) { Lock lock(pMutex); if (lock.IsLocked()) pPort = (value == 0) ? Defaults.WebServer.Port : value; }

                [[nodiscard]] bool Enabled() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pEnabled : false; }
                void Enabled(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pEnabled = value; }

                // 0 means the idle timeout is disabled; kept as given.
                [[nodiscard]] uint32_t IdleTimeoutMs() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pIdleTimeoutMs : 0; }
                void IdleTimeoutMs(uint32_t value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pIdleTimeoutMs = value; }

                [[nodiscard]] uint8_t MaxSessions() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pMaxSessions : 0; }
                void MaxSessions(uint8_t value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pMaxSessions = (value == 0) ? Defaults.WebServer.MaxSessions : value; }
        } WebServer;
        class webhooks {
            private:
                SemaphoreHandle_t pMutex;
                bool pEnabled{};
                String pToken;
                uint16_t pPort{};
            public:
                explicit webhooks(SemaphoreHandle_t mutex) noexcept : pMutex(mutex) {}
                [[nodiscard]] bool Enabled() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pEnabled : false; }
                void Enabled(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pEnabled = value; }

                // Letters/numbers only, 15-30 characters; set to empty on
                // any invalid value (too short/long or non-alphanumeric).
                [[nodiscard]] String Token() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pToken : String(); }
                void Token(String value) noexcept;

                [[nodiscard]] uint16_t Port() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pPort : 0; }
                void Port(uint16_t value) { Lock lock(pMutex); if (lock.IsLocked()) pPort = (value == 0) ? Defaults.Webhooks.Port : value; }
        } Webhooks;
        class telnetserver {
            private:
                SemaphoreHandle_t pMutex;
                uint16_t pPort{};
                bool pEnabled{};
                uint32_t pIdleTimeoutMs{};
                uint8_t pMaxSessions{};
            public:
                explicit telnetserver(SemaphoreHandle_t mutex) noexcept : pMutex(mutex) {}
                [[nodiscard]] uint16_t Port() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pPort : 0; }
                void Port(uint16_t value) { Lock lock(pMutex); if (lock.IsLocked()) pPort = (value == 0) ? Defaults.TelnetServer.Port : value; }

                [[nodiscard]] bool Enabled() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pEnabled : false; }
                void Enabled(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pEnabled = value; }

                // 0 means the idle timeout is disabled; kept as given.
                [[nodiscard]] uint32_t IdleTimeoutMs() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pIdleTimeoutMs : 0; }
                void IdleTimeoutMs(uint32_t value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pIdleTimeoutMs = value; }

                [[nodiscard]] uint8_t MaxSessions() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pMaxSessions : 0; }
                void MaxSessions(uint8_t value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pMaxSessions = (value == 0) ? Defaults.TelnetServer.MaxSessions : value; }
        } TelnetServer;
        class mqtt {
            private:
                SemaphoreHandle_t pMutex;
                bool pEnabled{};
                String pBroker;
                uint16_t pPort{};
                String pUser;
                String pPassword;
                bool pDiscoveryEnabled{};
                String pDiscoveryPrefix;
            public:
                explicit mqtt(SemaphoreHandle_t mutex) noexcept : pMutex(mutex) {}
                [[nodiscard]] bool Enabled() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pEnabled : false; }
                void Enabled(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pEnabled = value; }

                [[nodiscard]] String Broker() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pBroker : String(); }
                void Broker(String value) noexcept;

                [[nodiscard]] uint16_t Port() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pPort : 0; }
                void Port(uint16_t value) { Lock lock(pMutex); if (lock.IsLocked()) pPort = (value == 0) ? Defaults.MQTT.Port : value; }

                [[nodiscard]] String User() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pUser : String(); }
                void User(String value) noexcept;

                [[nodiscard]] String Password() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pPassword : String(); }
                void Password(String value) noexcept;

                [[nodiscard]] bool DiscoveryEnabled() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pDiscoveryEnabled : false; }
                void DiscoveryEnabled(bool value) noexcept { Lock lock(pMutex); if (lock.IsLocked()) pDiscoveryEnabled = value; }
                [[nodiscard]] String DiscoveryPrefix() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pDiscoveryPrefix : String(); }
                void DiscoveryPrefix(String value) noexcept;
        } MQTT;

        settings() noexcept;
        settings(const settings&) = delete;
        settings& operator=(const settings&) = delete;

        [[nodiscard]] bool FirstRun() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pFirstRun : false; }
        [[nodiscard]] bool SaveComponentsStateFlag() const noexcept { Lock lock(pMutex); return lock.IsLocked() ? pSaveComponentsStateFlag : false; }
        void SetSaveComponentsState() noexcept { Lock lock(pMutex); if (lock.IsLocked()) pSaveComponentsStateFlag = true; }

        // Collection Components;
        users Users;

        void LoadDefaults();
        void RestoreToFactoryDefaults();
        bool Load(const String& configfilename = Defaults.ConfigFileName) noexcept;
        bool Save(const String& configfilename = Defaults.ConfigFileName) const noexcept;
        bool InstallComponents(const String& configfilename = Defaults.ConfigFileName) noexcept;
        bool SaveComponentsState(const String& statefilename = Defaults.StateFileName) noexcept;
        bool ExecuteComponentCommand(String* parameters, String& output) noexcept;
        // Reads config.json fresh and parses it into `document` for
        // read-only inspection (e.g. populating a web form) - document
        // ["Components"] is the same raw object ExecuteComponentCommand
        // itself mutates. Returns false if the file is missing, invalid,
        // or the schema version doesn't match.
        bool ReadComponentsCatalog(JsonDocument& document) noexcept;
};

extern settings Settings;
