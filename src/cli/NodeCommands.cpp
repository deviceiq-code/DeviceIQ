#include "NodeCommands.h"

#include "CommandHelpers.h"
#include "core/Globals.h"

namespace {
    using cli::AppendSetting;
    using cli::BoolValue;
    using cli::ParseBool;

    bool Usage(String& output) {
        output = "Usage: nodes [list|enabled|discover|adopt|replace|identify|rename|remove]\r\n";
        return false;
    }

    void ShowAll(String& output) {
        AppendSetting(output, "Enabled", BoolValue(Settings.EspNow.Enabled()));
        AppendSetting(output, "Running", BoolValue(EspNow.Running()));
        AppendSetting(output, "Channel", String(EspNow.Channel()));
        AppendSetting(output, "Discovery", String(EspNow.DiscoverySecondsLeft()) + "s left");

        espnowbus::NodeInfo node;
        for (size_t index = 0; EspNow.GetNode(index, node); ++index) {
            output += node.name + " " + node.mac + " [" + (node.online ? "online" : "offline") + "]" +
                (node.configRejected ? " [config rejected]" : "") + "\r\n";
        }

        espnowbus::PendingInfo device;
        for (size_t index = 0; EspNow.GetPending(index, device); ++index) {
            output += "Waiting for adoption: " + device.mac + "\r\n";
        }

        String message;
        if (EspNow.Adoption(message) != espnowbus::AdoptionState::Idle) output += message + "\r\n";
    }

    bool IsNodesMutation(String* parameters) noexcept {
        String command = parameters[0];
        command.toLowerCase();
        if (command.isEmpty() || command == "show" || command == "list") return false;
        return command != "enabled" || !parameters[1].isEmpty();
    }

    bool ExecuteNodesCommand(String* parameters, String& output) {
        output.clear();
        String command = parameters[0];
        command.toLowerCase();

        if (command.isEmpty() || command == "show" || command == "list") {
            if (!parameters[1].isEmpty()) return Usage(output);
            ShowAll(output);
            return true;
        }

        if (command == "enabled") {
            if (parameters[1].isEmpty()) {
                AppendSetting(output, "Enabled", BoolValue(Settings.EspNow.Enabled()));
                return true;
            }
            bool value = false;
            if (!ParseBool(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be on or off.\r\n";
                return false;
            }

            const bool previous = Settings.EspNow.Enabled();
            Settings.EspNow.Enabled(value);
            if (!Settings.Save()) {
                Settings.EspNow.Enabled(previous);
                output = "Unable to save configuration. No changes were kept.\r\n";
                return false;
            }
            AppendSetting(output, "Enabled", BoolValue(value));
            output += "Restart required to apply the change.\r\n";
            return true;
        }

        if (command == "discover") {
            bool open = false;
            if (!ParseBool(parameters[1], open) || !parameters[2].isEmpty()) {
                output = "Usage: nodes discover on|off\r\n";
                return false;
            }
            if (!EspNow.Running()) {
                output = "ESP-NOW is not running. Enable it and restart.\r\n";
                return false;
            }
            if (open) EspNow.OpenDiscovery();
            else EspNow.CloseDiscovery();
            output = open ? "Discovery open.\r\n" : "Discovery closed.\r\n";
            return true;
        }

        String error;
        bool success = false;
        if (command == "adopt" && !parameters[2].isEmpty() && parameters[3].isEmpty()) {
            success = EspNow.Adopt(parameters[1], parameters[2], error);
            if (success) output = "Adoption started. Use 'nodes' to follow it.\r\n";
        } else if (command == "replace" && !parameters[2].isEmpty() && parameters[3].isEmpty()) {
            success = EspNow.Replace(parameters[1], parameters[2], error);
            if (success) output = "Replacement started. Use 'nodes' to follow it.\r\n";
        } else if (command == "identify" && !parameters[1].isEmpty() && parameters[2].isEmpty()) {
            success = EspNow.Identify(parameters[1], error);
            if (success) output = "Identify requested.\r\n";
        } else if (command == "rename" && !parameters[2].isEmpty() && parameters[3].isEmpty()) {
            success = EspNow.Rename(parameters[1], parameters[2], error);
            if (success) output = "Node renamed. Restart required for components to report the new name.\r\n";
        } else if (command == "remove" && !parameters[1].isEmpty() && parameters[2].isEmpty()) {
            success = EspNow.Remove(parameters[1], error);
            if (success) output = "Node removed.\r\n";
        } else {
            return Usage(output);
        }

        if (!success) output = error + "\r\n";
        return success;
    }
}

bool cli::RegisterNodeCommands() {
    return TelnetServer.OnCommand(
        "nodes",
        "Manage ESP-NOW nodes\r\n\r\n"
        "nodes [list]\r\n"
        "nodes enabled [on|off]\r\n"
        "nodes discover on|off\r\n"
        "nodes adopt mac name\r\n"
        "nodes replace name mac\r\n"
        "nodes identify name|mac\r\n"
        "nodes rename name newname\r\n"
        "nodes remove name\r\n"
        "Adoption needs discovery open and a node powered on in the last few\r\n"
        "minutes. Replace gives an existing node's name and components to a\r\n"
        "new board. Changes require an administrative session.",
        [](WiFiClient& client, String* parameters) {
            const bool mutation = IsNodesMutation(parameters);
            if (mutation && !TelnetServer.IsSessionAdmin(client)) {
                Logger.Log("CLI nodes mutation denied for " + client.remoteIP().toString(), logger::LogLevels::Warning);
                client.write(telnetserver::FormatLine("Nodes", "Permission denied.").c_str());
                return;
            }

            String output;
            output.reserve(384);
            const bool success = ExecuteNodesCommand(parameters, output);
            if (mutation) {
                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                Logger.Log(
                    "CLI nodes mutation " + String(success ? "accepted" : "rejected") + " for " + identity + "@" +
                        client.remoteIP().toString() + ": nodes " + parameters[0],
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );
            }
            const String formatted = telnetserver::FormatBlock("Nodes", output);
            client.write(reinterpret_cast<const uint8_t*>(formatted.c_str()), formatted.length());
        },
        false
    );
}
