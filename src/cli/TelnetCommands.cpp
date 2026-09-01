#include "TelnetCommands.h"

#include <cstdlib>

#include "CommandHelpers.h"
#include "core/Globals.h"

namespace {
    using cli::AppendSetting;
    using cli::BoolValue;
    using cli::ParseBool;
    using cli::ParseUInt16;

    struct TelnetSettingsSnapshot {
        bool enabled;
        uint16_t port;
        uint32_t idleTimeoutMs;
        uint8_t maxSessions;
    };

    TelnetSettingsSnapshot CaptureSettings() {
        return {
            Settings.TelnetServer.Enabled(),
            Settings.TelnetServer.Port(),
            Settings.TelnetServer.IdleTimeoutMs(),
            Settings.TelnetServer.MaxSessions()
        };
    }

    void RestoreSettings(const TelnetSettingsSnapshot& snapshot) {
        Settings.TelnetServer.Enabled(snapshot.enabled);
        Settings.TelnetServer.Port(snapshot.port);
        Settings.TelnetServer.IdleTimeoutMs(snapshot.idleTimeoutMs);
        Settings.TelnetServer.MaxSessions(snapshot.maxSessions);
    }

    bool SaveChange(const TelnetSettingsSnapshot& snapshot, const String& name, const String& value, String& output) {
        if (!Settings.Save()) {
            RestoreSettings(snapshot);
            output = "Unable to save Telnet configuration. No changes were kept.\r\n";
            return false;
        }

        AppendSetting(output, name, value);
        output += "Configuration saved to " + String(Defaults.ConfigFileName) + ".\r\n";
        return true;
    }

    bool Usage(String& output) {
        output = "Usage: telnet [show|enabled|port|idle-timeout|max-sessions]\r\n";
        return false;
    }

    String IdleTimeoutDisplay(uint32_t valueMs) {
        return valueMs == 0 ? String("disabled") : String(valueMs) + " ms";
    }

    bool ParseIdleTimeout(const String& value, uint32_t& parsedMs) {
        if (value.equalsIgnoreCase("off") || value.equalsIgnoreCase("disabled")) {
            parsedMs = 0;
            return true;
        }

        if (value.isEmpty()) return false;
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
        constexpr unsigned long MaxIdleTimeoutMs = 86400000UL; // 24 hours
        if (end == value.c_str() || *end != '\0' || parsed > MaxIdleTimeoutMs) return false;
        parsedMs = static_cast<uint32_t>(parsed);
        return true;
    }

    void ShowAll(String& output) {
        AppendSetting(output, "Current Enabled", BoolValue(TelnetServer.Enabled()));
        AppendSetting(output, "Current Port", String(TelnetServer.Port()));
        AppendSetting(output, "Current Idle Timeout", IdleTimeoutDisplay(TelnetServer.IdleTimeout()));
        AppendSetting(output, "Current Max Sessions", String(TelnetServer.MaxSessions()));
        output += "\r\n";
        AppendSetting(output, "Enabled", BoolValue(Settings.TelnetServer.Enabled()));
        AppendSetting(output, "Port", String(Settings.TelnetServer.Port()));
        AppendSetting(output, "Idle Timeout", IdleTimeoutDisplay(Settings.TelnetServer.IdleTimeoutMs()));
        AppendSetting(output, "Max Sessions", String(Settings.TelnetServer.MaxSessions()));
    }

    bool IsTelnetMutation(String* parameters) noexcept {
        String command = parameters[0];
        command.toLowerCase();
        if (command == "show" || command.isEmpty()) return false;
        return !parameters[1].isEmpty();
    }

