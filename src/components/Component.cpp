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
