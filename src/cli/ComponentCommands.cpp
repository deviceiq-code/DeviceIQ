#include "ComponentCommands.h"

#include "core/Globals.h"

bool cli::RegisterComponentCommands() {
    return TelnetServer.OnCommand(
        "comp",
        "Manage components\r\n\r\n"
        "comp list\r\n"
        "comp tree\r\n"
        "comp status [component_name|#component_id]\r\n"
        "comp set [component_name|#component_id] property=value\r\n"
        "comp trigger [component_name|#component_id] event [value=integer]\r\n"
        "comp rename [component_name|#component_id] name=newname\r\n"
        "comp event [component_name|#component_id] eventName script\r\n"
        "comp remove [component_name|#component_id]\r\n"
        "comp add relay|button|thermometer|blinds name=value [id=value] ...\r\n"
        "Blinds member references use numeric component IDs.",
        [](WiFiClient& client, String* parameters) {
            String subcommand = parameters[0];
            subcommand.toLowerCase();
            const bool mutation = subcommand == "set" || subcommand == "trigger" || subcommand == "rename" ||
                subcommand == "event" || subcommand == "remove" || subcommand == "add";

            if (mutation && !TelnetServer.IsSessionAdmin(client)) {
                Logger.Log("CLI component mutation denied for " + client.remoteIP().toString() + ": comp " + subcommand, logger::LogLevels::Warning);
                client.write(telnetserver::FormatLine("Component", "Permission denied.").c_str());
                return;
            }

            String output;
            output.reserve(768);
            const bool success = Settings.ExecuteComponentCommand(parameters, output);
            if (mutation) {
                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                Logger.Log(
                    "CLI component mutation " + String(success ? "accepted" : "rejected") + " for " + identity + "@" +
                        client.remoteIP().toString() + ": comp " + subcommand,
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );
            }
            const String formatted = telnetserver::FormatBlock("Component", output);
            client.write(reinterpret_cast<const uint8_t*>(formatted.c_str()), formatted.length());
        },
        false
    );
}
