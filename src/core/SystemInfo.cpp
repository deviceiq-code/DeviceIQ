#include "SystemInfo.h"

#include <esp_arduino_version.h>
#include <esp_system.h>
#include <esp32-hal-cpu.h>

#include "Globals.h"
#include "CLIFormat.h"

namespace {
    const char* FlashModeName(FlashMode_t mode) noexcept {
        switch (mode) {
            case FM_QIO: return "QIO";
            case FM_QOUT: return "QOUT";
            case FM_DIO: return "DIO";
            case FM_DOUT: return "DOUT";
            case FM_FAST_READ: return "Fast Read";
            case FM_SLOW_READ: return "Slow Read";
            default: return "Unknown";
        }
    }

    const char* ResetReasonName(esp_reset_reason_t reason) noexcept {
        switch (reason) {
            case ESP_RST_POWERON: return "Power on";
            case ESP_RST_EXT: return "External pin";
            case ESP_RST_SW: return "Software restart";
            case ESP_RST_PANIC: return "Exception/panic";
            case ESP_RST_INT_WDT: return "Interrupt watchdog";
            case ESP_RST_TASK_WDT: return "Task watchdog";
            case ESP_RST_WDT: return "Other watchdog";
            case ESP_RST_DEEPSLEEP: return "Deep-sleep wakeup";
            case ESP_RST_BROWNOUT: return "Brownout";
            case ESP_RST_SDIO: return "SDIO";
            default: return "Unknown";
        }
    }

    String FormatBytes(uint32_t bytes) {
        const char* unit = "B";
        double value = static_cast<double>(bytes);
        uint8_t decimals = 0;
        if (bytes >= 1024UL * 1024UL) {
            value /= 1024.0 * 1024.0;
            unit = "MiB";
            decimals = 2;
        } else if (bytes >= 1024UL) {
            value /= 1024.0;
            unit = "KiB";
            decimals = 2;
        }
        return String(value, static_cast<unsigned int>(decimals)) + " " + unit + " (" + String(bytes) + " bytes)";
    }

    String MacAddress(esp_mac_type_t type) {
        uint8_t mac[6]{};
        if (esp_read_mac(mac, type) != ESP_OK) return "Unavailable";
        char text[18];
        snprintf(
            text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
        );
        return String(text);
    }

    String Uptime() {
        uint64_t seconds = static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL;
        const uint32_t days = seconds / 86400ULL;
        seconds %= 86400ULL;
        const uint8_t hours = seconds / 3600ULL;
        seconds %= 3600ULL;
        const uint8_t minutes = seconds / 60ULL;
        const uint8_t remainingSeconds = seconds % 60ULL;
        char text[32];
        snprintf(text, sizeof(text), "%lu days, %02u:%02u:%02u", static_cast<unsigned long>(days), hours, minutes, remainingSeconds);
        return String(text);
    }

    String MemoryValue(uint32_t bytes, SystemInfo::MemoryUnit unit) {
        switch (unit) {
            case SystemInfo::MemoryUnit::Kilobytes: return String(bytes / 1024.0, 2);
            case SystemInfo::MemoryUnit::Megabytes: return String(bytes / (1024.0 * 1024.0), 2);
            default: return String(bytes);
        }
    }

    void AppendMemoryColumn(String& output, uint32_t bytes, SystemInfo::MemoryUnit unit) {
        const String value = MemoryValue(bytes, unit);
        for (size_t padding = value.length(); padding < 10; ++padding) output += ' ';
        output += value;
    }

    void AppendMemoryRow(String& output, const char* name, uint32_t total, uint32_t available, uint32_t minimum, uint32_t largest, SystemInfo::MemoryUnit unit) {
        const uint32_t used = total >= available ? total - available : 0;
        output += CLIFormat::ContinuationPrefix();
        output += name;
        for (size_t padding = strlen(name); padding < 11; ++padding) output += ' ';
        AppendMemoryColumn(output, total, unit);
        AppendMemoryColumn(output, used, unit);
        AppendMemoryColumn(output, available, unit);
        AppendMemoryColumn(output, minimum, unit);
        AppendMemoryColumn(output, largest, unit);
        output += "\r\n";
    }
}

