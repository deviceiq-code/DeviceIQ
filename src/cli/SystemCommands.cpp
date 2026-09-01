#include "SystemCommands.h"

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

    return rebootRegistered && dumpcfgRegistered && hwinfoRegistered && memRegistered && fsRegistered && versionRegistered;
}
