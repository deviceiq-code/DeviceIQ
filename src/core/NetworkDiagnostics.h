#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace NetworkDiagnostics {
    static constexpr uint16_t DefaultPingCount = 4;
    static constexpr uint16_t MaximumPingCount = 20;

    void Ping(WiFiClient& client, const String& destination, uint16_t count = DefaultPingCount);
}