void SystemInfo::Hardware(String& output) {
    output.reserve(1600);
    output += "Hardware       | Product: " + String(Version::ProductFamily) + " " + Version::ProductName + "\r\n";
    output += "               | Serial: " + Version::SerialNumber() + "\r\n";
    output += "               | Model: " + String(ESP.getChipModel()) + "\r\n";
    output += "               | Revision: " + String(ESP.getChipRevision()) + "\r\n";
    output += "               | CPU cores: " + String(ESP.getChipCores()) + "\r\n";
    output += "               | CPU frequency: " + String(ESP.getCpuFreqMHz()) + " MHz\r\n";
    output += "               | Crystal frequency: " + String(getXtalFrequencyMhz()) + " MHz\r\n";
    output += "               | APB frequency: " + String(getApbFrequency() / 1000000UL) + " MHz\r\n";
    output += "               | Internal temperature: " + String(temperatureRead(), 1) + " C\r\n";
    output += "               | WiFi MAC: " + MacAddress(ESP_MAC_WIFI_STA) + "\r\n";
    output += "               | Bluetooth MAC: " + MacAddress(ESP_MAC_BT) + "\r\n";
    output += "Flash          | Size: " + FormatBytes(ESP.getFlashChipSize()) + "\r\n";
    output += "               | Speed: " + String(ESP.getFlashChipSpeed() / 1000000UL) + " MHz\r\n";
    output += "               | Mode: " + String(FlashModeName(ESP.getFlashChipMode())) + "\r\n";
    output += "Firmware       | Sketch size: " + FormatBytes(ESP.getSketchSize()) + "\r\n";
    output += "               | OTA free space: " + FormatBytes(ESP.getFreeSketchSpace()) + "\r\n";
    output += "               | Sketch MD5: " + ESP.getSketchMD5() + "\r\n";
    output += "Memory         | Internal heap: " + FormatBytes(ESP.getHeapSize()) + "\r\n";
    output += "               | PSRAM: " + String(ESP.getPsramSize() > 0 ? FormatBytes(ESP.getPsramSize()) : "Not available") + "\r\n";
    output += "Runtime        | ESP-IDF: " + String(ESP.getSdkVersion()) + "\r\n";
    output += "               | Arduino-ESP32: " + String(ESP_ARDUINO_VERSION_MAJOR) + "." + String(ESP_ARDUINO_VERSION_MINOR) + "." + String(ESP_ARDUINO_VERSION_PATCH) + "\r\n";
    output += "               | Reset reason: " + String(ResetReasonName(esp_reset_reason())) + "\r\n";
    output += "               | Uptime: " + Uptime() + "\r\n";
    output += "               | FreeRTOS tasks: " + String(uxTaskGetNumberOfTasks()) + "\r\n";
}

void SystemInfo::Memory(String& output, MemoryUnit unit) {
    const uint32_t heapTotal = ESP.getHeapSize();
    const uint32_t heapAvailable = ESP.getFreeHeap();
    const uint32_t psramTotal = ESP.getPsramSize();
    const uint32_t psramAvailable = ESP.getFreePsram();

    output.reserve(768);
    switch (unit) {
        case MemoryUnit::Kilobytes: output += CLIFormat::Line("Memory", "Values in KiB"); break;
        case MemoryUnit::Megabytes: output += CLIFormat::Line("Memory", "Values in MiB"); break;
        default: output += CLIFormat::Line("Memory", "Values in bytes"); break;
    }
    output += CLIFormat::ContinuationPrefix() + "Type            Total       Used  Available    MinFree   MaxBlock\r\n";
    AppendMemoryRow(output, "Heap", heapTotal, heapAvailable, ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), unit);
    AppendMemoryRow(output, "PSRAM", psramTotal, psramAvailable, ESP.getMinFreePsram(), ESP.getMaxAllocPsram(), unit);
    const uint32_t largestBlock = ESP.getMaxAllocHeap() >= ESP.getMaxAllocPsram()
        ? ESP.getMaxAllocHeap()
        : ESP.getMaxAllocPsram();
    AppendMemoryRow(output, "Total", heapTotal + psramTotal, heapAvailable + psramAvailable, ESP.getMinFreeHeap() + ESP.getMinFreePsram(), largestBlock, unit);

    const uint32_t fragmentation = heapAvailable == 0 || ESP.getMaxAllocHeap() >= heapAvailable
        ? 0
        : 100U - static_cast<uint32_t>((static_cast<uint64_t>(ESP.getMaxAllocHeap()) * 100ULL) / heapAvailable);
    output += CLIFormat::Line("", "");
    output += CLIFormat::Line("Heap", "Fragmentation: " + String(fragmentation) + "%");
    output += CLIFormat::Prefix("CLI") + "Stack minimum: " + MemoryValue(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t), unit) + " ";
    output += unit == MemoryUnit::Bytes ? "bytes free\r\n" : (unit == MemoryUnit::Kilobytes ? "KiB free\r\n" : "MiB free\r\n");
}

bool SystemInfo::FileSystem(String& output) {
    filesystem::Statistics statistics;
    if (!::FileSystem.GetStatistics(statistics)) {
        output = "Filesystem     | Not mounted or unavailable\r\n";
        return false;
    }

    const size_t available = statistics.totalBytes >= statistics.usedBytes
        ? statistics.totalBytes - statistics.usedBytes
        : 0;
    const uint32_t usage = statistics.totalBytes == 0
        ? 0
        : static_cast<uint32_t>((static_cast<uint64_t>(statistics.usedBytes) * 100ULL) / statistics.totalBytes);

    output.reserve(640);
    output += "Filesystem     | Type: LittleFS\r\n";
    output += "               | Mounted: yes\r\n";
    output += "               | Total: " + FormatBytes(statistics.totalBytes) + "\r\n";
    output += "               | Used: " + FormatBytes(statistics.usedBytes) + "\r\n";
    output += "               | Available: " + FormatBytes(available) + "\r\n";
    output += "               | Usage: " + String(usage) + "%\r\n";
    output += "               | Files: " + String(statistics.files) + "\r\n";
    output += "               | Directories: " + String(statistics.directories) + "\r\n";
    output += "               | File data: " + FormatBytes(statistics.fileBytes) + "\r\n";
    output += "               | Largest file: " + FormatBytes(statistics.largestFileBytes) + "\r\n";
    output += "Known files    | Config: " + FormatBytes(::FileSystem.Size(Defaults.ConfigFileName)) + "\r\n";
    output += "               | Log: " + FormatBytes(::FileSystem.Size(Defaults.LogFileName)) + "\r\n";
    return true;
}
