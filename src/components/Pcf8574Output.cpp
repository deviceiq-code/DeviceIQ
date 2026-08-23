#include "Pcf8574Output.h"

Pcf8574Output::Pcf8574Output(uint8_t deviceAddress)
    : pDevice(deviceAddress), pDeviceAddress(deviceAddress) {}

Pcf8574Output::Pcf8574Output(TwoWire& wire, uint8_t deviceAddress)
    : pDevice(&wire, deviceAddress), pDeviceAddress(deviceAddress) {}

bool Pcf8574Output::Configure(uint8_t output, bool initialLevel) noexcept {
    if (pStarted || output >= OUTPUT_COUNT) return false;

    const uint8_t mask = static_cast<uint8_t>(1U << output);
    if ((pConfiguredOutputs & mask) != 0) return false;

    pDevice.pinMode(output, OUTPUT, initialLevel ? HIGH : LOW);
    pConfiguredOutputs = static_cast<uint8_t>(pConfiguredOutputs | mask);
    return true;
}

bool Pcf8574Output::Begin() noexcept {
    if (pStarted) return true;
    if (pConfiguredOutputs == 0 || !pDevice.begin()) return false;

    pStarted = true;
    return true;
}

bool Pcf8574Output::Write(uint8_t output, bool level) noexcept {
    if (!pStarted || output >= OUTPUT_COUNT) return false;

    const uint8_t mask = static_cast<uint8_t>(1U << output);
    if ((pConfiguredOutputs & mask) == 0) return false;
    return pDevice.digitalWrite(output, level ? HIGH : LOW);
}
