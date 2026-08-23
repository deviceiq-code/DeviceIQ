#include <Arduino.h>

#include "App.h"
#include "Globals.h"
#include "Users.h"

void App::Start() {
    Clock.SetEpoch(Defaults.InitialTimeAndDate);

    if (!InitializeFileSystem()) return;

    const bool configurationLoaded = Settings.Load();
    Clock.TimeZone(Settings.General.TimeZone());

    if (!InitializeLogger()) return;

    Logger.Log(Version::Info(), logger::LogLevels::Information);
    Logger.Log("Logger initialized", logger::LogLevels::Information);
    Logger.Log("FileSystem initialized", logger::LogLevels::Information);

    if (!InitializeNetwork()) return;
    if (!InitializeClock()) return;
    if (!InitializeComponents()) return;
    if (!InitializeTelnetServer()) return;

    LogConfigurationStatus(configurationLoaded);
}

bool App::InitializeFileSystem() {
    if (!FileSystem.Start(true)) {
        Serial.println("Error initializing FileSystem object");
        return false;
    }

    return true;
}

bool App::InitializeLogger() {
    Logger.LogLevel(Settings.Log.LogLevel());
    Logger.Endpoint(Settings.Log.Endpoint());
    Logger.SyslogServerHost(Settings.Log.SyslogServerHost());
    Logger.SyslogServerPort(Settings.Log.SyslogServerPort());

    if (!Logger.Start()) {
        Serial.println("Error initializing Logger object");
        return false;
    }

    return true;
}

bool App::InitializeNetwork() {
    Network.ConnectionTimeout(Settings.Network.ConnectionTimeout());
    Network.OnlineChecking(Settings.Network.OnlineChecking());
    Network.OnlineCheckingTimeout(Settings.Network.OnlineCheckingTimeout());
    Network.DHCP_Client(Settings.Network.DHCPClient());
    Network.SSID(Settings.Network.SSID());
    const bool stationPasswordValid = Network.Passphrase(Settings.Network.Passphrase());
    Network.Hostname(Settings.Network.Hostname());
    Network.SoftAP_SSID(Settings.Network.Hostname());
    Network.IP_Address(Settings.Network.IP_Address());
    Network.Netmask(Settings.Network.Netmask());
    Network.Gateway(Settings.Network.Gateway());
    Network.DNS_Server(0, Settings.Network.DNS(0));
    Network.DNS_Server(1, Settings.Network.DNS(1));

    if (!stationPasswordValid) {
        Logger.Log("Invalid WiFi passphrase", logger::LogLevels::Error);
    }

    Network.OnModeChanged(LogNetworkStatus);

    if (!Network.Start()) {
        Logger.Log("Error initializing Network object", logger::LogLevels::Error);
        return false;
    }

    Logger.Log("Network initialized", logger::LogLevels::Information);
    return true;
}

bool App::InitializeClock() {
    if (!Settings.General.NTPUpdate()) {
        Logger.Log("Date and time: NTP disabled, using local clock", logger::LogLevels::Information);
        return true;
    }

    if (pClockTaskHandle != nullptr) return true;

    const BaseType_t result = xTaskCreate(ClockTaskEntry, "Clock", CLOCK_TASK_STACK_SIZE, this, CLOCK_TASK_PRIORITY, &pClockTaskHandle);
    if (result != pdPASS) {
        pClockTaskHandle = nullptr;
        Logger.Log("Error starting clock synchronization task", logger::LogLevels::Error);
        return false;
    }

    return true;
}

bool App::InitializeComponents() {
    if (!ComponentController.Start()) {
        Logger.Log("Error initializing component task", logger::LogLevels::Error);
        return false;
    }

    Logger.Log("Component task initialized", logger::LogLevels::Information);
    return true;
}

void App::ClockTaskEntry(void* parameter) {
    static_cast<App*>(parameter)->ClockTask();
}

