#include "MQTTCommands.h"

#include "CommandHelpers.h"
#include "core/Globals.h"

namespace {
    using cli::AppendSetting;
    using cli::BoolValue;
    using cli::JoinParameters;
    using cli::ParseBool;
    using cli::ParseUInt16;
    using cli::PasswordState;

    struct MQTTSettingsSnapshot {
        bool enabled;
        String broker;
        uint16_t port;
        String user;
        String password;
        bool discoveryEnabled;
        String discoveryPrefix;
    };

    MQTTSettingsSnapshot CaptureSettings() {
        return {
            Settings.MQTT.Enabled(),
            Settings.MQTT.Broker(),
            Settings.MQTT.Port(),
            Settings.MQTT.User(),
            Settings.MQTT.Password(),
            Settings.MQTT.DiscoveryEnabled(),
            Settings.MQTT.DiscoveryPrefix()
        };
    }

    void RestoreSettings(const MQTTSettingsSnapshot& snapshot) {
        Settings.MQTT.Enabled(snapshot.enabled);
        Settings.MQTT.Broker(snapshot.broker);
        Settings.MQTT.Port(snapshot.port);
        Settings.MQTT.User(snapshot.user);
        Settings.MQTT.Password(snapshot.password);
        Settings.MQTT.DiscoveryEnabled(snapshot.discoveryEnabled);
        Settings.MQTT.DiscoveryPrefix(snapshot.discoveryPrefix);
    }

    // mqttclient copies every one of these fields once, in Start(), and never
    // rereads Settings.MQTT afterward - there is no live reconfiguration path,
    // so every change here needs a restart to take effect.
    bool SaveChange(const MQTTSettingsSnapshot& snapshot, const String& name, const String& value, String& output) {
        if (!Settings.Save()) {
            RestoreSettings(snapshot);
            output = "Unable to save MQTT configuration. No changes were kept.\r\n";
            return false;
        }

        AppendSetting(output, name, value);
        output += "Configuration saved to " + String(Defaults.ConfigFileName) + ".\r\n";
        output += "Restart required to apply the change.\r\n";
        return true;
    }

    bool Usage(String& output) {
        output = "Usage: mqtt [show|enabled|broker|port|user|password|discovery|discovery-prefix]\r\n";
        return false;
    }

    bool IsValidBroker(const String& value) {
        if (value.isEmpty()) return true;
        if (value.length() < 3 || value.length() > 128) return false;
        for (size_t index = 0; index < value.length(); ++index) {
            const char character = value.charAt(index);
            const bool ok = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') || character == '.' || character == '-';
            if (!ok) return false;
        }
        return true;
    }

    bool IsValidUser(const String& value) {
        if (value.isEmpty()) return true;
        if (value.length() < 3 || value.length() > 64) return false;
        for (size_t index = 0; index < value.length(); ++index) {
            const char character = value.charAt(index);
            const bool ok = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') || character == '.' || character == '_' || character == '-';
            if (!ok) return false;
        }
        return true;
    }

    bool IsValidPassword(const String& value) {
        if (value.isEmpty()) return true;
        if (value.length() < 6 || value.length() > 64) return false;
        for (size_t index = 0; index < value.length(); ++index) {
            const unsigned char character = static_cast<unsigned char>(value[index]);
            if (character < 0x20 || character > 0x7E) return false;
        }
        return true;
    }

    bool IsValidDiscoveryPrefix(const String& value) {
        if (value.isEmpty()) return true;
        if (value.length() > 64) return false;
        for (size_t index = 0; index < value.length(); ++index) {
            const char character = value.charAt(index);
            const bool ok = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') || character == '_' || character == '-';
            if (!ok) return false;
        }
        return true;
    }

    void ShowAll(String& output, bool isAdmin) {
        AppendSetting(output, "Enabled", BoolValue(Settings.MQTT.Enabled()));
        AppendSetting(output, "Broker", Settings.MQTT.Broker().isEmpty() ? "not set" : Settings.MQTT.Broker());
        AppendSetting(output, "Port", String(Settings.MQTT.Port()));
        AppendSetting(output, "User", Settings.MQTT.User().isEmpty() ? "not set" : Settings.MQTT.User());
        AppendSetting(output, "Password", isAdmin ? Settings.MQTT.Password() : PasswordState(Settings.MQTT.Password()));
        AppendSetting(output, "Discovery Enabled", BoolValue(Settings.MQTT.DiscoveryEnabled()));
        AppendSetting(output, "Discovery Prefix", Settings.MQTT.DiscoveryPrefix());
    }

    bool IsMQTTMutation(String* parameters) noexcept {
        String command = parameters[0];
        command.toLowerCase();
        if (command == "show" || command.isEmpty()) return false;
        return !parameters[1].isEmpty();
    }

