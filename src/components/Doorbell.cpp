#include "Doorbell.h"

#include <driver/gpio.h>
#include <freertos/task.h>

doorbell::doorbell(
    String name,
    int16_t id,
    Buses bus,
    uint8_t address,
    ActiveLevels activeLevel,
    InputModes inputMode,
    uint32_t debounceTimeMs,
    bool enabled
) :
    component(std::move(name), id, bus, address, enabled),
    pActiveLevel(activeLevel),
    pInputMode(inputMode),
    pDebounceTimeMs(debounceTimeMs),
    pDebounceTicks(pdMS_TO_TICKS(debounceTimeMs)) {}

void doorbell::GetInfo(String& output) const {
    component::GetInfo(output);
    output += "State          | " + String(State() ? "pressed" : "released") + "\r\n";
    output += "Rings          | " + String(Rings()) + "\r\n";
    output += "ActiveLevel    | " + String(ActiveLevel() == ActiveLevels::High ? "High" : "Low") + "\r\n";
    output += "InputMode      | " + String(InputMode() == InputModes::PullUp ? "PullUp" : InputMode() == InputModes::PullDown ? "PullDown" : "Floating") + "\r\n";
    output += "DebounceTimeMs | " + String(DebounceTime()) + "\r\n";
}

bool doorbell::Configure() noexcept {
    if (Bus() != Buses::Onboard || !GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(Address()))) return false;

    switch (pInputMode) {
        case InputModes::PullUp:
            pinMode(Address(), INPUT_PULLUP);
            break;
        case InputModes::PullDown:
            pinMode(Address(), INPUT_PULLDOWN);
            break;
        case InputModes::Floating:
            pinMode(Address(), INPUT);
            break;
    }
    return true;
}

bool doorbell::Initialize() noexcept {
    ResetState(xTaskGetTickCount());
    return true;
}

void doorbell::EnabledChanged(bool enabled) noexcept {
    if (enabled) {
        ResetState(xTaskGetTickCount());
        return;
    }

    pState.store(false, std::memory_order_relaxed);
    pRawState = false;
}

void doorbell::Control(TickType_t now) {
    const bool rawState = ReadState();
    if (rawState != pRawState) {
        pRawState = rawState;
        pRawChangedAt = now;
    }

    if (pRawState != State() && static_cast<TickType_t>(now - pRawChangedAt) >= pDebounceTicks) {
        ApplyState(pRawState);
    }
}

const ComponentDescriptor* doorbell::EventDescriptors(size_t& count) const noexcept {
    static const ComponentDescriptor descriptors[] = {
        {"Pressed", EventCodes::Pressed},
        {"Released", EventCodes::Released},
        {"Changed", EventCodes::Changed}
    };

    count = sizeof(descriptors) / sizeof(descriptors[0]);
    return descriptors;
}

bool doorbell::ReadState() const noexcept {
    const bool level = digitalRead(Address()) == HIGH;
    return pActiveLevel == ActiveLevels::High ? level : !level;
}

// Seeds from the current level without publishing anything, so a button
// already held (or a miswired, permanently "active" input) at boot doesn't
// ring the doorbell - only a later debounced edge does.
void doorbell::ResetState(TickType_t now) noexcept {
    const bool state = ReadState();
    pState.store(state, std::memory_order_relaxed);
    pRawState = state;
    pRawChangedAt = now;
}

void doorbell::ApplyState(bool pressed) noexcept {
    if (pressed == State()) return;

    pState.store(pressed, std::memory_order_relaxed);
    if (pressed) pRings.fetch_add(1, std::memory_order_relaxed);
    MarkStateChanged();
    (void)PublishEvent(EventCodes::Changed, pressed ? 1 : 0);
    (void)PublishEvent(pressed ? EventCodes::Pressed : EventCodes::Released, pressed ? 1 : 0);
}
