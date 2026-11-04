#include "RGB_LED.h"

#include <cstdlib>

#include <driver/gpio.h>

uint8_t rgb_led::sNextChannel = 0;

rgb_led::rgb_led(
    String name,
    int16_t id,
    uint8_t redPin,
    uint8_t greenPin,
    uint8_t bluePin,
    DriveModes driveMode,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    bool state,
    bool enabled
) :
    component(std::move(name), id, Buses::Onboard, redPin, enabled),
    pRedPin(redPin),
    pGreenPin(greenPin),
    pBluePin(bluePin),
    pDriveMode(driveMode),
    pState(state),
    pRed(red),
    pGreen(green),
    pBlue(blue) {}

ComponentPropertyResult rgb_led::SetProperty(const String& name, const String& value, TickType_t timeout) noexcept {
    if (name.equalsIgnoreCase("state")) {
        if (value.equalsIgnoreCase("toggle")) {
            if (!Enabled()) return ComponentPropertyResult::ComponentDisabled;
            return RequestCommand(CommandCodes::ToggleState, 0, timeout) ? ComponentPropertyResult::Accepted : ComponentPropertyResult::CommandRejected;
        }

        bool state = false;
        if (!ParseBoolean(value, state)) return ComponentPropertyResult::InvalidValue;
        if (!Enabled()) return ComponentPropertyResult::ComponentDisabled;
        if (state == State()) return ComponentPropertyResult::Accepted;

        return RequestCommand(state ? CommandCodes::TurnOn : CommandCodes::TurnOff, state ? 1 : 0, timeout) ?
            ComponentPropertyResult::Accepted : ComponentPropertyResult::CommandRejected;
    }

    if (name.equalsIgnoreCase("rgb")) {
        const int firstComma = value.indexOf(',');
        const int secondComma = value.indexOf(',', firstComma + 1);
        if (firstComma <= 0 || secondComma <= firstComma + 1 || secondComma == static_cast<int>(value.length()) - 1) {
            return ComponentPropertyResult::InvalidValue;
        }

        long red = 0, green = 0, blue = 0;
        char* end = nullptr;
        String redText = value.substring(0, firstComma);
        String greenText = value.substring(firstComma + 1, secondComma);
        String blueText = value.substring(secondComma + 1);
        redText.trim();
        greenText.trim();
        blueText.trim();

        red = std::strtol(redText.c_str(), &end, 10);
        if (end == redText.c_str() || *end != '\0' || red < 0 || red > 255) return ComponentPropertyResult::InvalidValue;
        green = std::strtol(greenText.c_str(), &end, 10);
        if (end == greenText.c_str() || *end != '\0' || green < 0 || green > 255) return ComponentPropertyResult::InvalidValue;
        blue = std::strtol(blueText.c_str(), &end, 10);
        if (end == blueText.c_str() || *end != '\0' || blue < 0 || blue > 255) return ComponentPropertyResult::InvalidValue;

        if (!Enabled()) return ComponentPropertyResult::ComponentDisabled;

        const int32_t packed = (static_cast<int32_t>(red) << 16) | (static_cast<int32_t>(green) << 8) | static_cast<int32_t>(blue);
        return RequestCommand(CommandCodes::SetColor, packed, timeout) ? ComponentPropertyResult::Accepted : ComponentPropertyResult::CommandRejected;
    }

    return component::SetProperty(name, value, timeout);
}

void rgb_led::GetInfo(String& output) const {
    component::GetInfo(output);
    output += "RedPin         | " + String(RedPin()) + "\r\n";
    output += "GreenPin       | " + String(GreenPin()) + "\r\n";
    output += "BluePin        | " + String(BluePin()) + "\r\n";
    output += "DriveMode      | " + String(DriveMode() == DriveModes::ActiveHigh ? "ActiveHigh" : "ActiveLow") + "\r\n";
    output += "State          | " + String(State() ? "on" : "off") + "\r\n";
    output += "Red            | " + String(Red()) + "\r\n";
    output += "Green          | " + String(Green()) + "\r\n";
    output += "Blue           | " + String(Blue()) + "\r\n";
}

