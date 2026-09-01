#include "UserCommands.h"

#include "CommandHelpers.h"
#include "core/Globals.h"
#include "core/Users.h"

namespace {
    const char* UserReturnMessage(UserReturn result) {
        switch (result) {
            case UserReturn::NoError: return "ok";
            case UserReturn::UserExists: return "a user with that name already exists";
            case UserReturn::UserNotFound: return "user not found";
            case UserReturn::MaxUsersReached: return "maximum number of users reached";
            case UserReturn::NoAdminRemaining: return "at least one admin user must remain";
            case UserReturn::InvalidUsername: return "invalid username (3-32 characters: lowercase letters, digits, '.', '_', '-')";
            case UserReturn::PasswordError: return "invalid password (8-64 characters) or unable to hash it";
            case UserReturn::SynchronizationError: return "user store temporarily unavailable";
            default: return "unexpected error";
        }
    }

    bool ParseAdminFlag(const String& parameter, bool& admin) {
        if (parameter.isEmpty()) {
            admin = false;
            return true;
        }

        const int separator = parameter.indexOf('=');
        if (separator <= 0) return false;

        String key = parameter.substring(0, separator);
        key.trim();
        key.toLowerCase();
        if (key != "admin") return false;

        return cli::ParseBool(parameter.substring(separator + 1), admin);
    }

    String Lowercased(const String& value) {
        String result = value;
        result.toLowerCase();
        return result;
    }

    // User accounts take effect immediately (Settings.Users is the live
    // authentication store, not a separate runtime copy applied at boot).
    // Rolling back an already-applied mutation on a Save() failure isn't
    // generally possible here (a removed password's hash can't be
    // reconstructed), so failures are reported rather than reverted.
    void ReportPersistence(String& output) {
        if (!Settings.Save()) {
            output += "Warning: could not save to disk; this change will be lost on restart.\r\n";
        }
    }

    void ShowAll(String& output) {
        Settings.Users.ForEachStored([&output](const String& username, bool admin, const uint8_t*, const uint8_t*) {
            cli::AppendSetting(output, username, admin ? "admin" : "user");
        });
    }

    bool Usage(String& output) {
        output = "Usage: user [list|add|remove|rename|set-admin|set-password]\r\n";
        return false;
    }

    bool IsUserMutation(const String& command) {
        return command == "add" || command == "remove" || command == "rename" ||
            command == "set-admin" || command == "set-password";
    }

    bool ExecuteUserCommand(String* parameters, String& output) {
        output.clear();
        String command = parameters[0];
        command.toLowerCase();

        if (command.isEmpty() || command == "list" || command == "show") {
            if (!parameters[1].isEmpty()) return Usage(output);
            ShowAll(output);
            return true;
        }

        if (command == "add") {
            if (parameters[1].isEmpty() || parameters[2].isEmpty() || !parameters[4].isEmpty()) {
                output = "Usage: user add username password [admin=true|false]\r\n";
                return false;
            }

            bool admin = false;
            if (!ParseAdminFlag(parameters[3], admin)) {
                output = "Invalid admin flag. Expected admin=true or admin=false.\r\n";
                return false;
            }

            const UserReturn result = Settings.Users.Add(parameters[1], parameters[2], admin);
            if (result != UserReturn::NoError) {
                output = "Unable to add user: " + String(UserReturnMessage(result)) + ".\r\n";
                return false;
            }

            output = "User '" + Lowercased(parameters[1]) + "' added.\r\n";
            ReportPersistence(output);
            return true;
        }

        if (command == "remove") {
            if (parameters[1].isEmpty() || !parameters[2].isEmpty()) {
                output = "Usage: user remove username\r\n";
                return false;
            }

            const UserReturn result = Settings.Users.Remove(parameters[1]);
            if (result != UserReturn::NoError) {
                output = "Unable to remove user: " + String(UserReturnMessage(result)) + ".\r\n";
                return false;
            }

            output = "User '" + Lowercased(parameters[1]) + "' removed.\r\n";
            ReportPersistence(output);
            return true;
        }

        if (command == "rename") {
            if (parameters[1].isEmpty() || parameters[2].isEmpty() || !parameters[3].isEmpty()) {
                output = "Usage: user rename username newusername\r\n";
                return false;
            }

            const UserReturn result = Settings.Users.Rename(parameters[1], parameters[2]);
            if (result != UserReturn::NoError) {
                output = "Unable to rename user: " + String(UserReturnMessage(result)) + ".\r\n";
                return false;
            }

            output = "User renamed to '" + Lowercased(parameters[2]) + "'.\r\n";
            ReportPersistence(output);
            return true;
        }

        if (command == "set-admin") {
            bool admin = false;
            if (parameters[1].isEmpty() || !cli::ParseBool(parameters[2], admin) || !parameters[3].isEmpty()) {
                output = "Usage: user set-admin username on|off\r\n";
                return false;
            }

            const UserReturn result = Settings.Users.SetAdmin(parameters[1], admin);
            if (result != UserReturn::NoError) {
                output = "Unable to change admin flag: " + String(UserReturnMessage(result)) + ".\r\n";
                return false;
            }

            output = "User '" + Lowercased(parameters[1]) + "' admin flag set to " + cli::BoolValue(admin) + ".\r\n";
            ReportPersistence(output);
            return true;
        }

        if (command == "set-password") {
            if (parameters[1].isEmpty() || parameters[2].isEmpty() || !parameters[3].isEmpty()) {
                output = "Usage: user set-password username newpassword\r\n";
                return false;
            }

            const UserReturn result = Settings.Users.SetPassword(parameters[1], parameters[2]);
            if (result != UserReturn::NoError) {
                output = "Unable to change password: " + String(UserReturnMessage(result)) + ".\r\n";
                return false;
            }

            output = "Password updated for user '" + Lowercased(parameters[1]) + "'.\r\n";
            ReportPersistence(output);
            return true;
        }

        return Usage(output);
    }
}

bool cli::RegisterUserCommands() {
    const bool logonRegistered = TelnetServer.OnCommand("logon", "Log into the system with specific credentials\r\n\r\nlogon [username] [password]", [](WiFiClient& client, String* parameter) {
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

    const bool userRegistered = TelnetServer.OnCommand(
        "user",
        "Manage user accounts\r\n\r\n"
        "user [list]\r\n"
        "user add username password [admin=true|false]\r\n"
        "user remove username\r\n"
        "user rename username newusername\r\n"
        "user set-admin username on|off\r\n"
        "user set-password username newpassword",
        [](WiFiClient& client, String* parameters) {
            String subcommand = parameters[0];
            subcommand.toLowerCase();

            String output;
            output.reserve(256);
            const bool success = ExecuteUserCommand(parameters, output);

            if (IsUserMutation(subcommand)) {
                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                Logger.Log(
                    "CLI user mutation " + String(success ? "accepted" : "rejected") + " for " + identity + "@" +
                        client.remoteIP().toString() + ": user " + subcommand + (parameters[1].isEmpty() ? "" : " " + parameters[1]),
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );
            }

            const String formatted = telnetserver::FormatBlock("User", output);
            client.write(reinterpret_cast<const uint8_t*>(formatted.c_str()), formatted.length());
        },
        true
    );

    return logonRegistered && userRegistered;
}
