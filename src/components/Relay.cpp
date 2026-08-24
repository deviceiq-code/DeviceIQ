#include "Relay.h"

#include <driver/gpio.h>

relay::relay(String name, int16_t id, Buses bus, uint8_t address, RelayTypes type, DriveModes driveMode, bool initialState, bool enabled) : component(std::move(name), id, bus, address, enabled), pState(initialState), pType(type), pDriveMode(driveMode), pInitialState(initialState) {}
relay::relay(String name, int16_t id, Pcf8574Output& outputDevice, uint8_t output, RelayTypes type, DriveModes driveMode, bool initialState, bool enabled) : component(std::move(name), id, Buses::I2C, output, enabled), pOutputDevice(&outputDevice), pState(initialState), pType(type), pDriveMode(driveMode), pInitialState(initialState) {}

bool relay::State(bool newState, TickType_t timeout) noexcept {
    return RequestCommand(newState ? CommandCodes::TurnOn : CommandCodes::TurnOff, newState ? 1 : 0, timeout);
}

bool relay::Toggle(TickType_t timeout) noexcept {
    return RequestCommand(CommandCodes::ToggleState, 0, timeout);
}

ComponentPropertyResult relay::SetProperty(const String& name, const String& value, TickType_t timeout) noexcept {
    if (!name.equalsIgnoreCase("state")) return component::SetProperty(name, value, timeout);

    bool state = false;
    if (!ParseBoolean(value, state)) return ComponentPropertyResult::InvalidValue;
    if (!Enabled()) return ComponentPropertyResult::ComponentDisabled;
    if (state == State()) return ComponentPropertyResult::Accepted;

    return State(state, timeout) ? ComponentPropertyResult::Accepted : ComponentPropertyResult::CommandRejected;
}

void relay::GetInfo(String& output) const {
    component::GetInfo(output);
    output += "State          | " + String(State() ? "on" : "off") + "\r\n";
    output += "RelayType      | " + String(Type() == RelayTypes::NormallyOpen ? "NormallyOpen" : "NormallyClosed") + "\r\n";
    output += "DriveMode      | " + String(DriveMode() == DriveModes::ActiveHigh ? "ActiveHigh" : "ActiveLow") + "\r\n";
    output += "InitialState   | " + String(pInitialState ? "on" : "off") + "\r\n";
}

bool relay::Configure() noexcept {
    const bool startupState = Enabled() ? pInitialState : false;
    const bool level = ElectricalLevel(startupState);

    if (Bus() == Buses::Onboard) {
        if (!GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(Address()))) return false;
        digitalWrite(Address(), level ? HIGH : LOW);
        pinMode(Address(), OUTPUT);
        return true;
    }

    if (Bus() == Buses::I2C && pOutputDevice != nullptr) {
        return pOutputDevice->Configure(Address(), level);
    }

    return false;
}

bool relay::Initialize() noexcept {
    const bool startupState = Enabled() ? pInitialState : false;

    if (Bus() == Buses::I2C) {
        if (pOutputDevice == nullptr || !pOutputDevice->Begin()) return false;
    }

    if (!WriteOutput(startupState)) return false;
    pState.store(startupState, std::memory_order_relaxed);
    return true;
}

void relay::EnabledChanged(bool enabled) noexcept {
    if (!enabled) (void)ApplyState(false, true);
}

void relay::HandleCommand(const ComponentCommand& command) {
    switch (command.code) {
        case CommandCodes::TurnOn:
            (void)ApplyState(true, true);
            break;
        case CommandCodes::TurnOff:
            (void)ApplyState(false, true);
            break;
        case CommandCodes::ToggleState:
            (void)ApplyState(!State(), true);
            break;
        default:
            break;
    }
}

const ComponentDescriptor* relay::EventDescriptors(size_t& count) const noexcept {
    static const ComponentDescriptor descriptors[] = {
        {"SettingOn", EventCodes::SettingOn},
        {"SettingOff", EventCodes::SettingOff},
        {"SetOn", EventCodes::SetOn},
        {"SetOff", EventCodes::SetOff},
        {"Changed", EventCodes::Changed},
        {"WriteFailed", EventCodes::WriteFailed}
    };

    count = sizeof(descriptors) / sizeof(descriptors[0]);
    return descriptors;
}

const ComponentDescriptor* relay::CommandDescriptors(size_t& count) const noexcept {
    static const ComponentDescriptor descriptors[] = {
        {"Off", CommandCodes::TurnOff},
        {"On", CommandCodes::TurnOn},
        {"Toggle", CommandCodes::ToggleState},
        {"Invert", CommandCodes::ToggleState}
    };

    count = sizeof(descriptors) / sizeof(descriptors[0]);
    return descriptors;
}

bool relay::ApplyState(bool newState, bool publishEvents) noexcept {
    if (newState == State()) return true;

    if (publishEvents) {
        (void)PublishEvent(newState ? EventCodes::SettingOn : EventCodes::SettingOff, newState ? 1 : 0);
    }

    if (!WriteOutput(newState)) {
        if (publishEvents) (void)PublishEvent(EventCodes::WriteFailed, newState ? 1 : 0);
        return false;
    }

    pState.store(newState, std::memory_order_relaxed);
    MarkStateChanged();

    if (publishEvents) {
        (void)PublishEvent(newState ? EventCodes::SetOn : EventCodes::SetOff, newState ? 1 : 0);
        (void)PublishEvent(EventCodes::Changed, newState ? 1 : 0);
    }
    return true;
}

bool relay::WriteOutput(bool logicalState) noexcept {
    const bool level = ElectricalLevel(logicalState);

    if (Bus() == Buses::Onboard) {
        digitalWrite(Address(), level ? HIGH : LOW);
        return true;
    }

    if (Bus() == Buses::I2C && pOutputDevice != nullptr) {
        return pOutputDevice->Write(Address(), level);
    }

    return false;
}

bool relay::ElectricalLevel(bool logicalState) const noexcept {
    return pDriveMode == DriveModes::ActiveHigh ? logicalState : !logicalState;
}