    bool ExecuteMQTTCommand(String* parameters, bool isAdmin, String& output) {
        output.clear();
        String command = parameters[0];
        command.toLowerCase();

        if (command.isEmpty() || command == "show") {
            if (!parameters[1].isEmpty()) return Usage(output);
            ShowAll(output, isAdmin);
            return true;
        }

        if (parameters[1].isEmpty()) {
            if (command == "enabled") AppendSetting(output, "Enabled", BoolValue(Settings.MQTT.Enabled()));
            else if (command == "broker") AppendSetting(output, "Broker", Settings.MQTT.Broker().isEmpty() ? "not set" : Settings.MQTT.Broker());
            else if (command == "port") AppendSetting(output, "Port", String(Settings.MQTT.Port()));
            else if (command == "user") AppendSetting(output, "User", Settings.MQTT.User().isEmpty() ? "not set" : Settings.MQTT.User());
            else if (command == "password") AppendSetting(output, "Password", isAdmin ? Settings.MQTT.Password() : PasswordState(Settings.MQTT.Password()));
            else if (command == "discovery") AppendSetting(output, "Discovery Enabled", BoolValue(Settings.MQTT.DiscoveryEnabled()));
            else if (command == "discovery-prefix") AppendSetting(output, "Discovery Prefix", Settings.MQTT.DiscoveryPrefix());
            else return Usage(output);
            return true;
        }

        const MQTTSettingsSnapshot snapshot = CaptureSettings();

        if (command == "enabled" || command == "discovery") {
            bool value = false;
            if (!ParseBool(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be on or off.\r\n";
                return false;
            }
            if (command == "enabled") Settings.MQTT.Enabled(value);
            else Settings.MQTT.DiscoveryEnabled(value);
            return SaveChange(snapshot, command == "enabled" ? "Enabled" : "Discovery Enabled", BoolValue(value), output);
        }

        if (command == "broker") {
            String value = JoinParameters(parameters, 1);
            if (value.equalsIgnoreCase("clear")) value.clear();
            if (!IsValidBroker(value)) {
                output = "Broker must be empty or 3-128 characters: letters, digits, '.', '-', no spaces.\r\n";
                return false;
            }
            Settings.MQTT.Broker(value);
            return SaveChange(snapshot, "Broker", Settings.MQTT.Broker().isEmpty() ? "not set" : Settings.MQTT.Broker(), output);
        }

        if (command == "port") {
            uint16_t value = 0;
            if (!ParseUInt16(parameters[1], value, false) || !parameters[2].isEmpty()) {
                output = "Value must be 1-65535.\r\n";
                return false;
            }
            Settings.MQTT.Port(value);
            return SaveChange(snapshot, "Port", String(Settings.MQTT.Port()), output);
        }

        if (command == "user") {
            String value = JoinParameters(parameters, 1);
            if (value.equalsIgnoreCase("clear")) value.clear();
            if (!IsValidUser(value)) {
                output = "User must be empty or 3-64 characters: letters, digits, '.', '_', '-'.\r\n";
                return false;
            }
            Settings.MQTT.User(value);
            return SaveChange(snapshot, "User", Settings.MQTT.User().isEmpty() ? "not set" : Settings.MQTT.User(), output);
        }

        if (command == "password") {
            String value = JoinParameters(parameters, 1);
            if (value.equalsIgnoreCase("clear")) value.clear();
            if (!IsValidPassword(value)) {
                output = "Password must be empty or 6-64 printable ASCII characters.\r\n";
                return false;
            }
            Settings.MQTT.Password(value);
            // Reached only after the admin check for mutations, so it's safe
            // to echo the value that was just set.
            return SaveChange(snapshot, "Password", Settings.MQTT.Password().isEmpty() ? "not set" : Settings.MQTT.Password(), output);
        }

        if (command == "discovery-prefix") {
            String value = parameters[1];
            if (value.equalsIgnoreCase("clear")) value.clear();
            if (!parameters[2].isEmpty() || !IsValidDiscoveryPrefix(value)) {
                output = "Discovery prefix must be 1-64 characters: letters, digits, '_', '-'. 'clear' resets it to the default.\r\n";
                return false;
            }
            Settings.MQTT.DiscoveryPrefix(value);
            return SaveChange(snapshot, "Discovery Prefix", Settings.MQTT.DiscoveryPrefix(), output);
        }

        return Usage(output);
    }
}

bool cli::RegisterMQTTCommands() {
    return TelnetServer.OnCommand(
        "mqtt",
        "Show and configure MQTT and Home Assistant discovery\r\n\r\n"
        "mqtt [show]\r\n"
        "mqtt enabled [on|off]\r\n"
        "mqtt broker [hostname|address|clear]\r\n"
        "mqtt port [value]\r\n"
        "mqtt user [value|clear]\r\n"
        "mqtt password [value|clear]\r\n"
        "mqtt discovery [on|off]\r\n"
        "mqtt discovery-prefix [value|clear]\r\n"
        "Omit a value to show it. Changes require an administrative session\r\n"
        "and a restart.",
        [](WiFiClient& client, String* parameters) {
            const bool mutation = IsMQTTMutation(parameters);
            if (mutation && !TelnetServer.IsSessionAdmin(client)) {
                Logger.Log("CLI mqtt mutation denied for " + client.remoteIP().toString(), logger::LogLevels::Warning);
                client.write(telnetserver::FormatLine("MQTT", "Permission denied.").c_str());
                return;
            }

            String output;
            output.reserve(384);
            const bool success = ExecuteMQTTCommand(parameters, TelnetServer.IsSessionAdmin(client), output);
            if (mutation) {
                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                Logger.Log(
                    "CLI mqtt mutation " + String(success ? "accepted" : "rejected") + " for " + identity + "@" +
                        client.remoteIP().toString() + ": mqtt " + parameters[0],
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );
            }
            const String formatted = telnetserver::FormatBlock("MQTT", output);
            client.write(reinterpret_cast<const uint8_t*>(formatted.c_str()), formatted.length());
        },
        false
    );
}
