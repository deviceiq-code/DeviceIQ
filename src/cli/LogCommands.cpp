#include "LogCommands.h"

#include <cstdlib>

#include "CommandHelpers.h"
#include "core/Globals.h"

namespace {
    using cli::AppendSetting;
    using cli::ParseUInt16;

    struct LogSettingsSnapshot {
        uint8_t endpoint;
        uint8_t level;
        String syslogServer;
        uint16_t syslogPort;
    };

    LogSettingsSnapshot CaptureSettings() {
        return {
            Settings.Log.Endpoint(),
            Settings.Log.LogLevel(),
            Settings.Log.SyslogServerHost(),
            Settings.Log.SyslogServerPort()
        };
    }

    void RestoreSettings(const LogSettingsSnapshot& snapshot) {
        Settings.Log.Endpoint(snapshot.endpoint);
        Settings.Log.LogLevel(snapshot.level);
        Settings.Log.SyslogServerHost(snapshot.syslogServer);
        Settings.Log.SyslogServerPort(snapshot.syslogPort);
    }

    // Settings are persisted but not applied to the running Logger instance:
    // its endpoint/level/syslog fields are read from the Logger task without
    // synchronization, so mutating them live from the Telnet task could race
    // with a log delivery in progress. A restart applies them safely.
    bool SaveChange(const LogSettingsSnapshot& snapshot, const String& name, const String& value, String& output) {
        if (!Settings.Save()) {
            RestoreSettings(snapshot);
            output = "Unable to save log configuration. No changes were kept.\r\n";
            return false;
        }

        AppendSetting(output, name, value);
        output += "Configuration saved to " + String(Defaults.ConfigFileName) + ".\r\n";
        output += "Restart required to apply the change.\r\n";
        return true;
    }

    bool Usage(String& output) {
        output = "Usage: log [show|view|clear|endpoint|level|syslog-server|syslog-port]\r\n";
        return false;
    }

    String EndpointNames(uint8_t endpoint) {
        if (endpoint == logger::Endpoints::NoLog) return "none";
        String names;
        if ((endpoint & logger::Endpoints::Serial) != 0) names += "serial";
        if ((endpoint & logger::Endpoints::Syslog) != 0) names += (names.isEmpty() ? "" : ",") + String("syslog");
        if ((endpoint & logger::Endpoints::File) != 0) names += (names.isEmpty() ? "" : ",") + String("file");
        return names.isEmpty() ? "none" : names;
    }

    String LevelNames(uint8_t level) {
        if (level == logger::LogLevels::All) return "all";
        if (level == 0) return "none";
        String names;
        if ((level & logger::LogLevels::Error) != 0) names += "error";
        if ((level & logger::LogLevels::Warning) != 0) names += (names.isEmpty() ? "" : ",") + String("warning");
        if ((level & logger::LogLevels::Information) != 0) names += (names.isEmpty() ? "" : ",") + String("information");
        if ((level & logger::LogLevels::Debug) != 0) names += (names.isEmpty() ? "" : ",") + String("debug");
        return names.isEmpty() ? "none" : names;
    }

    bool ParseEndpoint(String value, uint8_t& endpoint) {
        value.trim();
        value.toLowerCase();
        char* end = nullptr;
        const unsigned long numeric = std::strtoul(value.c_str(), &end, 10);
        if (end != value.c_str() && *end == '\0') {
            if (numeric > (logger::Endpoints::Serial | logger::Endpoints::Syslog | logger::Endpoints::File)) return false;
            endpoint = static_cast<uint8_t>(numeric);
            return true;
        }

        value.replace("+", ",");
        value.replace("|", ",");
        endpoint = 0;
        size_t offset = 0;
        while (offset < value.length()) {
            const int separator = value.indexOf(',', offset);
            String token = separator < 0 ? value.substring(offset) : value.substring(offset, static_cast<size_t>(separator));
            token.trim();
            if (token == "none") {
                if (value != "none") return false;
                endpoint = logger::Endpoints::NoLog;
            } else if (token == "all") {
                if (value != "all") return false;
                endpoint = logger::Endpoints::Serial | logger::Endpoints::Syslog | logger::Endpoints::File;
            } else if (token == "serial") endpoint |= logger::Endpoints::Serial;
            else if (token == "syslog") endpoint |= logger::Endpoints::Syslog;
            else if (token == "file") endpoint |= logger::Endpoints::File;
            else return false;
            if (separator < 0) break;
            offset = static_cast<size_t>(separator) + 1;
        }
        return !value.isEmpty();
    }

