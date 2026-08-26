#include "Component.h"

#include <freertos/task.h>

component::component(String name, int16_t id, Buses bus, uint8_t address, bool enabled)
    : pName(std::move(name)), pID(id), pEnabled(enabled), pBus(bus), pAddress(address) {}

bool component::ResolveEvent(const String& name, uint16_t& code) const noexcept {
    size_t count = 0;
    const ComponentDescriptor* descriptors = EventDescriptors(count);

    for (size_t index = 0; index < count; ++index) {
        if (name.equalsIgnoreCase(descriptors[index].name)) {
            code = descriptors[index].code;
            return true;
        }
    }
    return false;
}

bool component::ResolveEvent(uint16_t code, String& name) const noexcept {
    size_t count = 0;
    const ComponentDescriptor* descriptors = EventDescriptors(count);

    for (size_t index = 0; index < count; ++index) {
        if (descriptors[index].code == code) {
            name = descriptors[index].name;
            return true;
        }
    }
    return false;
}

bool component::ResolveCommand(const String& name, uint16_t& code) const noexcept {
    size_t count = 0;
    const ComponentDescriptor* descriptors = CommandDescriptors(count);

    for (size_t index = 0; index < count; ++index) {
        if (name.equalsIgnoreCase(descriptors[index].name)) {
            code = descriptors[index].code;
            return true;
        }
    }
    return false;
}

ComponentPropertyResult component::SetProperty(const String& name, const String& value, TickType_t timeout) noexcept {
    if (!name.equalsIgnoreCase("enabled")) return ComponentPropertyResult::PropertyNotSupported;

    bool enabled = false;
    if (!ParseBoolean(value, enabled)) return ComponentPropertyResult::InvalidValue;
    if (enabled == Enabled()) return ComponentPropertyResult::Accepted;

    return RequestCommand(enabled ? ComponentCommand::Enable : ComponentCommand::Disable, enabled ? 1 : 0, timeout) ? ComponentPropertyResult::Accepted : ComponentPropertyResult::CommandRejected;
}

void component::GetInfo(String& output) const {
    output += "Name           | " + Name() + "\r\n";
    output += "ID             | " + String(ID()) + "\r\n";
    output += "Class          | " + String(ClassName(Class())) + "\r\n";
    output += "Bus            | " + String(BusName(Bus())) + "\r\n";
    output += "Address        | " + String(Address()) + "\r\n";
    output += "Enabled        | " + String(Enabled() ? "true" : "false") + "\r\n";
    output += "Configured     | " + String(Configured() ? "true" : "false") + "\r\n";
    output += "Initialized    | " + String(Initialized() ? "true" : "false") + "\r\n";
    output += "PropertyChanged| " + String(PropertyChanged() ? "true" : "false") + "\r\n";
    output += "StateChanged   | " + String(StateChanged() ? "true" : "false") + "\r\n";
}

const char* component::ClassName(Classes value) noexcept {
    switch (value) {
        case Classes::Base: return "Base";
        case Classes::Relay: return "Relay";
        case Classes::PIR: return "PIR";
        case Classes::Button: return "Button";
        case Classes::Blinds: return "Blinds";
        case Classes::Thermometer: return "Thermometer";
        case Classes::CurrentMeter: return "CurrentMeter";
        case Classes::Doorbell: return "Doorbell";
        case Classes::ContactSensor: return "ContactSensor";
        default: return "Unknown";
    }
}

const char* component::BusName(Buses value) noexcept {
    switch (value) {
        case Buses::Group: return "Group";
        case Buses::Onboard: return "Onboard";
        case Buses::I2C: return "I2C";
        default: return "Unknown";
    }
}

bool component::ParseBoolean(const String& value, bool& result) noexcept {
    if (value.equalsIgnoreCase("true") || value.equalsIgnoreCase("on") || value == "1") {
        result = true;
        return true;
    }

    if (value.equalsIgnoreCase("false") || value.equalsIgnoreCase("off") || value == "0") {
        result = false;
        return true;
    }

    return false;
}

const ComponentDescriptor* component::EventDescriptors(size_t& count) const noexcept {
    count = 0;
    return nullptr;
}

const ComponentDescriptor* component::CommandDescriptors(size_t& count) const noexcept {
    count = 0;
    return nullptr;
}

bool component::RequestCommand(uint16_t code, int32_t value, TickType_t timeout) noexcept {
    if (pRuntime == nullptr) return false;

    ComponentCommand command;
    command.target = this;
    command.code = code;
    command.value = value;
    return pRuntime->SendCommand(command, timeout);
}

bool component::RequestCommandFromISR(uint16_t code, int32_t value, BaseType_t* higherPriorityTaskWoken) noexcept {
    if (pRuntime == nullptr) return false;

    ComponentCommand command;
    command.target = this;
    command.code = code;
    command.value = value;
    return pRuntime->SendCommandFromISR(command, higherPriorityTaskWoken);
}

bool component::PublishEvent(uint16_t code, int32_t value) noexcept {
    if (pRuntime == nullptr) return false;

    ComponentEvent event;
    event.source = this;
    event.code = code;
    event.value = value;
    event.timestamp = xTaskGetTickCount();
    return pRuntime->Publish(event);
}

bool component::PublishEventFromISR(uint16_t code, int32_t value, BaseType_t* higherPriorityTaskWoken) noexcept {
    if (pRuntime == nullptr) return false;

    ComponentEvent event;
    event.source = this;
    event.code = code;
    event.value = value;
    event.timestamp = xTaskGetTickCountFromISR();
    return pRuntime->PublishFromISR(event, higherPriorityTaskWoken);
}
