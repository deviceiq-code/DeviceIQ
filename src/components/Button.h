#pragma once

#include <atomic>
#include <utility>

#include <Arduino.h>

#include "Component.h"
#include "Pcf8574Input.h"

class button final : public component {
    public:
        enum class ActiveLevels : uint8_t { High, Low };
        enum class InputModes : uint8_t { Floating, PullUp, PullDown };

        enum EventCodes : uint16_t {
            Pressed = 1,
            Released,
            Clicked,
            LongClicked,
            DoubleClicked,
            TripleClicked
        };

        static constexpr uint32_t DEFAULT_DEBOUNCE_TIME_MS = 50;
        static constexpr uint32_t DEFAULT_LONG_CLICK_TIME_MS = 1000;
        static constexpr uint32_t DEFAULT_MULTI_CLICK_TIME_MS = 400;

        button(
            String name,
            int16_t id,
            Buses bus,
            uint8_t address,
            ActiveLevels activeLevel = ActiveLevels::Low,
            InputModes inputMode = InputModes::PullUp,
            uint32_t debounceTimeMs = DEFAULT_DEBOUNCE_TIME_MS,
            uint32_t longClickTimeMs = DEFAULT_LONG_CLICK_TIME_MS,
            uint32_t multiClickTimeMs = DEFAULT_MULTI_CLICK_TIME_MS,
            bool enabled = true
        );
        button(
            String name,
            int16_t id,
            Pcf8574Input& inputDevice,
            uint8_t input,
            ActiveLevels activeLevel = ActiveLevels::Low,
            InputModes inputMode = InputModes::PullUp,
            uint32_t debounceTimeMs = DEFAULT_DEBOUNCE_TIME_MS,
            uint32_t longClickTimeMs = DEFAULT_LONG_CLICK_TIME_MS,
            uint32_t multiClickTimeMs = DEFAULT_MULTI_CLICK_TIME_MS,
            bool enabled = true
        );
        ~button() override = default;

        [[nodiscard]] Classes Class() const noexcept override { return Classes::Button; }
        [[nodiscard]] bool State() const noexcept { return pState.load(std::memory_order_relaxed); }
        [[nodiscard]] ActiveLevels ActiveLevel() const noexcept { return pActiveLevel; }
        [[nodiscard]] InputModes InputMode() const noexcept { return pInputMode; }
        [[nodiscard]] uint32_t DebounceTime() const noexcept { return pDebounceTimeMs; }
        [[nodiscard]] uint32_t LongClickTime() const noexcept { return pLongClickTimeMs; }
        [[nodiscard]] uint32_t MultiClickTime() const noexcept { return pMultiClickTimeMs; }

    protected:
        bool Configure() noexcept override;
        bool Initialize() noexcept override;
        void EnabledChanged(bool enabled) noexcept override;
        void Control(TickType_t now) override;
        const ComponentDescriptor* EventDescriptors(size_t& count) const noexcept override;

    private:
        [[nodiscard]] bool ReadState(bool& state) noexcept;
        void ResetState(TickType_t now) noexcept;
        void ApplyState(bool pressed, TickType_t now) noexcept;
        void PublishPendingClick() noexcept;
        [[nodiscard]] static bool Elapsed(TickType_t now, TickType_t since, TickType_t interval) noexcept;

        std::atomic<bool> pState{false};
        Pcf8574Input* pInputDevice = nullptr;
        const ActiveLevels pActiveLevel;
        const InputModes pInputMode;
        const uint32_t pDebounceTimeMs;
        const uint32_t pLongClickTimeMs;
        const uint32_t pMultiClickTimeMs;
        const TickType_t pDebounceTicks;
        const TickType_t pLongClickTicks;
        const TickType_t pMultiClickTicks;

        bool pRawState = false;
        TickType_t pRawChangedAt = 0;
        TickType_t pPressedAt = 0;
        TickType_t pLastReleasedAt = 0;
        uint8_t pClickCount = 0;
};
