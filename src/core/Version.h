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

    static String Info() { return String(Version::ProductFamily) + String(Version::ProductName) + String(" ") + SoftwareVersion::Info(); }

    using Software = SoftwareVersion;
    using Hardware = HardwareVersion;
}