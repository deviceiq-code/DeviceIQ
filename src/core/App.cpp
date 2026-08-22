#include <Arduino.h>

#include "App.h"
#include "Globals.h"

namespace {
    void LogNetworkStatus() {
        const network::APMode mode = Network.ConnectionMode();

        if (mode == network::APMode::Offline) {
            Logger.Log("Network status: offline", logger::LogLevels::Warning);
            return;
        }

        if (mode == network::APMode::WifiClient) {
            Logger.Log("Network status: Online - WiFi Client, connected to " + Network.SSID() + " (IP: " + Network.IP_Address().toString() + " | RSSI: " + String(Network.RSSI()) + " dBm)", logger::LogLevels::Information);
        }

        if (mode == network::APMode::SoftAP) {
            Logger.Log("Network status: Online - SoftAP Mode, connected to " + Network.SSID() + " (IP: " + Network.IP_Address().toString() + " | RSSI: " + String(Network.RSSI()) + " dBm)", logger::LogLevels::Information);
        }
    }
}

void App::Start() {
    // Clock
    Clock.SetEpoch(Defaults.InitialTimeAndDate);

    // FileSystem
    if (FileSystem.Start(true) == false) {
        Serial.println("Error initializing FileSystem object");
        return;
    }

    // Settings
    const bool configurationLoaded = Settings.Load();

    // Network configuration
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

    // Telnet Server configuration
    TelnetServer.Enabled(Settings.TelnetServer.Enabled());
    TelnetServer.Port(Settings.TelnetServer.Port());

    // Logger
    Logger.LogLevel(Settings.Log.LogLevel());
    Logger.Endpoint(Settings.Log.Endpoint());
    Logger.SyslogServerHost(Settings.Log.SyslogServerHost());
    Logger.SyslogServerPort(Settings.Log.SyslogServerPort());

    if (Logger.Start() == false) {
        Serial.println("Error initializing Logger object");
        return;
    }

    if (!stationPasswordValid) {
        Logger.Log("Invalid WiFi passphrase", logger::LogLevels::Error);
    }

    Network.OnModeChanged(LogNetworkStatus);

    if (Network.Start() == false) {
        Logger.Log("Error initializing Network object", logger::LogLevels::Error);
        return;
    }

    Logger.Log(Version::Info(), logger::LogLevels::Information);
    Logger.Log("Logger initialized", logger::LogLevels::Information);
    Logger.Log("FileSystem initialized", logger::LogLevels::Information);
    Logger.Log("Network initialized", logger::LogLevels::Information);

    if (Settings.TelnetServer.Enabled()) {
        TelnetServer.WelcomeMessage(String(":: " + String(Version::ProductFamily) + " " + Settings.Network.Hostname() + " - Welcome"));

        // Commands
        TelnetServer.OnCommand("ver", "Show device version info\r\n\r\nver", [&](WiFiClient& client, String* parameter) {
            String result;

            result += "Version        | Product: " + String(Version::ProductName) + "\r\n";
            result += "               | Family: " + String(Version::ProductFamily) + "\r\n";
            result += "               | Hardware: " + Version::Hardware::Info() + "\r\n";
            result += "               | Software: " + Version::Software::Info() + "\r\n";

            client.write(result.c_str());
        }, false);
        
        TelnetServer.OnSessionBegin([](WiFiClient&, const telnetserver::SessionInfo& session) {
            Logger.Log("Telnet Server: Session started " + session.user + "@" + session.remoteIP.toString() + ":" + String(session.remotePort), logger::LogLevels::Information);
        });

        TelnetServer.OnSessionEnd([](WiFiClient&, const telnetserver::SessionInfo& session) {
            Logger.Log("Telnet Server: Session ended " + session.user + "@" + session.remoteIP.toString() + ":" + String(session.remotePort), logger::LogLevels::Information);
        });

        if (TelnetServer.Start() == false) {
            Logger.Log("Error initializing Telnet Server object", logger::LogLevels::Error);
            return;
        }
        Logger.Log("Telnet Server: Enabled on port " + String(Settings.TelnetServer.Port()), logger::LogLevels::Information);
    } else {
        Logger.Log("Telnet Server: Disabled", logger::LogLevels::Information);
    }

    if (configurationLoaded) {
        Logger.Log(String("Configuration initialized - file " + String(Defaults.ConfigFileName) + " read"), logger::LogLevels::Information);
    } else {
        Logger.Log(String("Configuration initialized with defaults - file " + String(Defaults.ConfigFileName) + " not loaded"), logger::LogLevels::Warning);
    }

    xTaskCreate([](void* parameter) {
        while (true) {
            Logger.Log("Hello!", logger::LogLevels::Information);
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }, "FileSystemTest", 2048, nullptr, 1, nullptr);
}
