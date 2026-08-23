#include "Pcf8574Input.h"

Pcf8574Input::Pcf8574Input(uint8_t deviceAddress)
    : pDevice(deviceAddress), pDeviceAddress(deviceAddress) {}

Pcf8574Input::Pcf8574Input(TwoWire& wire, uint8_t deviceAddress)
    : pDevice(&wire, deviceAddress), pDeviceAddress(deviceAddress) {}

bool Pcf8574Input::Configure(uint8_t input, bool pullUp) noexcept {
    if (pStarted || input >= INPUT_COUNT) return false;

    const uint8_t mask = static_cast<uint8_t>(1U << input);
    if ((pConfiguredInputs & mask) != 0) return false;

    pDevice.pinMode(input, pullUp ? INPUT_PULLUP : INPUT);
    pConfiguredInputs = static_cast<uint8_t>(pConfiguredInputs | mask);
    return true;
}

bool Pcf8574Input::Begin() noexcept {
    if (pStarted) return true;
    if (pConfiguredInputs == 0 || !pDevice.begin()) return false;

    pStarted = true;
    return true;
}

bool Pcf8574Input::Read(uint8_t input, bool& level) noexcept {
    if (!pStarted || input >= INPUT_COUNT) return false;

    const uint8_t mask = static_cast<uint8_t>(1U << input);
    if ((pConfiguredInputs & mask) == 0) return false;

    level = pDevice.digitalRead(input, true) == HIGH;
    return true;
}
