#include "UserCommands.h"

#include "core/Globals.h"
#include "core/Users.h"

bool cli::RegisterUserCommands() {
    return TelnetServer.OnCommand("logon", "Log into the system with specific credentials\r\n\r\nlogon [username] [password]", [](WiFiClient& client, String* parameter) {
        String result;

        if (parameter[0].isEmpty() || parameter[1].isEmpty()) {
            result += "Logon          | Missing username and password.\r\n";
        } else {
            const UserReturn authentication = Settings.Users.Authenticate(parameter[0], parameter[1], client.remoteIP());

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
}