void App::ClockTask() {
    while (true) {
        if (Network.ConnectionMode() != network::APMode::WifiClient) {
            vTaskDelay(pdMS_TO_TICKS(NTP_OFFLINE_RETRY_MS));
            continue;
        }

        const String server = Settings.General.NTPServer();
        const bool updated = Clock.NTPUpdate(server);

        if (updated) {
            Logger.Log("Date and time: Updated from NTP server " + server, logger::LogLevels::Information);
            vTaskDelay(pdMS_TO_TICKS(NTP_UPDATE_INTERVAL_MS));
        } else {
            Logger.Log("Date and time: Failed to update from NTP server " + server, logger::LogLevels::Error);
            vTaskDelay(pdMS_TO_TICKS(NTP_FAILURE_RETRY_MS));
        }
    }
}

bool App::InitializeTelnetServer() {
    TelnetServer.Enabled(Settings.TelnetServer.Enabled());
    TelnetServer.Port(Settings.TelnetServer.Port());

    if (!TelnetServer.Enabled()) {
        Logger.Log("Telnet Server: Disabled", logger::LogLevels::Information);
        return true;
    }

    TelnetServer.WelcomeMessage(":: " + String(Version::ProductFamily) + " " +Settings.Network.Hostname() + " - Welcome");

    if (!RegisterTelnetCommands()) {
        Logger.Log("Error registering Telnet Server commands", logger::LogLevels::Error);
        return false;
    }

    TelnetServer.OnSessionBegin([](WiFiClient&, const telnetserver::SessionInfo& session) {
        Logger.Log("Telnet Server: Session started " + session.user + "@" + session.remoteIP.toString() + ":" + String(session.remotePort), logger::LogLevels::Information);
    });

    TelnetServer.OnSessionEnd([](WiFiClient&, const telnetserver::SessionInfo& session) {
        Logger.Log("Telnet Server: Session ended " + session.user + "@" + session.remoteIP.toString() + ":" + String(session.remotePort), logger::LogLevels::Information);
    });

    if (!TelnetServer.Start()) {
        Logger.Log("Error initializing Telnet Server object", logger::LogLevels::Error);
        return false;
    }

    Logger.Log("Telnet Server: Enabled on port " + String(TelnetServer.Port()), logger::LogLevels::Information);
    return true;
}