    bool ParseLevel(String value, uint8_t& level) {
        value.trim();
        value.toLowerCase();
        char* end = nullptr;
        const unsigned long numeric = std::strtoul(value.c_str(), &end, 10);
        if (end != value.c_str() && *end == '\0') {
            if (numeric > 15 && numeric != logger::LogLevels::All) return false;
            level = static_cast<uint8_t>(numeric);
            return true;
        }

        value.replace("+", ",");
        value.replace("|", ",");
        level = 0;
        size_t offset = 0;
        while (offset < value.length()) {
            const int separator = value.indexOf(',', offset);
            String token = separator < 0 ? value.substring(offset) : value.substring(offset, static_cast<size_t>(separator));
            token.trim();
            if (token == "none") {
                if (value != "none") return false;
                level = 0;
            } else if (token == "all") {
                if (value != "all") return false;
                level = logger::LogLevels::All;
            } else if (token == "error") level |= logger::LogLevels::Error;
            else if (token == "warning" || token == "warn") level |= logger::LogLevels::Warning;
            else if (token == "information" || token == "info") level |= logger::LogLevels::Information;
            else if (token == "debug") level |= logger::LogLevels::Debug;
            else return false;
            if (separator < 0) break;
            offset = static_cast<size_t>(separator) + 1;
        }
        return !value.isEmpty();
    }

    void ShowAll(String& output) {
        AppendSetting(output, "Endpoint", EndpointNames(Settings.Log.Endpoint()));
        AppendSetting(output, "Level", LevelNames(Settings.Log.LogLevel()));
        AppendSetting(output, "Syslog Server", Settings.Log.SyslogServerHost().isEmpty() ? "not set" : Settings.Log.SyslogServerHost());
        AppendSetting(output, "Syslog Port", String(Settings.Log.SyslogServerPort()));
    }

