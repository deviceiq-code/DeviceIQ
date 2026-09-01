#include "SystemCommands.h"

#include <cstdlib>

#include "core/Globals.h"
#include "core/SystemInfo.h"

namespace {
    void DeviceRestart() {
        if (!Settings.SaveComponentsState()) {
            Logger.Log("Error saving component state before restart", logger::LogLevels::Error);
        }

        Logger.Log("Device restart requested", logger::LogLevels::Information);
        vTaskDelay(pdMS_TO_TICKS(250));
        ESP.restart();
    }
}

bool cli::RegisterSystemCommands() {
    const bool rebootRegistered = TelnetServer.OnCommand("reboot", "Reboot the device\r\n\r\nreboot", [](WiFiClient& client, String*) {
        client.write(telnetserver::FormatLine("System", String(Version::ProductFamily) + " is rebooting...").c_str());
        client.stop();
        DeviceRestart();
    }, true);

    const bool dumpcfgRegistered = TelnetServer.OnCommand("dumpcfg", "Prints the configuration file\r\n\r\ndumpcfg", [](WiFiClient& client, String*) {
        const String path = Defaults.ConfigFileName;
        String content;

        if (FileSystem.Read(path.c_str(), content) != filesystem::Result::Ok) {
            const String error = telnetserver::FormatLine("Config", "Error reading config file '" + path + "'.");
            client.write(error.c_str());
            return;
        }

        const String header = telnetserver::FormatLine("Config", "File: " + path) +
            telnetserver::FormatLine("", "Content:") + "\r\n";
        client.write(header.c_str());

        constexpr size_t ChunkSize = 512;
        for (size_t offset = 0; offset < content.length() && client.connected(); offset += ChunkSize) {
            const size_t remaining = content.length() - offset;
            const size_t length = remaining < ChunkSize ? remaining : ChunkSize;
            client.write(reinterpret_cast<const uint8_t*>(content.c_str() + offset), length);
        }
    }, true);

    const bool logRegistered = TelnetServer.OnCommand(
        "log",
        "View or clear the device log\r\n\r\n"
        "log view\r\n"
        "log view [line_count]\r\n"
        "log view all\r\n"
        "log clear",
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
                client.write(telnetserver::FormatLine("Log", success ? "Cleared." : "Unable to clear the log file.").c_str());
                return;
            }

            if (subcommand != "view" || !parameters[2].isEmpty()) {
                client.write(telnetserver::FormatLine("Log", "Usage: log view [line_count|all] | log clear").c_str());
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
        },
        true
    );

    const bool hwinfoRegistered = TelnetServer.OnCommand("hwinfo", "Show ESP32 hardware information\r\n\r\nhwinfo", [](WiFiClient& client, String*) {
        String output;
        SystemInfo::Hardware(output);
        client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    }, false);

    const bool memRegistered = TelnetServer.OnCommand("mem", "Show memory usage\r\n\r\nmem [b|kb|mb]", [](WiFiClient& client, String* parameters) {
        String parameter = parameters[0];
        parameter.toLowerCase();

        SystemInfo::MemoryUnit unit = SystemInfo::MemoryUnit::Bytes;
        if (parameter.isEmpty() || parameter == "b") {
            unit = SystemInfo::MemoryUnit::Bytes;
        } else if (parameter == "kb") {
            unit = SystemInfo::MemoryUnit::Kilobytes;
        } else if (parameter == "mb") {
            unit = SystemInfo::MemoryUnit::Megabytes;
        } else {
            client.write(telnetserver::FormatLine("Memory", "Usage: mem [b|kb|mb]").c_str());
            return;
        }

        String output;
        SystemInfo::Memory(output, unit);
        client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    }, true);

    const bool fsRegistered = TelnetServer.OnCommand("fs", "Show filesystem information\r\n\r\nfs", [](WiFiClient& client, String*) {
        String output;
        (void)SystemInfo::FileSystem(output);
        client.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());
    }, true);

    const bool versionRegistered = TelnetServer.OnCommand("ver", "Show device version info\r\n\r\nver", [](WiFiClient& client, String*) {
        String result;
        result += "Version        | Product: " + String(Version::ProductName) + "\r\n";
        result += "               | Family: " + String(Version::ProductFamily) + "\r\n";
        result += "               | Serial: " + Version::SerialNumber() + "\r\n";
        result += "               | Hardware: " + Version::Hardware::Info() + "\r\n";
        result += "               | Software: " + Version::Software::Info() + "\r\n";
        client.write(result.c_str());
    }, false);

    return rebootRegistered && dumpcfgRegistered && logRegistered && hwinfoRegistered && memRegistered && fsRegistered && versionRegistered;
}
