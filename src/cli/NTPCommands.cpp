#include "NTPCommands.h"

#include <cstdlib>

#include "CommandHelpers.h"
#include "core/Globals.h"

namespace {
    using cli::AppendSetting;
    using cli::BoolValue;
    using cli::ParseBool;

    struct NTPSettingsSnapshot {
        bool enabled;
        String server;
        int8_t timeZone;
    };

    NTPSettingsSnapshot CaptureSettings() {
        return {
            Settings.General.NTPUpdate(),
            Settings.General.NTPServer(),
            Settings.General.TimeZone()
        };
    }

    void RestoreSettings(const NTPSettingsSnapshot& snapshot) {
        Settings.General.NTPUpdate(snapshot.enabled);
        Settings.General.NTPServer(snapshot.server);
        Settings.General.TimeZone(snapshot.timeZone);
    }

    bool SaveChange(const NTPSettingsSnapshot& snapshot, const String& name, const String& value, String& output, bool appliesImmediately) {
        if (!Settings.Save()) {
            RestoreSettings(snapshot);
            output = "Unable to save NTP configuration. No changes were kept.\r\n";
            return false;
        }

        AppendSetting(output, name, value);
        output += "Configuration saved to " + String(Defaults.ConfigFileName) + ".\r\n";
        output += appliesImmediately ? "Applied immediately.\r\n" : "Restart required to apply the change.\r\n";
        return true;
    }

    bool Usage(String& output) {
        output = "Usage: ntp [show|enabled|server|time-zone]\r\n";
        return false;
    }

    bool IsValidNTPServer(const String& value) {
        if (value.length() < 3 || value.length() > 128) return false;
        for (size_t index = 0; index < value.length(); ++index) {
            const char character = value.charAt(index);
            const bool ok = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') || character == '.' || character == '-';
            if (!ok) return false;
        }
        return true;
    }

    bool ParseTimeZone(const String& value, int& parsed) {
        if (value.isEmpty()) return false;
        char* end = nullptr;
        const long numeric = std::strtol(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0' || numeric < -12 || numeric > 14) return false;
        parsed = static_cast<int>(numeric);
        return true;
    }

    void ShowAll(String& output) {
        AppendSetting(output, "Enabled", BoolValue(Settings.General.NTPUpdate()));
        AppendSetting(output, "Server", Settings.General.NTPServer());
        AppendSetting(output, "Time Zone", String(Settings.General.TimeZone()));
    }

    bool IsNTPMutation(String* parameters) noexcept {
        String command = parameters[0];
        command.toLowerCase();
        if (command == "show" || command.isEmpty()) return false;
        return !parameters[1].isEmpty();
    }

    bool ExecuteNTPCommand(String* parameters, String& output) {
        output.clear();
        String command = parameters[0];
        command.toLowerCase();

        if (command.isEmpty() || command == "show") {
            if (!parameters[1].isEmpty()) return Usage(output);
            ShowAll(output);
            return true;
        }

        if (parameters[1].isEmpty()) {
            if (command == "enabled") AppendSetting(output, "Enabled", BoolValue(Settings.General.NTPUpdate()));
            else if (command == "server") AppendSetting(output, "Server", Settings.General.NTPServer());
            else if (command == "time-zone") AppendSetting(output, "Time Zone", String(Settings.General.TimeZone()));
            else return Usage(output);
            return true;
        }

        const NTPSettingsSnapshot snapshot = CaptureSettings();

        if (command == "enabled") {
            bool value = false;
            if (!ParseBool(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be on or off.\r\n";
                return false;
            }
            Settings.General.NTPUpdate(value);
            return SaveChange(snapshot, "Enabled", BoolValue(value), output, false);
        }

        if (command == "server") {
            if (!parameters[2].isEmpty()) {
                output = "Usage: ntp server [hostname]\r\n";
                return false;
            }
            if (!IsValidNTPServer(parameters[1])) {
                output = "Server must be 3-128 characters: letters, digits, '.', '-', no spaces.\r\n";
                return false;
            }
            Settings.General.NTPServer(parameters[1]);
            // NTPServer() self-validates and can silently fall back to the
            // default; report what was actually stored, not what was typed.
            return SaveChange(snapshot, "Server", Settings.General.NTPServer(), output, true);
        }

        if (command == "time-zone") {
            int value = 0;
            if (!ParseTimeZone(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be an integer between -12 and 14.\r\n";
                return false;
            }
            Settings.General.TimeZone(value);
            if (!SaveChange(snapshot, "Time Zone", String(Settings.General.TimeZone()), output, true)) return false;
            Clock.TimeZone(static_cast<int8_t>(Settings.General.TimeZone()));
            return true;
        }

        return Usage(output);
    }
}

bool cli::RegisterNTPCommands() {
    return TelnetServer.OnCommand(
        "ntp",
        "Show and configure the clock and NTP synchronization\r\n\r\n"
        "ntp [show]\r\n"
        "ntp enabled [on|off]\r\n"
        "ntp server [hostname]\r\n"
        "ntp time-zone [-12..14]\r\n"
        "Omit a value to show it. Server and time-zone apply immediately;\r\n"
        "enabled requires an administrative session and a restart.",
        [](WiFiClient& client, String* parameters) {
            const bool mutation = IsNTPMutation(parameters);
            if (mutation && !TelnetServer.IsSessionAdmin(client)) {
                Logger.Log("CLI ntp mutation denied for " + client.remoteIP().toString(), logger::LogLevels::Warning);
                client.write(telnetserver::FormatLine("NTP", "Permission denied.").c_str());
                return;
            }

            String output;
            output.reserve(256);
            const bool success = ExecuteNTPCommand(parameters, output);
            if (mutation) {
                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                Logger.Log(
                    "CLI ntp mutation " + String(success ? "accepted" : "rejected") + " for " + identity + "@" +
                        client.remoteIP().toString() + ": ntp " + parameters[0],
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );
            }
            const String formatted = telnetserver::FormatBlock("NTP", output);
            client.write(reinterpret_cast<const uint8_t*>(formatted.c_str()), formatted.length());
        },
        false
    );
}