    bool ExecuteSettingsCommand(String* parameters, String& output) {
        output.clear();
        String command = parameters[0];
        command.toLowerCase();

        if (command.isEmpty() || command == "show") {
            if (!parameters[1].isEmpty()) return Usage(output);
            ShowAll(output);
            return true;
        }

        if (parameters[1].isEmpty()) {
            if (command == "endpoint") AppendSetting(output, "Endpoint", EndpointNames(Settings.Log.Endpoint()));
            else if (command == "level") AppendSetting(output, "Level", LevelNames(Settings.Log.LogLevel()));
            else if (command == "syslog-server") AppendSetting(output, "Syslog Server", Settings.Log.SyslogServerHost().isEmpty() ? "not set" : Settings.Log.SyslogServerHost());
            else if (command == "syslog-port") AppendSetting(output, "Syslog Port", String(Settings.Log.SyslogServerPort()));
            else return Usage(output);
            return true;
        }

        const LogSettingsSnapshot snapshot = CaptureSettings();

        if (command == "endpoint") {
            uint8_t value = 0;
            if (!ParseEndpoint(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be a comma-separated list of serial,syslog,file (or none/all), or a numeric bitmask 0-7.\r\n";
                return false;
            }
            Settings.Log.Endpoint(value);
            return SaveChange(snapshot, "Endpoint", EndpointNames(Settings.Log.Endpoint()), output);
        }

        if (command == "level") {
            uint8_t value = 0;
            if (!ParseLevel(parameters[1], value) || !parameters[2].isEmpty()) {
                output = "Value must be a comma-separated list of error,warning,information,debug (or none/all), or a numeric bitmask 0-15.\r\n";
                return false;
            }
            Settings.Log.LogLevel(value);
            return SaveChange(snapshot, "Level", LevelNames(Settings.Log.LogLevel()), output);
        }

        if (command == "syslog-server") {
            if (!parameters[2].isEmpty()) {
                output = "Usage: log syslog-server [hostname|address|clear]\r\n";
                return false;
            }
            String value = parameters[1];
            if (value.equalsIgnoreCase("clear")) value.clear();
            Settings.Log.SyslogServerHost(value);
            return SaveChange(snapshot, "Syslog Server", Settings.Log.SyslogServerHost().isEmpty() ? "not set" : Settings.Log.SyslogServerHost(), output);
        }

        if (command == "syslog-port") {
            uint16_t value = 0;
            if (!ParseUInt16(parameters[1], value, false) || !parameters[2].isEmpty()) {
                output = "Value must be 1-65535.\r\n";
                return false;
            }
            Settings.Log.SyslogServerPort(value);
            return SaveChange(snapshot, "Syslog Port", String(Settings.Log.SyslogServerPort()), output);
        }

        return Usage(output);
    }
}

bool cli::RegisterLogCommands() {
    return TelnetServer.OnCommand(
        "log",
        "View, clear, or configure the device log\r\n\r\n"
        "log [show]\r\n"
        "log view [line_count|all]\r\n"
        "log clear\r\n"
        "log endpoint [serial,syslog,file|none|all]\r\n"
        "log level [error,warning,information,debug|none|all]\r\n"
        "log syslog-server [hostname|address|clear]\r\n"
        "log syslog-port [value]\r\n"
        "Omit a value to show it. Endpoint/level/syslog-server/syslog-port\r\n"
        "changes require a restart.",
        [](WiFiClient& client, String* parameters) {
            String subcommand = parameters[0];
            subcommand.toLowerCase();

            if (subcommand == "clear") {
                if (!parameters[1].isEmpty()) {
                    client.write(telnetserver::FormatLine("Log", "Usage: log clear").c_str());
                    return;
                }

                const filesystem::Result result = FileSystem.Remove(Defaults.LogFileName);
                const bool success = result == filesystem::Result::Ok || result == filesystem::Result::NotFound;

                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                // Logged after clearing: if delivery to file is enabled, this
                // becomes the first entry of the fresh log, so clearing it
                // can never itself go unrecorded.
                Logger.Log(
                    "CLI log clear " + String(success ? "accepted" : "rejected") + " for " + identity + "@" + client.remoteIP().toString(),
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );

                client.write(telnetserver::FormatLine("Log", success ? "Cleared." : "Unable to clear the log file.").c_str());
                return;
            }

            if (subcommand == "view") {
                if (!parameters[2].isEmpty()) {
                    client.write(telnetserver::FormatLine("Log", "Usage: log view [line_count|all]").c_str());
                    return;
                }

                bool showAll = parameters[1].equalsIgnoreCase("all");
                uint32_t requestedLines = 10;
                if (!parameters[1].isEmpty() && !showAll) {
                    char* end = nullptr;
                    const unsigned long parsed = std::strtoul(parameters[1].c_str(), &end, 10);
                    if (end == parameters[1].c_str() || *end != '\0' || parsed == 0 || parsed > UINT16_MAX) {
                        client.write(telnetserver::FormatLine("Log", "Line count must be between 1 and 65535, or 'all'.").c_str());
                        return;
                    }
                    requestedLines = static_cast<uint32_t>(parsed);
                }

                String content;
                const filesystem::Result readResult = FileSystem.Read(Defaults.LogFileName, content);
                if (readResult == filesystem::Result::NotFound || (readResult == filesystem::Result::Ok && content.isEmpty())) {
                    client.write(telnetserver::FormatLine("Log", "Empty.").c_str());
                    return;
                }
                if (readResult != filesystem::Result::Ok) {
                    client.write(telnetserver::FormatLine("Log", "Unable to read the log file.").c_str());
                    return;
                }

                size_t start = 0;
                if (!showAll) {
                    size_t cursor = content.length();
                    while (cursor > 0 && (content[cursor - 1] == '\n' || content[cursor - 1] == '\r')) --cursor;
                    start = cursor;
                    uint32_t linesFound = 0;
                    while (start > 0) {
                        --start;
                        if (content[start] != '\n') continue;
                        ++linesFound;
                        if (linesFound == requestedLines) {
                            ++start;
                            break;
                        }
                    }
                }

                size_t contentEnd = content.length();
                while (contentEnd > start && (content[contentEnd - 1] == '\n' || content[contentEnd - 1] == '\r')) --contentEnd;
                uint32_t displayedLines = contentEnd > start ? 1 : 0;
                for (size_t index = start; index < contentEnd; ++index) {
                    if (content[index] == '\n') ++displayedLines;
                }

                const String description = showAll
                    ? "All lines (" + String(displayedLines) + ")"
                    : "Last " + String(displayedLines) + " line(s)";
                const String header = telnetserver::FormatLine("Log", description + ":") + "\r\n";
                client.write(reinterpret_cast<const uint8_t*>(header.c_str()), header.length());

                constexpr size_t ChunkSize = 512;
                for (size_t offset = start; offset < content.length() && client.connected(); offset += ChunkSize) {
                    const size_t remaining = content.length() - offset;
                    const size_t length = remaining < ChunkSize ? remaining : ChunkSize;
                    client.write(reinterpret_cast<const uint8_t*>(content.c_str() + offset), length);
                }
                return;
            }

            const bool mutation = !subcommand.isEmpty() && subcommand != "show" && !parameters[1].isEmpty();
            String output;
            output.reserve(384);
            const bool success = ExecuteSettingsCommand(parameters, output);
            if (mutation) {
                telnetserver::SessionInfo session;
                const String identity = TelnetServer.SessionInformation(client, session) ? session.user : String("unknown");
                Logger.Log(
                    "CLI log mutation " + String(success ? "accepted" : "rejected") + " for " + identity + "@" +
                        client.remoteIP().toString() + ": log " + subcommand,
                    success ? logger::LogLevels::Information : logger::LogLevels::Warning
                );
            }
            const String formatted = telnetserver::FormatBlock("Log", output);
            client.write(reinterpret_cast<const uint8_t*>(formatted.c_str()), formatted.length());
        },
        true
    );
}