bool rgb_led::Configure() noexcept {
    if (pRedPin == pGreenPin || pRedPin == pBluePin || pGreenPin == pBluePin) return false;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(pRedPin)) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(pGreenPin)) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(pBluePin))) return false;

    if (sNextChannel + 3 > MAX_LEDC_CHANNELS) return false;
    pRedChannel = sNextChannel++;
    pGreenChannel = sNextChannel++;
    pBlueChannel = sNextChannel++;

    const uint8_t pins[] = {pRedPin, pGreenPin, pBluePin};
    const uint8_t channels[] = {pRedChannel, pGreenChannel, pBlueChannel};
    for (size_t index = 0; index < 3; ++index) {
        if (!ledcAttachChannel(pins[index], PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS, channels[index])) return false;
    }

    return true;
}

bool rgb_led::Initialize() noexcept {
    if (!Enabled()) pState.store(false, std::memory_order_relaxed);
    WriteChannels();
    return true;
}

void rgb_led::EnabledChanged(bool enabled) noexcept {
    if (!enabled) (void)ApplyState(false);
}

void rgb_led::HandleCommand(const ComponentCommand& command) {
    switch (command.code) {
        case CommandCodes::TurnOn:
            (void)ApplyState(true);
            break;
        case CommandCodes::TurnOff:
            (void)ApplyState(false);
            break;
        case CommandCodes::ToggleState:
            (void)ApplyState(!State());
            break;
        case CommandCodes::SetColor: {
            const uint8_t red = static_cast<uint8_t>((command.value >> 16) & 0xff);
            const uint8_t green = static_cast<uint8_t>((command.value >> 8) & 0xff);
            const uint8_t blue = static_cast<uint8_t>(command.value & 0xff);
            (void)ApplyColor(red, green, blue);
            break;
        }
        default:
            break;
    }
}

const ComponentDescriptor* rgb_led::EventDescriptors(size_t& count) const noexcept {
    static const ComponentDescriptor descriptors[] = {
        {"SettingOn", EventCodes::SettingOn},
        {"SettingOff", EventCodes::SettingOff},
        {"SetOn", EventCodes::SetOn},
        {"SetOff", EventCodes::SetOff},
        {"ColorChanged", EventCodes::ColorChanged},
        {"Changed", EventCodes::Changed}
    };

    count = sizeof(descriptors) / sizeof(descriptors[0]);
    return descriptors;
}

const ComponentDescriptor* rgb_led::CommandDescriptors(size_t& count) const noexcept {
    static const ComponentDescriptor descriptors[] = {
        {"Off", CommandCodes::TurnOff},
        {"On", CommandCodes::TurnOn},
        {"Toggle", CommandCodes::ToggleState},
        {"Invert", CommandCodes::ToggleState}
    };

    count = sizeof(descriptors) / sizeof(descriptors[0]);
    return descriptors;
}

bool rgb_led::ApplyState(bool newState) noexcept {
    if (newState == State()) return true;

    (void)PublishEvent(newState ? EventCodes::SettingOn : EventCodes::SettingOff, newState ? 1 : 0);
    pState.store(newState, std::memory_order_relaxed);
    WriteChannels();
    MarkStateChanged();
    (void)PublishEvent(newState ? EventCodes::SetOn : EventCodes::SetOff, newState ? 1 : 0);
    (void)PublishEvent(EventCodes::Changed, newState ? 1 : 0);
    return true;
}

bool rgb_led::ApplyColor(uint8_t red, uint8_t green, uint8_t blue) noexcept {
    if (red == Red() && green == Green() && blue == Blue()) return true;

    pRed.store(red, std::memory_order_relaxed);
    pGreen.store(green, std::memory_order_relaxed);
    pBlue.store(blue, std::memory_order_relaxed);
    WriteChannels();
    MarkStateChanged();

    const int32_t packed = (static_cast<int32_t>(red) << 16) | (static_cast<int32_t>(green) << 8) | static_cast<int32_t>(blue);
    (void)PublishEvent(EventCodes::ColorChanged, packed);
    (void)PublishEvent(EventCodes::Changed, packed);
    return true;
}

void rgb_led::WriteChannels() noexcept {
    const bool on = State();
    // Arduino-ESP32 3.x addresses LEDC by pin; the channel only matters at attach time.
    ledcWrite(pRedPin, ElectricalDuty(on ? Red() : 0));
    ledcWrite(pGreenPin, ElectricalDuty(on ? Green() : 0));
    ledcWrite(pBluePin, ElectricalDuty(on ? Blue() : 0));
}

uint8_t rgb_led::ElectricalDuty(uint8_t colorValue) const noexcept {
    return pDriveMode == DriveModes::ActiveHigh ? colorValue : static_cast<uint8_t>(255 - colorValue);
}
