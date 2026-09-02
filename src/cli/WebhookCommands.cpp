#include "WebhookCommands.h"

#include "CommandHelpers.h"
#include "core/Globals.h"

namespace {
    using cli::AppendSetting;
    using cli::BoolValue;
    using cli::ParseBool;
    using cli::ParseUInt16;
    using cli::PasswordState;

    struct WebhookSettingsSnapshot {
        bool enabled;
        String token;
        uint16_t port;
    };

    WebhookSettingsSnapshot CaptureSettings() {
        return {Settings.Webhooks.Enabled(), Settings.Webhooks.Token(), Settings.Webhooks.Port()};
    }

    void RestoreSettings(const WebhookSettingsSnapshot& snapshot) {
        Settings.Webhooks.Enabled(snapshot.enabled);
        Settings.Webhooks.Token(snapshot.token);
        Settings.Webhooks.Port(snapshot.port);
    }

    bool SaveChange(const WebhookSettingsSnapshot& snapshot, const String& name, const String& value, String& output) {
        if (!Settings.Save()) {
            RestoreSettings(snapshot);
            output = "Unable to save Webhooks configuration. No changes were kept.\r\n";
            return false;
        }

        AppendSetting(output, name, value);
        output += "Configuration saved to " + String(Defaults.ConfigFileName) + ".\r\n";
        output += "Restart required to apply the change.\r\n";
        return true;
    }

    bool Usage(String& output) {
        output = "Usage: webhooks [show|enabled|token|port]\r\n";
        return false;
    }

    bool IsValidToken(const String& value) {
        if (value.length() < 15 || value.length() > 30) return false;
        for (size_t index = 0; index < value.length(); ++index) {
            const char character = value.charAt(index);
            const bool ok = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
            if (!ok) return false;
        }
        return true;
    }

    void ShowAll(String& output, bool isAdmin) {
        AppendSetting(output, "Enabled", BoolValue(Settings.Webhooks.Enabled()));
        AppendSetting(output, "Token", isAdmin ? Settings.Webhooks.Token() : PasswordState(Settings.Webhooks.Token()));
        AppendSetting(output, "Port", String(Settings.Webhooks.Port()));
    }

    bool IsWebhooksMutation(String* parameters) noexcept {
        String command = parameters[0];
        command.toLowerCase();
        if (command == "show" || command.isEmpty()) return false;
        return !parameters[1].isEmpty();
    }

    bool ExecuteWebhooksCommand(String* parameters, bool isAdmin, String& output) {
        output.clear();
        String command = parameters[0];
        command.toLowerCase();

        if (command.isEmpty() || command == "show") {
            if (!parameters[1].isEmpty()) return Usage(output);
            ShowAll(output, isAdmin);
            return true;
        }

        if (parameters[1].isEmpty()) {
            if (command == "enabled") AppendSetting(output, "Enabled", BoolValue(Settings.Webhooks.Enabled()));
            else if (command == "token") AppendSetting(output, "Token", isAdmin ? Settings.Webhooks.Token() : PasswordState(Settings.Webhooks.Token()));
            else if (command == "port") AppendSetting(output, "Port", String(Settings.Webhooks.Port()));
            else return Usage(output);
            return true;
        }

        const WebhookSettingsSnapshot snapshot = CaptureSettings();

        if (command == "enabled") {
            bool value = false;
            if (!ParseBool(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be on or off.\r\n";
                return false;
            }
            Settings.Webhooks.Enabled(value);
            return SaveChange(snapshot, "Enabled", BoolValue(value), output);
        }

        if (command == "token") {
            if (!parameters[2].isEmpty() || !IsValidToken(parameters[1])) {
                output = "Token must be 15-30 letters and numbers.\r\n";
                return false;
            }
            Settings.Webhooks.Token(parameters[1]);
            // Reached only after the admin check for mutations, so it's safe
            // to echo the value that was just set.
            return SaveChange(snapshot, "Token", Settings.Webhooks.Token(), output);
        }

        if (command == "port") {
            uint16_t value = 0;
            if (!ParseUInt16(parameters[1], value, false) || !parameters[2].isEmpty()) {
                output = "Value must be 1-65535.\r\n";
                return false;
            }
            if (value == Settings.WebServer.Port()) {
                output = "Port must be different from the Web Server port.\r\n";
                return false;
            }
            Settings.Webhooks.Port(value);
            return SaveChange(snapshot, "Port", String(Settings.Webhooks.Port()), output);
        }

        return Usage(output);
    }
}

bool cli::RegisterWebhookCommands() {
    return TelnetServer.OnCommand(
        "webhooks",
        "Show and configure the webhooks server\r\n\r\n"
        "webhooks [show]\r\n"
        "webhooks enabled [on|off]\r\n"
        "webhooks token [value]\r\n"
        "webhooks port [value]\r\n"
        "Omit a value to show it. Changes require an administrative session\r\n"
        "and a restart. Token must be 15-30 letters/numbers. Port must differ\r\n"
        "from the Web Server port.",
        [](WiFiClient& client, String* parameters) {
            const bool mutation = IsWebhooksMutation(parameters);
            if (mutation && !TelnetServer.IsSessionAdmin(client)) {
                Logger.Log("CLI webhooks mutation denied for " + client.remoteIP().toString(), logger::LogLevels::Warning);
                client.write(telnetserver::FormatLine("Webhooks", "Permission denied.").c_str());
                return;
            }

            String output;
            output.reserve(384);
            const bool success = ExecuteWebhooksCommand(parameters, TelnetServer.IsSessionAdmin(client), output);
            if (mutation) {
                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                Logger.Log(
                    "CLI webhooks mutation " + String(success ? "accepted" : "rejected") + " for " + identity + "@" +
                        client.remoteIP().toString() + ": webhooks " + parameters[0],
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );
            }
            const String formatted = telnetserver::FormatBlock("Webhooks", output);
            client.write(reinterpret_cast<const uint8_t*>(formatted.c_str()), formatted.length());
        },
        false
    );
}
