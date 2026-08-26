#pragma once

#include <Arduino.h>

namespace Version {
    constexpr const char* ProductFamily = "DeviceIQ";
    constexpr const char* ProductName   = "Home";

    struct SoftwareVersion {
        static constexpr uint8_t Major = 1;
        static constexpr uint8_t Minor = 0;
        static constexpr uint8_t Revision = 0;

        static String Info() { return String(Major) + "." + String(Minor) + "." + String(Revision);}
    };

    struct HardwareVersion {
        static constexpr const char* Model = "ESP32-WROOM";

        static constexpr uint8_t Major = 1;
        static constexpr uint8_t Minor = 3;
        static constexpr uint8_t Revision = 2;

        static String Info() { return String(Major) + "." + String(Minor) + "." + String(Revision);}
    };

    static String SerialNumber() {
        uint8_t mac[6]{};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);

        char serial[18];
        snprintf(
            serial, sizeof(serial), "DIQ-%02X%02X%02X%02X-%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
        );
        return String(serial);
    }

    static String Info() { return String(Version::ProductFamily) + " " + String(Version::ProductName) + String(" ") + SoftwareVersion::Info(); }

    using Software = SoftwareVersion;
    using Hardware = HardwareVersion;
}