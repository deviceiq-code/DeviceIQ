#pragma once

#include <atomic>
#include <utility>

#include <Arduino.h>

#include "Component.h"
#include "Pcf8574Output.h"

class relay final : public component {
    public:
        enum class RelayTypes : uint8_t { NormallyClosed, NormallyOpen };
        enum class DriveModes : uint8_t { ActiveHigh, ActiveLow };

        // A relay with PulseTimeMs > 0 is momentary: turning it on
        // auto-releases it back off after that many milliseconds, instead
        // of latching - for driving something that expects a brief button
        // press (e.g. a cloned RF remote's tactile switch) rather than a
        // held contact. See Control().
        static constexpr uint32_t MIN_PULSE_TIME_MS = 20;
        static constexpr uint32_t MAX_PULSE_TIME_MS = 5000;

        enum CommandCodes : uint16_t {
            TurnOff = ComponentCommand::CustomCommandBase,
            TurnOn,
            ToggleState
        };

        enum EventCodes : uint16_t {
            SettingOn = 1,
            SettingOff,
            SetOn,
            SetOff,
            Changed,
            WriteFailed
        };

        relay(String name, int16_t id, Buses bus, uint8_t address, RelayTypes type = RelayTypes::NormallyOpen, DriveModes driveMode = DriveModes::ActiveHigh, bool initialState = false, bool enabled = true, uint32_t pulseTimeMs = 0);
        relay(String name, int16_t id, Pcf8574Output& outputDevice, uint8_t output, RelayTypes type = RelayTypes::NormallyOpen, DriveModes driveMode = DriveModes::ActiveHigh, bool initialState = false, bool enabled = true, uint32_t pulseTimeMs = 0);
        ~relay() override = default;

        [[nodiscard]] Classes Class() const noexcept override { return Classes::Relay; }
        // A momentary relay is never meaningfully "on" at rest - Control()
        // always releases it within PulseTimeMs - so persisting its state
        // across a reboot would only risk latching it on-boot if that ever
        // got saved as true.
        [[nodiscard]] bool HasPersistentState() const noexcept override { return pPulseTimeMs == 0; }
        [[nodiscard]] bool State() const noexcept { return pState.load(std::memory_order_relaxed); }
        [[nodiscard]] RelayTypes Type() const noexcept { return pType; }
        [[nodiscard]] DriveModes DriveMode() const noexcept { return pDriveMode; }
        [[nodiscard]] bool InitialState() const noexcept { return pInitialState; }
        [[nodiscard]] uint32_t PulseTime() const noexcept { return pPulseTimeMs; }

        [[nodiscard]] bool State(bool newState, TickType_t timeout = 0) noexcept;
        [[nodiscard]] bool Toggle(TickType_t timeout = 0) noexcept;
        [[nodiscard]] bool On(TickType_t timeout = 0) noexcept { return State(true, timeout); }
        [[nodiscard]] bool Off(TickType_t timeout = 0) noexcept { return State(false, timeout); }
        [[nodiscard]] ComponentPropertyResult SetProperty(const String& name, const String& value, TickType_t timeout = 0) noexcept override;
        void GetInfo(String& output) const override;

    protected:
        bool Configure() noexcept override;
        bool Initialize() noexcept override;
        void EnabledChanged(bool enabled) noexcept override;
        void Control(TickType_t now) override;
        void HandleCommand(const ComponentCommand& command) override;
        const ComponentDescriptor* EventDescriptors(size_t& count) const noexcept override;
        const ComponentDescriptor* CommandDescriptors(size_t& count) const noexcept override;

    private:
        [[nodiscard]] bool ApplyState(bool newState, bool publishEvents) noexcept;
        [[nodiscard]] bool WriteOutput(bool logicalState) noexcept;
        [[nodiscard]] bool ElectricalLevel(bool logicalState) const noexcept;

        Pcf8574Output* pOutputDevice = nullptr;
        std::atomic<bool> pState{false};
        const RelayTypes pType;
        const DriveModes pDriveMode;
        const bool pInitialState;
        const uint32_t pPulseTimeMs;
        const TickType_t pPulseTicks;
        TickType_t pPulseStartedAt = 0;
};
