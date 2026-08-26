#include "Blinds.h"

#include <cstdlib>
#include <freertos/task.h>

blinds::blinds(
    String name,
    int16_t id,
    relay& relayUp,
    relay& relayDown,
    button* buttonUp,
    button* buttonDown,
    uint8_t initialPosition,
    uint32_t stepTimeMs,
    uint32_t reversalDelayMs,
    bool enabled
) :
    component(std::move(name), id, Buses::Group, 0, enabled),
    pRelayUp(relayUp),
    pRelayDown(relayDown),
    pButtonUp(buttonUp),
    pButtonDown(buttonDown),
    pPosition(initialPosition),
    pTargetPosition(initialPosition),
    pStepTimeMs(stepTimeMs),
    pReversalDelayMs(reversalDelayMs),
    pStepTicks(pdMS_TO_TICKS(stepTimeMs) == 0 ? 1 : pdMS_TO_TICKS(stepTimeMs)),
    pReversalDelayTicks(pdMS_TO_TICKS(reversalDelayMs)) {}

ComponentPropertyResult blinds::SetProperty(const String& name, const String& value, TickType_t timeout) noexcept {
    uint16_t command = 0;
    int32_t argument = 0;

    if (name.equalsIgnoreCase("state")) {
        if (value.equalsIgnoreCase("open") || value.equalsIgnoreCase("opening")) command = CommandCodes::Open;
        else if (value.equalsIgnoreCase("close") || value.equalsIgnoreCase("closing")) command = CommandCodes::Close;
        else if (value.equalsIgnoreCase("stop") || value.equalsIgnoreCase("stopped")) command = CommandCodes::Stop;
        else return ComponentPropertyResult::InvalidValue;
    } else if (name.equalsIgnoreCase("position")) {
        if (value.isEmpty()) return ComponentPropertyResult::InvalidValue;
        char* end = nullptr;
        const long parsed = std::strtol(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0' || parsed < 0 || parsed > 100) return ComponentPropertyResult::InvalidValue;
        command = CommandCodes::SetPosition;
        argument = static_cast<int32_t>(parsed);
    } else {
        return component::SetProperty(name, value, timeout);
    }

    if (!Enabled()) return ComponentPropertyResult::ComponentDisabled;
    return RequestCommand(command, argument, timeout)
        ? ComponentPropertyResult::Accepted
        : ComponentPropertyResult::CommandRejected;
}

void blinds::GetInfo(String& output) const {
    component::GetInfo(output);
    output += "State          | " + String(MotionName(State())) + "\r\n";
    output += "Position       | " + String(Position()) + "\r\n";
    output += "TargetPosition | " + String(TargetPosition()) + "\r\n";
    output += "RelayUp        | " + pRelayUp.Name() + "\r\n";
    output += "RelayDown      | " + pRelayDown.Name() + "\r\n";
    output += "ButtonUp       | " + String(pButtonUp == nullptr ? "" : pButtonUp->Name()) + "\r\n";
    output += "ButtonDown     | " + String(pButtonDown == nullptr ? "" : pButtonDown->Name()) + "\r\n";
    output += "StepTimeMs     | " + String(StepTime()) + "\r\n";
    output += "ReversalDelayMs| " + String(ReversalDelay()) + "\r\n";
}

const char* blinds::MotionName(Motion value) noexcept {
    switch (value) {
        case Motion::Opening: return "opening";
        case Motion::Closing: return "closing";
        case Motion::Stopped: return "stopped";
        default: return "unknown";
    }
}

bool blinds::Configure() noexcept {
    return &pRelayUp != &pRelayDown &&
        pRelayUp.Owner() == this && pRelayDown.Owner() == this &&
        (pButtonUp == nullptr || pButtonUp->Owner() == this) &&
        (pButtonDown == nullptr || pButtonDown->Owner() == this);
}

bool blinds::Initialize() noexcept {
    if (pRelayUp.State() || pRelayDown.State()) return false;
    pLastPositionAt = xTaskGetTickCount();
    return true;
}

void blinds::EnabledChanged(bool enabled) noexcept {
    if (!enabled) StopMovement();
}

void blinds::Control(TickType_t now) {
    UpdatePosition(now);

    if (pPendingMotion != Motion::Stopped &&
        static_cast<TickType_t>(now - pReverseStartedAt) >= pReversalDelayTicks) {
        if (pRelayUp.State() || pRelayDown.State()) {
            (void)pRelayUp.Off();
            (void)pRelayDown.Off();
            pReverseStartedAt = now;
            return;
        }
        const Motion direction = pPendingMotion;
        const MoveSource source = pPendingSource;
        const uint8_t target = pPendingTarget;
        pPendingMotion = Motion::Stopped;
        pPendingSource = MoveSource::None;
        Energize(direction, source, target);
    }
}

void blinds::HandleCommand(const ComponentCommand& command) {
    const TickType_t now = xTaskGetTickCount();
    switch (command.code) {
        case CommandCodes::Open:
            StartMovement(Motion::Opening, MoveSource::Automatic, 100, now);
            break;
        case CommandCodes::Close:
            StartMovement(Motion::Closing, MoveSource::Automatic, 0, now);
            break;
        case CommandCodes::Stop:
            StopMovement();
            break;
        case CommandCodes::SetPosition: {
            const uint8_t target = static_cast<uint8_t>(constrain(command.value, 0, 100));
            if (target == Position()) StopMovement();
            else StartMovement(target > Position() ? Motion::Opening : Motion::Closing, MoveSource::Automatic, target, now);
            break;
        }
        default:
            break;
    }
}

void blinds::HandleMemberEvent(const ComponentEvent& event) {
    if (!Enabled()) return;
    if (event.source == pButtonUp || event.source == pButtonDown) HandleButtonEvent(event);
    else if (event.source == &pRelayUp || event.source == &pRelayDown) HandleRelayEvent(event);
}

const ComponentDescriptor* blinds::EventDescriptors(size_t& count) const noexcept {
    static const ComponentDescriptor descriptors[] = {
        {"Changed", EventCodes::Changed},
        {"Opening", EventCodes::Opening},
        {"Closing", EventCodes::Closing},
        {"Stopped", EventCodes::Stopped},
        {"Opened", EventCodes::Opened},
        {"Closed", EventCodes::Closed},
        {"Fault", EventCodes::Fault}
    };
    count = sizeof(descriptors) / sizeof(descriptors[0]);
    return descriptors;
}

const ComponentDescriptor* blinds::CommandDescriptors(size_t& count) const noexcept {
    static const ComponentDescriptor descriptors[] = {
        {"Open", CommandCodes::Open},
        {"Close", CommandCodes::Close},
        {"Stop", CommandCodes::Stop}
    };
    count = sizeof(descriptors) / sizeof(descriptors[0]);
    return descriptors;
}

void blinds::StartMovement(Motion direction, MoveSource source, uint8_t target, TickType_t now) noexcept {
    if (direction == Motion::Stopped) {
        StopMovement();
        return;
    }
    if (target == Position()) {
        StopMovement();
        return;
    }

    const bool reversing = direction == Motion::Opening
        ? pRelayDown.State() || State() == Motion::Closing
        : pRelayUp.State() || State() == Motion::Opening;

    if (reversing) {
        (void)pRelayUp.Off();
        (void)pRelayDown.Off();
        SetMotion(Motion::Stopped);
        pPendingMotion = direction;
        pPendingSource = source;
        pPendingTarget = target;
        pTargetPosition.store(target, std::memory_order_relaxed);
        pReverseStartedAt = now;
        return;
    }

    Energize(direction, source, target);
}

void blinds::Energize(Motion direction, MoveSource source, uint8_t target) noexcept {
    pMoveSource = source;
    pTargetPosition.store(target, std::memory_order_relaxed);
    if (direction == Motion::Opening) {
        (void)pRelayDown.Off();
        if (!pRelayUp.On()) {
            pMoveSource = MoveSource::None;
            (void)PublishEvent(EventCodes::Fault, 1);
        }
    } else if (direction == Motion::Closing) {
        (void)pRelayUp.Off();
        if (!pRelayDown.On()) {
            pMoveSource = MoveSource::None;
            (void)PublishEvent(EventCodes::Fault, -1);
        }
    }
}

void blinds::StopMovement(bool publishEvent) noexcept {
    pPendingMotion = Motion::Stopped;
    pPendingSource = MoveSource::None;
    pMoveSource = MoveSource::None;
    (void)pRelayUp.Off();
    (void)pRelayDown.Off();
    pTargetPosition.store(Position(), std::memory_order_relaxed);
    SetMotion(Motion::Stopped, publishEvent);
}

void blinds::SetMotion(Motion motion, bool publishEvent) noexcept {
    const Motion previous = pMotion.exchange(motion, std::memory_order_relaxed);
    if (previous == motion) return;
    MarkStateChanged();
    if (!publishEvent) return;
    const uint16_t eventCode = motion == Motion::Opening ? EventCodes::Opening :
        motion == Motion::Closing ? EventCodes::Closing : EventCodes::Stopped;
    (void)PublishEvent(eventCode, static_cast<int32_t>(Position()));
    (void)PublishEvent(EventCodes::Changed, static_cast<int32_t>(Position()));
}

void blinds::HandleButtonEvent(const ComponentEvent& event) noexcept {
    const bool isUp = event.source == pButtonUp;
    const bool isDown = event.source == pButtonDown;
    if (!isUp && !isDown) return;

    if (event.code == button::EventCodes::Pressed) {
        if ((isUp && pButtonDown != nullptr && pButtonDown->State()) ||
            (isDown && pButtonUp != nullptr && pButtonUp->State())) {
            StopMovement();
            return;
        }
        StartMovement(
            isUp ? Motion::Opening : Motion::Closing,
            isUp ? MoveSource::ManualUp : MoveSource::ManualDown,
            isUp ? 100 : 0,
            event.timestamp
        );
        return;
    }

    if (event.code == button::EventCodes::Released) {
        if ((isUp && pMoveSource == MoveSource::ManualUp) ||
            (isDown && pMoveSource == MoveSource::ManualDown) ||
            (isUp && pPendingSource == MoveSource::ManualUp) ||
            (isDown && pPendingSource == MoveSource::ManualDown)) StopMovement();
        return;
    }

    if (event.code == button::EventCodes::DoubleClicked) {
        StartMovement(
            isUp ? Motion::Opening : Motion::Closing,
            MoveSource::Automatic,
            isUp ? 100 : 0,
            event.timestamp
        );
    }
    // Clicked, LongClicked and TripleClicked are intentionally private no-ops.
}

void blinds::HandleRelayEvent(const ComponentEvent& event) noexcept {
    if (event.code == relay::EventCodes::WriteFailed) {
        StopMovement();
        (void)PublishEvent(EventCodes::Fault, event.source == &pRelayUp ? 1 : -1);
        return;
    }

    if (event.code != relay::EventCodes::SetOn) return;

    if (event.source == &pRelayUp) {
        if (pRelayDown.State()) {
            StopMovement();
            (void)PublishEvent(EventCodes::Fault, 2);
            return;
        }
        pLastPositionAt = event.timestamp;
        SetMotion(Motion::Opening);
    } else if (event.source == &pRelayDown) {
        if (pRelayUp.State()) {
            StopMovement();
            (void)PublishEvent(EventCodes::Fault, -2);
            return;
        }
        pLastPositionAt = event.timestamp;
        SetMotion(Motion::Closing);
    }
}

void blinds::UpdatePosition(TickType_t now) noexcept {
    const Motion motion = State();
    if (motion == Motion::Stopped) {
        pLastPositionAt = now;
        return;
    }

    const TickType_t elapsed = static_cast<TickType_t>(now - pLastPositionAt);
    const uint32_t steps = elapsed / pStepTicks;
    if (steps == 0) return;
    pLastPositionAt += static_cast<TickType_t>(steps * pStepTicks);

    const uint8_t previous = Position();
    const uint8_t target = TargetPosition();
    uint8_t position = previous;
    if (motion == Motion::Opening) {
        position = static_cast<uint8_t>(min<uint32_t>(target, static_cast<uint32_t>(previous) + steps));
    } else {
        position = static_cast<uint8_t>(steps >= previous - target ? target : previous - steps);
    }

    if (position != previous) {
        pPosition.store(position, std::memory_order_relaxed);
        MarkStateChanged();
        (void)PublishEvent(EventCodes::Changed, static_cast<int32_t>(position));
    }

    if (position == target) {
        StopMovement(false);
        (void)PublishEvent(position == 100 ? EventCodes::Opened : position == 0 ? EventCodes::Closed : EventCodes::Stopped, position);
        (void)PublishEvent(EventCodes::Changed, position);
    }
}
