#include <Arduino.h>

#include "App.h"
#include "Globals.h"
#include "Users.h"
#include "SystemInfo.h"

void App::Start() {
    Clock.SetEpoch(Defaults.InitialTimeAndDate);

    if (!InitializeFileSystem()) return;

    bool configurationLoaded = Settings.Load();
    const bool firstRun = Settings.FirstRun();
    bool firstRunConfigurationSaved = false;

    if (firstRun) {
        firstRunConfigurationSaved = Settings.Save();
        if (firstRunConfigurationSaved) configurationLoaded = Settings.Load();
    }

    Clock.TimeZone(Settings.General.TimeZone());

    if (!InitializeLogger()) return;

    Logger.Log(Version::Info(), logger::LogLevels::Information);
    Logger.Log("Logger initialized", logger::LogLevels::Information);
    Logger.Log("FileSystem initialized", logger::LogLevels::Information);

    if (firstRun) {
        if (firstRunConfigurationSaved && configurationLoaded) {
            Logger.Log("First Run - New configuration file " + String(Defaults.ConfigFileName) + " saved", logger::LogLevels::Information);
        } else if (!firstRunConfigurationSaved) {
            Logger.Log("First Run - Error saving new configuration file " + String(Defaults.ConfigFileName), logger::LogLevels::Error);
        } else {
            Logger.Log("First Run - New configuration file " + String(Defaults.ConfigFileName) + " saved but could not be loaded", logger::LogLevels::Error);
        }
    }

    if (!InitializeNetwork()) return;
    if (!InitializeClock()) return;
    if (!InitializeComponents()) return;
    (void)InitializeMQTT();
    if (!InitializeAutomation()) return;
    if (!InitializeStatePersistence()) return;
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
    if (!Settings.InstallComponents()) {
        Logger.Log("Components configuration missing or invalid; using built-in defaults", logger::LogLevels::Warning);
    }

    if (!ComponentController.Start()) {
        Logger.Log("Error initializing component task", logger::LogLevels::Error);
        return false;
    }

    Logger.Log("Component task initialized", logger::LogLevels::Information);
    return true;
}

bool App::InitializeMQTT() {
    if (!Settings.MQTT.Enabled()) {
        Logger.Log("MQTT disabled", logger::LogLevels::Information);
        return true;
    }

    if (!MQTTClient.Start()) {
        Logger.Log("Error initializing MQTT", logger::LogLevels::Error);
        return false;
    }

    Logger.Log("MQTT task initialized", logger::LogLevels::Information);
    return true;
}

bool App::InitializeStatePersistence() {
    if (pStatePersistenceTaskHandle != nullptr) return true;

    const BaseType_t result = xTaskCreate(
        StatePersistenceTaskEntry,
        "StateSave",
        STATE_PERSISTENCE_TASK_STACK_SIZE,
        this,
        STATE_PERSISTENCE_TASK_PRIORITY,
        &pStatePersistenceTaskHandle
    );

    if (result != pdPASS) {
        pStatePersistenceTaskHandle = nullptr;
        Logger.Log("Error starting component state persistence task", logger::LogLevels::Error);
        return false;
    }

    Logger.Log(
        "Component state persistence initialized (interval: " +
            String(Settings.General.SaveStatePooling()) + " seconds)",
        logger::LogLevels::Information
    );
    return true;
}

