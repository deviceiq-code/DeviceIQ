#pragma once

#include <atomic>
#include <utility>

#include <Arduino.h>

#include "Component.h"

class rgb_led final : public component {
    public:
        enum class DriveModes : uint8_t { ActiveHigh, ActiveLow };

        enum CommandCodes : uint16_t {
            TurnOff = ComponentCommand::CustomCommandBase,
            TurnOn,
            ToggleState,
            SetColor
        };

        enum EventCodes : uint16_t {
            SettingOn = 1,
            SettingOff,
            SetOn,
            SetOff,
            ColorChanged,
            Changed
        };

        static constexpr uint32_t PWM_FREQUENCY_HZ = 5000;
        static constexpr uint8_t PWM_RESOLUTION_BITS = 8;
        // SOC_LEDC_CHANNEL_NUM in this Arduino-ESP32 core's SDK - 8 on both
        // the ESP32 classic and ESP32-S3 targets this project builds for -
        // so at most 2 RGB_LED instances (3 channels each) can coexist.
        static constexpr uint8_t MAX_LEDC_CHANNELS = 8;

        rgb_led(
            String name,
            int16_t id,
            uint8_t redPin,
            uint8_t greenPin,
            uint8_t bluePin,
            DriveModes driveMode = DriveModes::ActiveHigh,
            uint8_t red = 0,
            uint8_t green = 0,
            uint8_t blue = 0,
            bool state = false,
            bool enabled = true
        );
        ~rgb_led() override = default;

        [[nodiscard]] Classes Class() const noexcept override { return Classes::RGB_LED; }
        [[nodiscard]] bool HasPersistentState() const noexcept override { return true; }
        [[nodiscard]] bool State() const noexcept { return pState.load(std::memory_order_relaxed); }
        [[nodiscard]] uint8_t Red() const noexcept { return pRed.load(std::memory_order_relaxed); }
        [[nodiscard]] uint8_t Green() const noexcept { return pGreen.load(std::memory_order_relaxed); }
        [[nodiscard]] uint8_t Blue() const noexcept { return pBlue.load(std::memory_order_relaxed); }
        [[nodiscard]] DriveModes DriveMode() const noexcept { return pDriveMode; }
        [[nodiscard]] uint8_t RedPin() const noexcept { return pRedPin; }
        [[nodiscard]] uint8_t GreenPin() const noexcept { return pGreenPin; }
        [[nodiscard]] uint8_t BluePin() const noexcept { return pBluePin; }

        [[nodiscard]] ComponentPropertyResult SetProperty(const String& name, const String& value, TickType_t timeout = 0) noexcept override;
        void GetInfo(String& output) const override;

    protected:
        bool Configure() noexcept override;
        bool Initialize() noexcept override;
        void EnabledChanged(bool enabled) noexcept override;
        void HandleCommand(const ComponentCommand& command) override;
        const ComponentDescriptor* EventDescriptors(size_t& count) const noexcept override;
        const ComponentDescriptor* CommandDescriptors(size_t& count) const noexcept override;

    private:
        [[nodiscard]] bool ApplyState(bool newState) noexcept;
        [[nodiscard]] bool ApplyColor(uint8_t red, uint8_t green, uint8_t blue) noexcept;
        void WriteChannels() noexcept;
        [[nodiscard]] uint8_t ElectricalDuty(uint8_t colorValue) const noexcept;

        static uint8_t sNextChannel;

        const uint8_t pRedPin;
        const uint8_t pGreenPin;
        const uint8_t pBluePin;
        const DriveModes pDriveMode;
        uint8_t pRedChannel = 0;
        uint8_t pGreenChannel = 0;
        uint8_t pBlueChannel = 0;
        std::atomic<bool> pState;
        std::atomic<uint8_t> pRed;
        std::atomic<uint8_t> pGreen;
        std::atomic<uint8_t> pBlue;
};
