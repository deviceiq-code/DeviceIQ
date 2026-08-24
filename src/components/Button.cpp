#include "Button.h"

#include <driver/gpio.h>
#include <freertos/task.h>

button::button(
    String name,
    int16_t id,
    Buses bus,
    uint8_t address,
    ActiveLevels activeLevel,
    InputModes inputMode,
    uint32_t debounceTimeMs,
    uint32_t longClickTimeMs,
    uint32_t multiClickTimeMs,
    bool enabled
) :
    component(std::move(name), id, bus, address, enabled),
    pActiveLevel(activeLevel),
    pInputMode(inputMode),
    pDebounceTimeMs(debounceTimeMs),
    pLongClickTimeMs(longClickTimeMs),
    pMultiClickTimeMs(multiClickTimeMs),
    pDebounceTicks(pdMS_TO_TICKS(debounceTimeMs)),
    pLongClickTicks(pdMS_TO_TICKS(longClickTimeMs)),
    pMultiClickTicks(pdMS_TO_TICKS(multiClickTimeMs)) {}

button::button(
    String name,
    int16_t id,
    Pcf8574Input& inputDevice,
    uint8_t input,
    ActiveLevels activeLevel,
    InputModes inputMode,
    uint32_t debounceTimeMs,
    uint32_t longClickTimeMs,
    uint32_t multiClickTimeMs,
    bool enabled
) :
    component(std::move(name), id, Buses::I2C, input, enabled),
    pInputDevice(&inputDevice),
    pActiveLevel(activeLevel),
    pInputMode(inputMode),
    pDebounceTimeMs(debounceTimeMs),
    pLongClickTimeMs(longClickTimeMs),
    pMultiClickTimeMs(multiClickTimeMs),
    pDebounceTicks(pdMS_TO_TICKS(debounceTimeMs)),
    pLongClickTicks(pdMS_TO_TICKS(longClickTimeMs)),
    pMultiClickTicks(pdMS_TO_TICKS(multiClickTimeMs)) {}

void button::GetInfo(String& output) const {
    component::GetInfo(output);
    output += "State          | " + String(State() ? "pressed" : "released") + "\r\n";
    output += "ActiveLevel    | " + String(ActiveLevel() == ActiveLevels::High ? "High" : "Low") + "\r\n";
    output += "InputMode      | " + String(InputMode() == InputModes::PullUp ? "PullUp" : InputMode() == InputModes::PullDown ? "PullDown" : "Floating") + "\r\n";
    output += "DebounceTimeMs | " + String(DebounceTime()) + "\r\n";
    output += "LongClickTimeMs| " + String(LongClickTime()) + "\r\n";
    output += "MultiClickMs   | " + String(MultiClickTime()) + "\r\n";
}

bool button::Configure() noexcept {
    if (Bus() == Buses::I2C && pInputDevice != nullptr) {
        return pInputDevice->Configure(Address(), pInputMode == InputModes::PullUp);
    }

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

bool button::Initialize() noexcept {
    if (Bus() == Buses::I2C && (pInputDevice == nullptr || !pInputDevice->Begin())) return false;

    ResetState(xTaskGetTickCount());
    return true;
}

void button::EnabledChanged(bool enabled) noexcept {
    if (enabled) {
        ResetState(xTaskGetTickCount());
        return;
    }

    pState.store(false, std::memory_order_relaxed);
    pRawState = false;
    pClickCount = 0;
}

void button::Control(TickType_t now) {
    bool rawState = false;
    if (!ReadState(rawState)) return;

    if (rawState != pRawState) {
        pRawState = rawState;
        pRawChangedAt = now;
    }

    const bool stableState = State();
    if (pRawState != stableState && Elapsed(now, pRawChangedAt, pDebounceTicks)) {
        ApplyState(pRawState, now);
    }

    if (
        pClickCount > 0 &&
        !State() &&
        !pRawState &&
        Elapsed(now, pLastReleasedAt, pMultiClickTicks)
    ) {
        PublishPendingClick();
    }
}

const ComponentDescriptor* button::EventDescriptors(size_t& count) const noexcept {
    static const ComponentDescriptor descriptors[] = {
        {"Pressed", EventCodes::Pressed},
        {"Released", EventCodes::Released},
        {"Clicked", EventCodes::Clicked},
        {"LongClicked", EventCodes::LongClicked},
        {"DoubleClicked", EventCodes::DoubleClicked},
        {"TripleClicked", EventCodes::TripleClicked}
    };

    count = sizeof(descriptors) / sizeof(descriptors[0]);
    return descriptors;
}

bool button::ReadState(bool& state) noexcept {
    bool level = false;

    if (Bus() == Buses::Onboard) {
        level = digitalRead(Address()) == HIGH;
    } else if (Bus() == Buses::I2C && pInputDevice != nullptr) {
        if (!pInputDevice->Read(Address(), level)) return false;
    } else {
        return false;
    }

    state = pActiveLevel == ActiveLevels::High ? level : !level;
    return true;
}

void button::ResetState(TickType_t now) noexcept {
    bool state = false;
    if (!ReadState(state)) state = false;
    pState.store(state, std::memory_order_relaxed);
    pRawState = state;
    pRawChangedAt = now;
    pPressedAt = now;
    pLastReleasedAt = now;
    pClickCount = 0;
}

void button::ApplyState(bool pressed, TickType_t now) noexcept {
    if (pressed == State()) return;

    pState.store(pressed, std::memory_order_relaxed);
    MarkStateChanged();

    if (pressed) {
        if (pClickCount > 0 && Elapsed(pRawChangedAt, pLastReleasedAt, pMultiClickTicks)) {
            PublishPendingClick();
        }

        pPressedAt = now;
        (void)PublishEvent(EventCodes::Pressed, 1);
        return;
    }

    const TickType_t pressedTicks = now - pPressedAt;
    (void)PublishEvent(EventCodes::Released, 0);

    if (Elapsed(now, pPressedAt, pLongClickTicks)) {
        PublishPendingClick();
        (void)PublishEvent(EventCodes::LongClicked, static_cast<int32_t>(pressedTicks * portTICK_PERIOD_MS));
        return;
    }

    if (pClickCount < 3) ++pClickCount;
    pLastReleasedAt = now;

    if (pClickCount == 3) PublishPendingClick();
}

void button::PublishPendingClick() noexcept {
    switch (pClickCount) {
        case 1:
            (void)PublishEvent(EventCodes::Clicked, 1);
            break;
        case 2:
            (void)PublishEvent(EventCodes::DoubleClicked, 2);
            break;
        case 3:
            (void)PublishEvent(EventCodes::TripleClicked, 3);
            break;
        default:
            break;
    }

    pClickCount = 0;
}

bool button::Elapsed(TickType_t now, TickType_t since, TickType_t interval) noexcept {
    return static_cast<TickType_t>(now - since) >= interval;
}