bool App::InitializeAutomation() {
    if (pAutomationTaskHandle != nullptr) return true;

    const BaseType_t result = xTaskCreate(
        AutomationTaskEntry,
        "Automation",
        AUTOMATION_TASK_STACK_SIZE,
        this,
        AUTOMATION_TASK_PRIORITY,
        &pAutomationTaskHandle
    );

    if (result != pdPASS) {
        pAutomationTaskHandle = nullptr;
        Logger.Log("Error starting automation task", logger::LogLevels::Error);
        return false;
    }

    Logger.Log(
        "Automation initialized (event bindings: " + String(Automation.Count()) + ")",
        logger::LogLevels::Information
    );
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

void App::StatePersistenceTaskEntry(void* parameter) {
    static_cast<App*>(parameter)->StatePersistenceTask();
}

void App::AutomationTaskEntry(void* parameter) {
    static_cast<App*>(parameter)->AutomationTask();
}

void App::AutomationTask() {
    ComponentEvent event;
    while (true) {
        if (ComponentController.ReceiveEvent(event, portMAX_DELAY)) {
            (void)MQTTClient.Notify(event);
            (void)Automation.Execute(event);
        }
    }
}

void App::StatePersistenceTask() {
    while (true) {
        const uint32_t intervalMs =
            static_cast<uint32_t>(Settings.General.SaveStatePooling()) * 1000U;
        vTaskDelay(pdMS_TO_TICKS(intervalMs));

        if (!ComponentController.PersistenceRequired() &&
            !Settings.SaveComponentsStateFlag()) {
            continue;
        }

        if (Settings.SaveComponentsState()) {
            Logger.Log("Component state saved", logger::LogLevels::Debug);
        } else {
            Logger.Log("Error saving component state", logger::LogLevels::Error);
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

    const bool compRegistered = TelnetServer.OnCommand(
        "comp",
        "Manage components\r\n\r\n"
        "comp list\r\n"
        "comp status [component_name|#component_id]\r\n"
        "comp set [component_name|#component_id] property=value\r\n"
        "comp rename [component_name|#component_id] name=newname\r\n"
        "comp remove [component_name|#component_id]\r\n"
        "comp add relay|button name=value [id=value] address=value ...",
        [](WiFiClient& client, String* parameters) {
            String subcommand = parameters[0];
            subcommand.toLowerCase();
            const bool mutation = subcommand == "set" || subcommand == "rename" ||
                subcommand == "remove" || subcommand == "add";

            if (mutation && !TelnetServer.IsSessionAdmin(client)) {
                client.write("Permission denied.\r\n");
                return;
            }

            String output;
            output.reserve(768);
            (void)Settings.ExecuteComponentCommand(parameters, output);
            client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
        },
        false
    );

    const bool hwinfoRegistered = TelnetServer.OnCommand("hwinfo", "Show ESP32 hardware information\r\n\r\nhwinfo", [](WiFiClient& client, String*) {
        String output;
        SystemInfo::Hardware(output);
        client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    }, false);

    const bool memRegistered = TelnetServer.OnCommand("mem", "Show memory usage\r\n\r\nmem [b|kb|mb]", [](WiFiClient& client, String* parameters) {
        String parameter = parameters[0];
        parameter.toLowerCase();

        SystemInfo::MemoryUnit unit = SystemInfo::MemoryUnit::Bytes;
        if (parameter.isEmpty() || parameter == "b") {
            unit = SystemInfo::MemoryUnit::Bytes;
        } else if (parameter == "kb") {
            unit = SystemInfo::MemoryUnit::Kilobytes;
        } else if (parameter == "mb") {
            unit = SystemInfo::MemoryUnit::Megabytes;
        } else {
            client.write("Usage: mem [b|kb|mb]\r\n");
            return;
        }

        String output;
        SystemInfo::Memory(output, unit);
        client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    }, true);

    const bool fsRegistered = TelnetServer.OnCommand("fs", "Show filesystem information\r\n\r\nfs", [](WiFiClient& client, String*) {
        String output;
        (void)SystemInfo::FileSystem(output);
        client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    }, true);

    const bool versionRegistered = TelnetServer.OnCommand("ver", "Show device version info\r\n\r\nver", [](WiFiClient& client, String*) {
        String result;
        result += "Version        | Product: " + String(Version::ProductName) + "\r\n";
        result += "               | Family: " + String(Version::ProductFamily) + "\r\n";
        result += "               | Serial: " + Version::SerialNumber() + "\r\n";
        result += "               | Hardware: " + Version::Hardware::Info() + "\r\n";
        result += "               | Software: " + Version::Software::Info() + "\r\n";
        client.write(result.c_str());
    }, false);

    return rebootRegistered && dumpcfgRegistered && logonRegistered && compRegistered &&
        hwinfoRegistered && memRegistered && fsRegistered && versionRegistered;
}

void App::DeviceRestart() {
    if (!Settings.SaveComponentsState()) {
        Logger.Log("Error saving component state before restart", logger::LogLevels::Error);
    }

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
        Logger.Log("Network status: Offline", logger::LogLevels::Warning);
        return;
    }

    if (mode == network::APMode::WifiClient) {
        Logger.Log("Network: WiFi Client, connected to " + Network.SSID() + " (Hostname: " + Network.Hostname() + " | IP: " + Network.IP_Address().toString() + " | MAC: " + Network.MAC_Address() + " | RSSI: " + String(Network.RSSI()) + " dBm)", logger::LogLevels::Information);
        return;
    }

    Logger.Log("Network: SoftAP Mode, connected to " + Network.SSID() + " (Hostname: " + Network.Hostname() + " | IP: " + Network.IP_Address().toString() + " | MAC: " + Network.MAC_Address() + " | RSSI: " + String(Network.RSSI()) + " dBm)", logger::LogLevels::Information);
}