bool App::RegisterTelnetCommands() {
    const bool rebootRegistered = TelnetServer.OnCommand("reboot", "Reboot the device\r\n\r\nreboot", [&](WiFiClient& client, String*) {
        client.write(String(String(Version::ProductFamily) + " is rebooting...\r\n").c_str());
        client.stop();
        DeviceRestart();
    }, true);

    const bool dumpcfgRegistered = TelnetServer.OnCommand("dumpcfg", "Prints the configuration file\r\n\r\ndumpcfg", [&](WiFiClient& client, String*) {
        const String path = Defaults.ConfigFileName;
        String content;

        if (FileSystem.Read(path.c_str(), content) != filesystem::Result::Ok) {
            const String error = "Config         | Error reading config file '" + path + "'.\r\n";
            client.write(error.c_str());
            return;
        }

        const String header = "Config         | File: " + path + "\r\n\r\n";
        client.write(header.c_str());

        constexpr size_t chunkSize = 2048;
        for (size_t offset = 0; offset < content.length(); offset += chunkSize) {
            const size_t remaining = content.length() - offset;
            client.write(reinterpret_cast<const uint8_t*>(content.c_str() + offset), remaining < chunkSize ? remaining : chunkSize);
        }
        client.write("\r\n");
    }, true);

    const bool logonRegistered = TelnetServer.OnCommand("logon", "Log into the system with specific credentials\r\n\r\nlogon [username] [password]", [](WiFiClient& client, String* parameter) {
        String result;

        if (parameter[0].isEmpty() || parameter[1].isEmpty()) {
            result += "Logon          | Missing username and password.\r\n";
        } else {
            const UserReturn authentication = Settings.Users.Authenticate(parameter[0], parameter[1]);

            switch (authentication) {
                case UserReturn::AuthenticationSuccess : {
                    UserInfo user;
                    const UserReturn findResult = Settings.Users.Find(parameter[0], &user);

                    if (findResult == UserReturn::NoError && TelnetServer.SetSessionIdentity(client, user.username, user.admin)) {
                        result += "Logon          | Logon successful for user " + user.username + ".\r\n";
                        Logger.Log("Telnet Server: Logon successful for " + user.username + "@" + client.remoteIP().toString() + ":" + String(client.remotePort()), logger::LogLevels::Information);
                    } else {
                        result += "Logon          | Unable to update the Telnet session.\r\n";
                        Logger.Log("Telnet Server: Unable to update authenticated session", logger::LogLevels::Error);
                    }
                } break;

                case UserReturn::InvalidCredentials : {
                    result += "Logon          | Logon failed for user " + parameter[0] + " - Invalid credentials.\r\n";
                    Logger.Log("Telnet Server: Logon failed for " + parameter[0] + "@" + client.remoteIP().toString() + ":" + String(client.remotePort()) + " - Invalid credentials", logger::LogLevels::Warning);
                } break;

                case UserReturn::AuthenticationRateLimited : {
                    result += "Logon          | Too many failed attempts. Try again later.\r\n";
                    Logger.Log("Telnet Server: Logon rate limited for " + parameter[0] + "@" + client.remoteIP().toString() + ":" + String(client.remotePort()), logger::LogLevels::Warning);
                } break;

                case UserReturn::SynchronizationError : {
                    result += "Logon          | Authentication temporarily unavailable.\r\n";
                    Logger.Log("Telnet Server: User synchronization error during logon", logger::LogLevels::Error);
                } break;

                default: {
                    result += "Logon          | Authentication failed.\r\n";
                    Logger.Log("Telnet Server: Unexpected authentication result", logger::LogLevels::Error);
                } break;
            }
        }
        client.write(result.c_str());
    }, false);

    const bool versionRegistered = TelnetServer.OnCommand("ver", "Show device version info\r\n\r\nver", [](WiFiClient& client, String*) {
        String result;
        result += "Version        | Product: " + String(Version::ProductName) + "\r\n";
        result += "               | Family: " + String(Version::ProductFamily) + "\r\n";
        result += "               | Hardware: " + Version::Hardware::Info() + "\r\n";
        result += "               | Software: " + Version::Software::Info() + "\r\n";
        client.write(result.c_str());
    }, false);

    return rebootRegistered && dumpcfgRegistered && logonRegistered && versionRegistered;
}

void App::DeviceRestart() {
    Logger.Log("Device restart requested", logger::LogLevels::Information);
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP.restart();
}

void App::LogConfigurationStatus(bool configurationLoaded) {
    if (configurationLoaded) {
        Logger.Log(String("Configuration initialized - file " + String(Defaults.ConfigFileName) + " read"), logger::LogLevels::Information);
    } else {
        Logger.Log(String("Configuration initialized with defaults - file " + String(Defaults.ConfigFileName) + " not loaded"), logger::LogLevels::Warning);
    }
}

void App::LogNetworkStatus() {
    const network::APMode mode = Network.ConnectionMode();

    if (mode == network::APMode::Offline) {
        Logger.Log("Network status: offline", logger::LogLevels::Warning);
        return;
    }

    if (mode == network::APMode::WifiClient) {
        Logger.Log("Network status: Online - WiFi Client, connected to " + Network.SSID() + " (IP: " + Network.IP_Address().toString() + " | RSSI: " + String(Network.RSSI()) + " dBm)", logger::LogLevels::Information);
        return;
    }

    Logger.Log("Network status: Online - SoftAP Mode, connected to " + Network.SSID() + " (IP: " + Network.IP_Address().toString() + " | RSSI: " + String(Network.RSSI()) + " dBm)", logger::LogLevels::Information);
}
