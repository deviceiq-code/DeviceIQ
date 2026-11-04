#pragma once

#include <atomic>
#include <utility>

#include <Arduino.h>

#include "Component.h"
#include "core/EspNowBus.h"

// A debounced GPIO input, mechanically a stripped-down Button (no long/
// multi-click), but exposed to Home Assistant as an "event" entity with
// device_class "doorbell" instead of a binary_sensor - see
// mqttclient::PublishDoorbellDiscovery(). That's what lets HA's HomeKit
// Bridge surface a native Doorbell accessory (push notification / HomePod
// chime) without a linked camera. Same model as DeviceIQ Lite's DoorBell.
class doorbell final : public component {
    public:
        enum class ActiveLevels : uint8_t { High, Low };
        enum class InputModes : uint8_t { Floating, PullUp, PullDown };

        enum EventCodes : uint16_t {
            Pressed = 1,
            Released,
            Changed
        };

        static constexpr uint32_t DEFAULT_DEBOUNCE_TIME_MS = 50;

        doorbell(
            String name,
            int16_t id,
            Buses bus,
            uint8_t address,
            ActiveLevels activeLevel = ActiveLevels::Low,
            InputModes inputMode = InputModes::PullUp,
            uint32_t debounceTimeMs = DEFAULT_DEBOUNCE_TIME_MS,
            bool enabled = true
        );
        // A doorbell button wired to GPIO `address` of ESP-NOW node `node`.
        doorbell(
            String name,
            int16_t id,
            String node,
            uint8_t address,
            ActiveLevels activeLevel = ActiveLevels::Low,
            InputModes inputMode = InputModes::PullUp,
            uint32_t debounceTimeMs = DEFAULT_DEBOUNCE_TIME_MS,
            bool enabled = true
        );
        ~doorbell() override = default;

        [[nodiscard]] Classes Class() const noexcept override { return Classes::Doorbell; }
        [[nodiscard]] bool State() const noexcept { return pState.load(std::memory_order_relaxed); }
        // Debounced presses since boot. A ring is momentary, so a client
        // polling State() (the web dashboard) would almost always miss it;
        // comparing this counter between polls doesn't.
        [[nodiscard]] uint32_t Rings() const noexcept { return pRings.load(std::memory_order_relaxed); }
        [[nodiscard]] ActiveLevels ActiveLevel() const noexcept { return pActiveLevel; }
        [[nodiscard]] InputModes InputMode() const noexcept { return pInputMode; }
        [[nodiscard]] uint32_t DebounceTime() const noexcept { return pDebounceTimeMs; }
        [[nodiscard]] const String& Node() const noexcept { return pNode; }
        [[nodiscard]] bool NodeOnline() const noexcept;
        void GetInfo(String& output) const override;

    protected:
        bool Configure() noexcept override;
        bool Initialize() noexcept override;
        void EnabledChanged(bool enabled) noexcept override;
        void Control(TickType_t now) override;
        const ComponentDescriptor* EventDescriptors(size_t& count) const noexcept override;

    private:
        // False while an ESP-NOW node isn't reporting: Control() then holds
        // the last known state rather than inventing an edge.
        [[nodiscard]] bool ReadState(bool& state) const noexcept;
        void ResetState(TickType_t now) noexcept;
        void ApplyState(bool pressed) noexcept;

        const String pNode;
        espnowbus::Port pPort;
        std::atomic<bool> pState{false};
        std::atomic<uint32_t> pRings{0};
        const ActiveLevels pActiveLevel;
        const InputModes pInputMode;
        const uint32_t pDebounceTimeMs;
        const TickType_t pDebounceTicks;

        bool pRawState = false;
        TickType_t pRawChangedAt = 0;
};
