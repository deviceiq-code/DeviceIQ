#pragma once

#include <atomic>
#include <utility>

#include <Arduino.h>

#include "Button.h"
#include "Component.h"
#include "Relay.h"

class blinds final : public component {
    public:
        enum class Motion : uint8_t { Stopped, Opening, Closing };

        enum CommandCodes : uint16_t {
            Open = ComponentCommand::CustomCommandBase,
            Close,
            Stop,
            SetPosition
        };

        enum EventCodes : uint16_t {
            Changed = 1,
            Opening,
            Closing,
            Stopped,
            Opened,
            Closed,
            Fault
        };

        static constexpr uint32_t DEFAULT_OPEN_STEP_TIME_MS = 250;
        static constexpr uint32_t DEFAULT_CLOSE_STEP_TIME_MS = 250;
        static constexpr float DEFAULT_OPEN_CORRECTION_FACTOR = 0.0f;
        static constexpr float DEFAULT_CLOSE_CORRECTION_FACTOR = 0.0f;
        static constexpr uint32_t DEFAULT_ENDSTOP_MARGIN_MS = 0;
        static constexpr uint32_t DEFAULT_REVERSAL_DELAY_MS = 250;
        static constexpr float MAX_CORRECTION_FACTOR = 0.95f;

        blinds(
            String name,
            int16_t id,
            relay& relayUp,
            relay& relayDown,
            button* buttonUp = nullptr,
            button* buttonDown = nullptr,
            uint8_t initialPosition = 0,
            uint32_t openStepTimeMs = DEFAULT_OPEN_STEP_TIME_MS,
            uint32_t closeStepTimeMs = DEFAULT_CLOSE_STEP_TIME_MS,
            float openCorrectionFactor = DEFAULT_OPEN_CORRECTION_FACTOR,
            float closeCorrectionFactor = DEFAULT_CLOSE_CORRECTION_FACTOR,
            uint32_t endstopMarginMs = DEFAULT_ENDSTOP_MARGIN_MS,
            uint32_t reversalDelayMs = DEFAULT_REVERSAL_DELAY_MS,
            bool enabled = true
        );
        ~blinds() override = default;

        [[nodiscard]] Classes Class() const noexcept override { return Classes::Blinds; }
        [[nodiscard]] bool HasPersistentState() const noexcept override { return true; }
        [[nodiscard]] Motion State() const noexcept { return pMotion.load(std::memory_order_relaxed); }
        [[nodiscard]] uint8_t Position() const noexcept { return pPosition.load(std::memory_order_relaxed); }
        [[nodiscard]] uint8_t TargetPosition() const noexcept { return pTargetPosition.load(std::memory_order_relaxed); }
        [[nodiscard]] uint32_t OpenStepTime() const noexcept { return pOpenStepTimeMs; }
        [[nodiscard]] uint32_t CloseStepTime() const noexcept { return pCloseStepTimeMs; }
        [[nodiscard]] float OpenCorrectionFactor() const noexcept { return pOpenCorrectionFactor; }
        [[nodiscard]] float CloseCorrectionFactor() const noexcept { return pCloseCorrectionFactor; }
        [[nodiscard]] uint32_t EndstopMargin() const noexcept { return pEndstopMarginMs; }
        [[nodiscard]] uint32_t ReversalDelay() const noexcept { return pReversalDelayMs; }
        [[nodiscard]] const relay& RelayUp() const noexcept { return pRelayUp; }
        [[nodiscard]] const relay& RelayDown() const noexcept { return pRelayDown; }
        [[nodiscard]] const button* ButtonUp() const noexcept { return pButtonUp; }
        [[nodiscard]] const button* ButtonDown() const noexcept { return pButtonDown; }

        [[nodiscard]] ComponentPropertyResult SetProperty(const String& name, const String& value, TickType_t timeout = 0) noexcept override;
        void GetInfo(String& output) const override;
        [[nodiscard]] static const char* MotionName(Motion value) noexcept;

    protected:
        bool Configure() noexcept override;
        bool Initialize() noexcept override;
        void EnabledChanged(bool enabled) noexcept override;
        void Control(TickType_t now) override;
        void HandleCommand(const ComponentCommand& command) override;
        void HandleMemberEvent(const ComponentEvent& event) override;
        const ComponentDescriptor* EventDescriptors(size_t& count) const noexcept override;
        const ComponentDescriptor* CommandDescriptors(size_t& count) const noexcept override;

    private:
        enum class MoveSource : uint8_t { None, ManualUp, ManualDown, Automatic };

        void StartMovement(Motion direction, MoveSource source, uint8_t target, TickType_t now) noexcept;
        void Energize(Motion direction, MoveSource source, uint8_t target) noexcept;
        void StopMovement(bool publishEvent = true) noexcept;
        void SetMotion(Motion motion, bool publishEvent = true) noexcept;
        void HandleButtonEvent(const ComponentEvent& event) noexcept;
        void HandleRelayEvent(const ComponentEvent& event) noexcept;
        void UpdatePosition(TickType_t now) noexcept;
        void CompleteMovement(uint8_t position) noexcept;
        [[nodiscard]] float Curve(float progress, Motion direction) const noexcept;
        [[nodiscard]] float InverseCurve(float value, Motion direction) const noexcept;
        [[nodiscard]] TickType_t TotalTravelTicks(Motion direction) const noexcept;

        relay& pRelayUp;
        relay& pRelayDown;
        button* const pButtonUp;
        button* const pButtonDown;
        std::atomic<Motion> pMotion{Motion::Stopped};
        std::atomic<uint8_t> pPosition{0};
        std::atomic<uint8_t> pTargetPosition{0};
        const uint32_t pOpenStepTimeMs;
        const uint32_t pCloseStepTimeMs;
        const float pOpenCorrectionFactor;
        const float pCloseCorrectionFactor;
        const uint32_t pEndstopMarginMs;
        const uint32_t pReversalDelayMs;
        const TickType_t pOpenTravelTicks;
        const TickType_t pCloseTravelTicks;
        const TickType_t pEndstopMarginTicks;
        const TickType_t pReversalDelayTicks;
        TickType_t pLastPositionAt = 0;
        TickType_t pReverseStartedAt = 0;
        TickType_t pEndstopMarginStartedAt = 0;
        float pCurveProgress = 0.0f;
        bool pInEndstopMargin = false;
        Motion pPendingMotion = Motion::Stopped;
        MoveSource pMoveSource = MoveSource::None;
        MoveSource pPendingSource = MoveSource::None;
        uint8_t pPendingTarget = 0;
};
