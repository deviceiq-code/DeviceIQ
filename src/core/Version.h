#pragma once

#include <Arduino.h>

#ifndef DEVICEIQ_VERSION_MAJOR
#define DEVICEIQ_VERSION_MAJOR 1
#endif
#ifndef DEVICEIQ_VERSION_MINOR
#define DEVICEIQ_VERSION_MINOR 0
#endif
#ifndef DEVICEIQ_VERSION_REVISION
#define DEVICEIQ_VERSION_REVISION 0
#endif

namespace Version {
    constexpr const char* ProductFamily = "DeviceIQ";
    constexpr const char* ProductName   = "Home";

    struct SoftwareVersion {
        static constexpr uint8_t Major = DEVICEIQ_VERSION_MAJOR;
        static constexpr uint8_t Minor = DEVICEIQ_VERSION_MINOR;
        static constexpr uint8_t Revision = DEVICEIQ_VERSION_REVISION;

        static String Info() { return String(Major) + "." + String(Minor) + "." + String(Revision);}
    };

    struct HardwareVersion {
        static constexpr const char* Model = "ESP32-S3 N16R8";

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