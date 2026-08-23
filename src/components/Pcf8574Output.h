#pragma once

#include <Arduino.h>
#include <PCF8574.h>
#include <Wire.h>

class Pcf8574Output {
    public:
        explicit Pcf8574Output(uint8_t deviceAddress);
        Pcf8574Output(TwoWire& wire, uint8_t deviceAddress);

        Pcf8574Output(const Pcf8574Output&) = delete;
        Pcf8574Output& operator=(const Pcf8574Output&) = delete;

        [[nodiscard]] bool Configure(uint8_t output, bool initialLevel) noexcept;
        [[nodiscard]] bool Begin() noexcept;
        [[nodiscard]] bool Write(uint8_t output, bool level) noexcept;
        [[nodiscard]] bool Started() const noexcept { return pStarted; }
        [[nodiscard]] uint8_t DeviceAddress() const noexcept { return pDeviceAddress; }

    private:
        static constexpr uint8_t OUTPUT_COUNT = 8;

        PCF8574 pDevice;
        const uint8_t pDeviceAddress;
        uint8_t pConfiguredOutputs = 0;
        bool pStarted = false;
};
