#pragma once

#include <Arduino.h>
#include <PCF8574.h>
#include <Wire.h>

class Pcf8574Input {
    public:
        explicit Pcf8574Input(uint8_t deviceAddress);
        Pcf8574Input(TwoWire& wire, uint8_t deviceAddress);

        Pcf8574Input(const Pcf8574Input&) = delete;
        Pcf8574Input& operator=(const Pcf8574Input&) = delete;

        [[nodiscard]] bool Configure(uint8_t input, bool pullUp) noexcept;
        [[nodiscard]] bool Begin() noexcept;
        [[nodiscard]] bool Read(uint8_t input, bool& level) noexcept;
        [[nodiscard]] bool Started() const noexcept { return pStarted; }
        [[nodiscard]] uint8_t DeviceAddress() const noexcept { return pDeviceAddress; }

    private:
        static constexpr uint8_t INPUT_COUNT = 8;

        PCF8574 pDevice;
        const uint8_t pDeviceAddress;
        uint8_t pConfiguredInputs = 0;
        bool pStarted = false;
};
