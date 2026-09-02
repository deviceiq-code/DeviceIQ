#include "WebCommands.h"

#include <cstdlib>

#include "CommandHelpers.h"
#include "core/Globals.h"

namespace {
    using cli::AppendSetting;
    using cli::BoolValue;
    using cli::ParseBool;
    using cli::ParseUInt16;

    struct WebSettingsSnapshot {
        bool enabled;
        uint16_t port;
        uint32_t idleTimeoutMs;
        uint8_t maxSessions;
    };

    WebSettingsSnapshot CaptureSettings() {
        return {
            Settings.WebServer.Enabled(),
            Settings.WebServer.Port(),
            Settings.WebServer.IdleTimeoutMs(),
            Settings.WebServer.MaxSessions()
        };
    }

    void RestoreSettings(const WebSettingsSnapshot& snapshot) {
        Settings.WebServer.Enabled(snapshot.enabled);
        Settings.WebServer.Port(snapshot.port);
        Settings.WebServer.IdleTimeoutMs(snapshot.idleTimeoutMs);
        Settings.WebServer.MaxSessions(snapshot.maxSessions);
    }

    bool SaveChange(const WebSettingsSnapshot& snapshot, const String& name, const String& value, String& output) {
        if (!Settings.Save()) {
            RestoreSettings(snapshot);
            output = "Unable to save Web Server configuration. No changes were kept.\r\n";
            return false;
        }

        AppendSetting(output, name, value);
        output += "Configuration saved to " + String(Defaults.ConfigFileName) + ".\r\n";
        return true;
    }

    bool Usage(String& output) {
        output = "Usage: web [show|enabled|port|idle-timeout|max-sessions]\r\n";
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
        AppendSetting(output, "Current Enabled", BoolValue(HTTPServer.Enabled()));
        AppendSetting(output, "Current Port", String(HTTPServer.Port()));
        AppendSetting(output, "Current Idle Timeout", IdleTimeoutDisplay(HTTPServer.IdleTimeout()));
        AppendSetting(output, "Current Max Sessions", String(HTTPServer.MaxSessions()));
        output += "\r\n";
        AppendSetting(output, "Enabled", BoolValue(Settings.WebServer.Enabled()));
        AppendSetting(output, "Port", String(Settings.WebServer.Port()));
        AppendSetting(output, "Idle Timeout", IdleTimeoutDisplay(Settings.WebServer.IdleTimeoutMs()));
        AppendSetting(output, "Max Sessions", String(Settings.WebServer.MaxSessions()));
    }

    bool IsWebMutation(String* parameters) noexcept {
        String command = parameters[0];
        command.toLowerCase();
        if (command == "show" || command.isEmpty()) return false;
        return !parameters[1].isEmpty();
    }

    bool ExecuteWebCommand(String* parameters, String& output) {
        output.clear();
        String command = parameters[0];
        command.toLowerCase();

        if (command.isEmpty() || command == "show") {
            if (!parameters[1].isEmpty()) return Usage(output);
            ShowAll(output);
            return true;
        }

        if (parameters[1].isEmpty()) {
            if (command == "enabled") AppendSetting(output, "Enabled", BoolValue(Settings.WebServer.Enabled()));
            else if (command == "port") AppendSetting(output, "Port", String(Settings.WebServer.Port()));
            else if (command == "idle-timeout") AppendSetting(output, "Idle Timeout", IdleTimeoutDisplay(Settings.WebServer.IdleTimeoutMs()));
            else if (command == "max-sessions") AppendSetting(output, "Max Sessions", String(Settings.WebServer.MaxSessions()));
            else return Usage(output);
            return true;
        }

        const WebSettingsSnapshot snapshot = CaptureSettings();

        if (command == "enabled") {
            bool value = false;
            if (!ParseBool(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be on or off.\r\n";
                return false;
            }
            Settings.WebServer.Enabled(value);
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
            if (value == Settings.Webhooks.Port()) {
                output = "Port must be different from the Webhooks port.\r\n";
                return false;
            }
            Settings.WebServer.Port(value);
            if (!SaveChange(snapshot, "Port", String(Settings.WebServer.Port()), output)) return false;
            output += "Restart required to apply the change.\r\n";
            return true;
        }

        if (command == "idle-timeout") {
            uint32_t value = 0;
            if (!ParseIdleTimeout(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be milliseconds (0-86400000), or 'off' to disable.\r\n";
                return false;
            }
            Settings.WebServer.IdleTimeoutMs(value);
            if (!SaveChange(snapshot, "Idle Timeout", IdleTimeoutDisplay(value), output)) return false;
            HTTPServer.IdleTimeout(value);
            output += "Applied immediately.\r\n";
            return true;
        }

        if (command == "max-sessions") {
            uint16_t value = 0;
            if (!ParseUInt16(parameters[1], value, false) || !parameters[2].isEmpty() ||
                value < httpserver::MIN_SESSIONS || value > httpserver::MAX_SESSIONS) {
                output = "Value must be " + String(httpserver::MIN_SESSIONS) + "-" + String(httpserver::MAX_SESSIONS) + ".\r\n";
                return false;
            }
            Settings.WebServer.MaxSessions(static_cast<uint8_t>(value));
            if (!SaveChange(snapshot, "Max Sessions", String(Settings.WebServer.MaxSessions()), output)) return false;
            HTTPServer.MaxSessions(value);
            output += "Applied immediately.\r\n";
            return true;
        }

        return Usage(output);
    }
}

bool cli::RegisterWebCommands() {
    return TelnetServer.OnCommand(
        "web",
        "Show and configure the web server\r\n\r\n"
        "web [show]\r\n"
        "web enabled [on|off]\r\n"
        "web port [value]\r\n"
        "web idle-timeout [milliseconds|off]\r\n"
        "web max-sessions [value]\r\n"
        "Omit a value to show it. Enabled/port changes require an administrative\r\n"
        "session and a restart. Idle-timeout and max-sessions apply immediately.",
        [](WiFiClient& client, String* parameters) {
            const bool mutation = IsWebMutation(parameters);
            if (mutation && !TelnetServer.IsSessionAdmin(client)) {
                Logger.Log("CLI web mutation denied for " + client.remoteIP().toString(), logger::LogLevels::Warning);
                client.write(telnetserver::FormatLine("Web", "Permission denied.").c_str());
                return;
            }

            String output;
            output.reserve(512);
            const bool success = ExecuteWebCommand(parameters, output);
            if (mutation) {
                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                Logger.Log(
                    "CLI web mutation " + String(success ? "accepted" : "rejected") + " for " + identity + "@" +
                        client.remoteIP().toString() + ": web " + parameters[0],
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );
            }
            const String formatted = telnetserver::FormatBlock("Web", output);
            client.write(reinterpret_cast<const uint8_t*>(formatted.c_str()), formatted.length());
        },
        false
    );
}
