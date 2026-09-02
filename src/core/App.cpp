#include <Arduino.h>

#include "App.h"
#include "Globals.h"
#include "components/Blinds.h"
#include "components/Relay.h"
#include "components/Thermometer.h"
#include "cli/CLI.h"

void app::Start() {
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
    // A misconfigured component (e.g. an invalid GPIO picked through the
    // web UI) must never take the rest of the device offline with it - that
    // would leave no way to fix the configuration except a serial reflash.
    // So this failing is logged, not fatal: components just end up
    // uninstalled/inactive, and boot continues so Telnet/Web management
    // still comes up to fix the bad configuration and reboot.
    (void)InitializeComponents();
    (void)InitializeMQTT();
    if (!InitializeAutomation()) return;
    if (!InitializeStatePersistence()) return;
    if (!InitializeTelnetServer()) return;
    if (!InitializeHTTPServer()) return;
    if (!InitializeWebhookServer()) return;

    LogConfigurationStatus(configurationLoaded);
}

bool app::InitializeFileSystem() {
    if (!FileSystem.Start(true)) {
        Serial.println("Error initializing FileSystem object");
        return false;
    }

    return true;
}

bool app::InitializeLogger() {
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

bool app::InitializeNetwork() {
    Network.ConnectionTimeout(Settings.Network.ConnectionTimeout());
    Network.ReconnectEnabled(Settings.Network.ReconnectEnabled());
    Network.ReconnectInitialInterval(Settings.Network.ReconnectInitialInterval());
    Network.ReconnectMaximumInterval(Settings.Network.ReconnectMaximumInterval());
    Network.FallbackAPEnabled(Settings.Network.FallbackAPEnabled());
    Network.FallbackAPRetention(Settings.Network.FallbackAPRetention());
    Network.DHCP_Client(Settings.Network.DHCPClient());
    Network.SSID(Settings.Network.SSID());
    const bool stationPasswordValid = Network.Passphrase(Settings.Network.Passphrase());
    Network.Hostname(Settings.Network.Hostname());
    const String fallbackSSID = Settings.Network.FallbackAPSSID();
    Network.SoftAP_SSID(fallbackSSID.isEmpty() ? Settings.Network.Hostname() : fallbackSSID);
    const bool fallbackPasswordValid = Network.SoftAP_Password(Settings.Network.FallbackAPPassword());
    Network.IP_Address(Settings.Network.IP_Address());
    Network.Netmask(Settings.Network.Netmask());
    Network.Gateway(Settings.Network.Gateway());
    Network.DNS_Server(0, Settings.Network.DNS(0));
    Network.DNS_Server(1, Settings.Network.DNS(1));

    if (!stationPasswordValid) {
        Logger.Log("Invalid WiFi passphrase", logger::LogLevels::Error);
    }
    if (!fallbackPasswordValid) {
        Logger.Log("Invalid fallback SoftAP password", logger::LogLevels::Error);
    }

    Network.OnModeChanged(LogNetworkStatus);

    if (!Network.Start()) {
        Logger.Log("Error initializing Network object", logger::LogLevels::Error);
        return false;
    }

    Logger.Log("Network task initialized", logger::LogLevels::Information);
    return true;
}

bool app::InitializeClock() {
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

bool app::InitializeComponents() {
    if (!Settings.InstallComponents()) {
        Logger.Log("Components schema or configuration is invalid; no components installed", logger::LogLevels::Error);
    }

    if (!ComponentController.Start()) {
        const String detail = ComponentController.StartError();
        Logger.Log("Error initializing components" + (detail.isEmpty() ? String() : ": " + detail), logger::LogLevels::Error);
        return false;
    }

    Logger.Log("Component task initialized", logger::LogLevels::Information);
    return true;
}

bool app::InitializeMQTT() {
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

bool app::InitializeStatePersistence() {
    if (pStatePersistenceTaskHandle != nullptr) return true;

    const BaseType_t result = xTaskCreate(StatePersistenceTaskEntry, "StateSave", STATE_PERSISTENCE_TASK_STACK_SIZE, this, STATE_PERSISTENCE_TASK_PRIORITY, &pStatePersistenceTaskHandle);

    if (result != pdPASS) {
        pStatePersistenceTaskHandle = nullptr;
        Logger.Log("Error starting component state persistence task", logger::LogLevels::Error);
        return false;
    }

    Logger.Log("Component state persistence initialized (interval: " + String(Settings.General.SaveStatePooling()) + " seconds)", logger::LogLevels::Information);
    return true;
}

bool app::InitializeAutomation() {
    if (pAutomationTaskHandle != nullptr) return true;

    const BaseType_t result = xTaskCreate(AutomationTaskEntry, "Automation", AUTOMATION_TASK_STACK_SIZE, this, AUTOMATION_TASK_PRIORITY, &pAutomationTaskHandle);

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

void app::ClockTask() {
    uint32_t consecutiveFailures = 0;
    while (true) {
        if (Network.ConnectionMode() != network::APMode::WifiClient) {
            vTaskDelay(pdMS_TO_TICKS(NTP_OFFLINE_RETRY_MS));
            continue;
        }

        const String server = Settings.General.NTPServer();
        const bool updated = Clock.NTPUpdate(server);

        if (updated) {
            Logger.Log(
                consecutiveFailures > 0
                    ? "Date and time: NTP synchronization recovered using " + server
                    : "Date and time: Updated from NTP server " + server,
                logger::LogLevels::Information
            );
            consecutiveFailures = 0;
            vTaskDelay(pdMS_TO_TICKS(NTP_UPDATE_INTERVAL_MS));
        } else {
            ++consecutiveFailures;
            if (consecutiveFailures == 1 || (consecutiveFailures % 10U) == 0U) {
                Logger.Log(
                    "Date and time: NTP update failed using " + server + " (attempts: " + String(consecutiveFailures) + ")",
                    logger::LogLevels::Warning
                );
            }
            vTaskDelay(pdMS_TO_TICKS(NTP_FAILURE_RETRY_MS));
        }
    }
}

void app::AutomationTask() {
    ComponentEvent event;
    while (true) {
        if (ComponentController.ReceiveEvent(event, pdMS_TO_TICKS(1000))) {
            if (event.source != nullptr) {
                if (event.source->Class() == component::Classes::Relay && event.code == relay::EventCodes::WriteFailed) {
                    Logger.Log("Relay #" + String(event.source->ID()) + " " + event.source->Name() + ": output write failed", logger::LogLevels::Error);
                } else if (event.source->Class() == component::Classes::Thermometer && event.code == thermometer::EventCodes::ReadFailed) {
                    Logger.Log("Thermometer #" + String(event.source->ID()) + " " + event.source->Name() + ": sensor read failed", logger::LogLevels::Warning);
                } else if (event.source->Class() == component::Classes::Thermometer && event.code == thermometer::EventCodes::ReadRecovered) {
                    Logger.Log("Thermometer #" + String(event.source->ID()) + " " + event.source->Name() + ": sensor reading recovered", logger::LogLevels::Information);
                } else if (event.source->Class() == component::Classes::Blinds && event.code == blinds::EventCodes::Fault) {
                    const int32_t code = event.value;
                    const char* reason = (code == 1 || code == -1) ? "relay output write failed" :
                        (code == 2 || code == -2) ? "both movement directions became active" :
                        (code == 3 || code == -3) ? "invalid travel time" : "unknown fault";
                    Logger.Log("Blinds #" + String(event.source->ID()) + " " + event.source->Name() + ": " + reason + " (code " + String(code) + ")", logger::LogLevels::Error);
                }
            }
            (void)MQTTClient.Notify(event);
            (void)Automation.Execute(event);
        }

        const uint32_t droppedCommands = ComponentController.TakeDroppedCommands();
        const uint32_t droppedEvents = ComponentController.TakeDroppedEvents();
        const uint32_t droppedMQTTEvents = MQTTClient.TakeDroppedEvents();
        if (droppedCommands > 0) Logger.Log("Component command queue full: " + String(droppedCommands) + " command(s) dropped", logger::LogLevels::Warning);
        if (droppedEvents > 0) Logger.Log("Component event queue full: " + String(droppedEvents) + " event(s) dropped", logger::LogLevels::Warning);
        if (droppedMQTTEvents > 0) Logger.Log("MQTT event queue full: " + String(droppedMQTTEvents) + " event(s) dropped", logger::LogLevels::Warning);
    }
}

void app::StatePersistenceTask() {
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

bool app::InitializeTelnetServer() {
    TelnetServer.Enabled(Settings.TelnetServer.Enabled());
    TelnetServer.Port(Settings.TelnetServer.Port());
    TelnetServer.IdleTimeout(Settings.TelnetServer.IdleTimeoutMs());
    TelnetServer.MaxSessions(Settings.TelnetServer.MaxSessions());

    if (!TelnetServer.Enabled()) {
        Logger.Log("Telnet Server: Disabled", logger::LogLevels::Information);
        return true;
    }

    TelnetServer.WelcomeMessage(":: " + String(Version::ProductFamily) + " " +Settings.Network.Hostname() + " - Welcome");

    if (!cli::RegisterCommands()) {
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

bool app::InitializeHTTPServer() {
    HTTPServer.Enabled(Settings.WebServer.Enabled());
    HTTPServer.Port(Settings.WebServer.Port());
    HTTPServer.IdleTimeout(Settings.WebServer.IdleTimeoutMs());
    HTTPServer.MaxSessions(Settings.WebServer.MaxSessions());

    if (!HTTPServer.Enabled()) {
        Logger.Log("Web Server: Disabled", logger::LogLevels::Information);
        return true;
    }

    if (!HTTPServer.Start()) {
        Logger.Log("Error initializing Web Server object", logger::LogLevels::Error);
        return false;
    }

    Logger.Log("Web Server: Enabled on port " + String(HTTPServer.Port()), logger::LogLevels::Information);
    return true;
}

bool app::InitializeWebhookServer() {
    WebhookServer.Enabled(Settings.Webhooks.Enabled());
    WebhookServer.Port(Settings.Webhooks.Port());

    if (!WebhookServer.Enabled()) {
        Logger.Log("Webhooks: Disabled", logger::LogLevels::Information);
        return true;
    }

    if (!WebhookServer.Start()) {
        Logger.Log("Error initializing Webhooks Server object", logger::LogLevels::Error);
        return false;
    }

    Logger.Log("Webhooks: Enabled on port " + String(WebhookServer.Port()), logger::LogLevels::Information);
    return true;
}

void app::LogConfigurationStatus(bool configurationLoaded) {
    if (configurationLoaded) {
        Logger.Log(String("Configuration initialized - file " + String(Defaults.ConfigFileName) + " read"), logger::LogLevels::Information);
    } else {
        Logger.Log(String("Configuration initialized with defaults - file " + String(Defaults.ConfigFileName) + " not loaded"), logger::LogLevels::Warning);
    }
}

void app::LogNetworkStatus() {
    const network::APMode mode = Network.ConnectionMode();

    if (mode == network::APMode::Offline) {
        Logger.Log("Network status: Offline", logger::LogLevels::Warning);
        return;
    }

    if (mode == network::APMode::WifiClient) {
        Logger.Log("Network: WiFi Client, connected to " + Network.SSID() + " (Hostname: " + Network.Hostname() + " | IP: " + Network.IP_Address().toString() + " | MAC: " + Network.MAC_Address() + " | RSSI: " + String(Network.RSSI()) + " dBm)", logger::LogLevels::Information);
        return;
    }

    Logger.Log("Network: SoftAP active as " + Network.SSID() + " (Hostname: " + Network.Hostname() + " | IP: " + Network.IP_Address().toString() + " | MAC: " + Network.MAC_Address() + ")", logger::LogLevels::Information);
}