    bool ExecuteTelnetCommand(String* parameters, String& output) {
        output.clear();
        String command = parameters[0];
        command.toLowerCase();

        if (command.isEmpty() || command == "show") {
            if (!parameters[1].isEmpty()) return Usage(output);
            ShowAll(output);
            return true;
        }

        if (parameters[1].isEmpty()) {
            if (command == "enabled") AppendSetting(output, "Enabled", BoolValue(Settings.TelnetServer.Enabled()));
            else if (command == "port") AppendSetting(output, "Port", String(Settings.TelnetServer.Port()));
            else if (command == "idle-timeout") AppendSetting(output, "Idle Timeout", IdleTimeoutDisplay(Settings.TelnetServer.IdleTimeoutMs()));
            else if (command == "max-sessions") AppendSetting(output, "Max Sessions", String(Settings.TelnetServer.MaxSessions()));
            else return Usage(output);
            return true;
        }

        const TelnetSettingsSnapshot snapshot = CaptureSettings();

        if (command == "enabled") {
            bool value = false;
            if (!ParseBool(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be on or off.\r\n";
                return false;
            }
            Settings.TelnetServer.Enabled(value);
            if (!SaveChange(snapshot, "Enabled", BoolValue(value), output)) return false;
            output += "Restart required to apply the change.\r\n";
            return true;
        }

        if (command == "port") {
            uint16_t value = 0;
            if (!ParseUInt16(parameters[1], value, false) || !parameters[2].isEmpty()) {
                output = "Value must be 1-65535.\r\n";
                return false;
            }
            Settings.TelnetServer.Port(value);
            if (!SaveChange(snapshot, "Port", String(Settings.TelnetServer.Port()), output)) return false;
            output += "Restart required to apply the change.\r\n";
            return true;
        }

        if (command == "idle-timeout") {
            uint32_t value = 0;
            if (!ParseIdleTimeout(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be milliseconds (0-86400000), or 'off' to disable.\r\n";
                return false;
            }
            Settings.TelnetServer.IdleTimeoutMs(value);
            if (!SaveChange(snapshot, "Idle Timeout", IdleTimeoutDisplay(value), output)) return false;
            TelnetServer.IdleTimeout(value);
            output += "Applied immediately.\r\n";
            return true;
        }

        if (command == "max-sessions") {
            uint16_t value = 0;
            if (!ParseUInt16(parameters[1], value, false) || !parameters[2].isEmpty() ||
                value < telnetserver::MIN_SESSIONS || value > telnetserver::MAX_SESSIONS) {
                output = "Value must be " + String(telnetserver::MIN_SESSIONS) + "-" + String(telnetserver::MAX_SESSIONS) + ".\r\n";
                return false;
            }
            Settings.TelnetServer.MaxSessions(static_cast<uint8_t>(value));
            if (!SaveChange(snapshot, "Max Sessions", String(Settings.TelnetServer.MaxSessions()), output)) return false;
            TelnetServer.MaxSessions(value);
            output += "Applied immediately.\r\n";
            return true;
        }

        return Usage(output);
    }
}

bool cli::RegisterTelnetCommands() {
    return TelnetServer.OnCommand(
        "telnet",
        "Show and configure the Telnet server\r\n\r\n"
        "telnet [show]\r\n"
        "telnet enabled [on|off]\r\n"
        "telnet port [value]\r\n"
        "telnet idle-timeout [milliseconds|off]\r\n"
        "telnet max-sessions [value]\r\n"
        "Omit a value to show it. Enabled/port changes require an administrative\r\n"
        "session and a restart. Idle-timeout and max-sessions apply immediately.",
        [](WiFiClient& client, String* parameters) {
            const bool mutation = IsTelnetMutation(parameters);
            if (mutation && !TelnetServer.IsSessionAdmin(client)) {
                Logger.Log("CLI telnet mutation denied for " + client.remoteIP().toString(), logger::LogLevels::Warning);
                client.write(telnetserver::FormatLine("Telnet", "Permission denied.").c_str());
                return;
            }

            String output;
            output.reserve(512);
            const bool success = ExecuteTelnetCommand(parameters, output);
            if (mutation) {
                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                Logger.Log(
                    "CLI telnet mutation " + String(success ? "accepted" : "rejected") + " for " + identity + "@" +
                        client.remoteIP().toString() + ": telnet " + parameters[0],
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );
            }
            const String formatted = telnetserver::FormatBlock("Telnet", output);
            client.write(reinterpret_cast<const uint8_t*>(formatted.c_str()), formatted.length());
        },
        false
    );
}
