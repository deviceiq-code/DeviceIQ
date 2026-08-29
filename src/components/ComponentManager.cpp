#include "ComponentManager.h"

bool ComponentManager::Register(component& candidate) noexcept {
    if (IsStarted() || pComponentCount >= MAX_COMPONENTS || IsRegistered(&candidate)) return false;

    for (size_t index = 0; index < pComponentCount; ++index) {
        if (pComponents[index]->ID() == candidate.ID() || pComponents[index]->Name() == candidate.Name()) return false;
        if (
            candidate.Bus() == component::Buses::Onboard &&
            pComponents[index]->Bus() == component::Buses::Onboard &&
            pComponents[index]->Address() == candidate.Address()
        ) return false;
    }

    candidate.SetRuntime(this);
    pComponents[pComponentCount++] = &candidate;
    return true;
}

bool ComponentManager::Register(std::unique_ptr<component> candidate) noexcept {
    if (!candidate) return false;

    const size_t index = pComponentCount;
    if (!Register(*candidate)) return false;

    pOwnedComponents[index] = std::move(candidate);
    return true;
}

bool ComponentManager::AssignOwner(component& member, component& owner) noexcept {
    if (IsStarted() || &member == &owner || !IsRegistered(&member) || !IsRegistered(&owner) ||
        member.Owner() != nullptr || owner.Owner() != nullptr) return false;
    member.SetOwner(&owner);
    return true;
}

bool ComponentManager::Start() noexcept {
    if (IsStarted()) return true;
    pStartError.clear();

    if (pCommandQueue == nullptr) pCommandQueue = xQueueCreateStatic(COMMAND_QUEUE_LENGTH, sizeof(ComponentCommand), pCommandQueueStorage, &pCommandQueueControl);
    if (pEventQueue == nullptr) pEventQueue = xQueueCreateStatic(EVENT_QUEUE_LENGTH, sizeof(ComponentEvent), pEventQueueStorage, &pEventQueueControl);
    if (pCommandQueue == nullptr || pEventQueue == nullptr) {
        pStartError = "unable to create command/event queues";
        return false;
    }

    for (size_t index = 0; index < pComponentCount; ++index) {
        if (pComponents[index]->Configured()) continue;
        if (!pComponents[index]->Configure()) {
            pStartError = "component #" + String(pComponents[index]->ID()) + " " + pComponents[index]->Name() + " failed during resource configuration";
            return false;
        }
        pComponents[index]->SetConfigured(true);
    }

    for (size_t index = 0; index < pComponentCount; ++index) {
        if (pComponents[index]->Initialized()) continue;
        if (!pComponents[index]->Initialize()) {
            pStartError = "component #" + String(pComponents[index]->ID()) + " " + pComponents[index]->Name() + " failed during hardware initialization";
            return false;
        }
        pComponents[index]->SetInitialized(true);
    }

    const BaseType_t result = xTaskCreate(TaskEntry, "Components", TASK_STACK_SIZE, this, TASK_PRIORITY, &pTaskHandle);
    if (result != pdPASS) {
        pTaskHandle = nullptr;
        pStartError = "unable to create component task";
        return false;
    }

    return true;
}

bool ComponentManager::SendCommand(const ComponentCommand& command, TickType_t timeout) noexcept {
    if (pCommandQueue == nullptr || command.target == nullptr || !IsRegistered(command.target)) return false;
    if (xQueueSend(pCommandQueue, &command, timeout) == pdTRUE) return true;
    pDroppedCommands.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool ComponentManager::SendCommandFromISR(const ComponentCommand& command, BaseType_t* higherPriorityTaskWoken) noexcept {
    if (pCommandQueue == nullptr || command.target == nullptr) return false;
    if (xQueueSendFromISR(pCommandQueue, &command, higherPriorityTaskWoken) == pdTRUE) return true;
    pDroppedCommands.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool ComponentManager::ReceiveEvent(ComponentEvent& event, TickType_t timeout) noexcept {
    if (pEventQueue == nullptr) return false;
    return xQueueReceive(pEventQueue, &event, timeout) == pdTRUE;
}

component* ComponentManager::FindByName(const String& name) const noexcept {
    for (size_t index = 0; index < pComponentCount; ++index) {
        if (pComponents[index]->Name().equalsIgnoreCase(name)) return pComponents[index];
    }
    return nullptr;
}

component* ComponentManager::FindByID(int16_t id) const noexcept {
    for (size_t index = 0; index < pComponentCount; ++index) {
        if (pComponents[index]->ID() == id) return pComponents[index];
    }
    return nullptr;
}

component* ComponentManager::At(size_t index) const noexcept {
    return index < pComponentCount ? pComponents[index] : nullptr;
}

bool ComponentManager::PropertyChanged() const noexcept {
    for (size_t index = 0; index < pComponentCount; ++index) {
        if (pComponents[index]->PropertyChanged()) return true;
    }
    return false;
}

bool ComponentManager::StateChanged() const noexcept {
    for (size_t index = 0; index < pComponentCount; ++index) {
        if (pComponents[index]->StateChanged()) return true;
    }
    return false;
}

bool ComponentManager::PersistenceRequired() const noexcept {
    for (size_t index = 0; index < pComponentCount; ++index) {
        const component* item = pComponents[index];
        if (item->IsPublic() && item->PropertyChanged()) return true;
        if (item->IsPublic() && item->HasPersistentState() && item->StateChanged()) return true;
    }
    return false;
}

void ComponentManager::ClearPropertyChanged() noexcept {
    for (size_t index = 0; index < pComponentCount; ++index) pComponents[index]->ClearPropertyChanged();
}

void ComponentManager::ClearStateChanged() noexcept {
    for (size_t index = 0; index < pComponentCount; ++index) pComponents[index]->ClearStateChanged();
}

void ComponentManager::TaskEntry(void* parameter) {
    static_cast<ComponentManager*>(parameter)->Task();
}

void ComponentManager::Task() {
    TickType_t lastWake = xTaskGetTickCount();

    while (true) {
        ProcessCommands();

        const TickType_t now = xTaskGetTickCount();
        for (size_t index = 0; index < pComponentCount; ++index) {
            component* component = pComponents[index];
            if (component->Enabled()) component->Control(now);
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_INTERVAL_MS));
    }
}

void ComponentManager::ProcessCommands() {
    ComponentCommand command;
    while (xQueueReceive(pCommandQueue, &command, 0) == pdTRUE) ProcessCommand(command);
}

void ComponentManager::ProcessCommand(const ComponentCommand& command) {
    if (!IsRegistered(command.target)) return;

    if (command.type == ComponentCommand::Types::TriggerEvent) {
        if (!command.target->Enabled() || !command.target->Initialized()) return;

        ComponentEvent event;
        event.source = command.target;
        event.code = command.code;
        event.value = command.value;
        event.timestamp = xTaskGetTickCount();
        (void)Publish(event);
        return;
    }

    if (command.code == ComponentCommand::Enable) {
        if (command.target->SetEnabled(true)) command.target->EnabledChanged(true);
        return;
    }

    if (command.code == ComponentCommand::Disable) {
        if (command.target->SetEnabled(false)) command.target->EnabledChanged(false);
        return;
    }

    if (command.target->Enabled()) command.target->HandleCommand(command);
}

bool ComponentManager::IsRegistered(const component* component) const noexcept {
    for (size_t index = 0; index < pComponentCount; ++index) {
        if (pComponents[index] == component) return true;
    }
    return false;
}

bool ComponentManager::Publish(const ComponentEvent& event) noexcept {
    if (pEventQueue == nullptr || event.source == nullptr || !IsRegistered(event.source)) return false;
    component* owner = const_cast<component*>(event.source->Owner());
    if (owner != nullptr) {
        owner->HandleMemberEvent(event);
        return true;
    }
    if (xQueueSend(pEventQueue, &event, 0) == pdTRUE) return true;
    pDroppedEvents.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool ComponentManager::PublishFromISR(const ComponentEvent& event, BaseType_t* higherPriorityTaskWoken) noexcept {
    if (pEventQueue == nullptr || event.source == nullptr) return false;
    // Owned components are currently polled from the component task. Reject an
    // unexpected ISR publication rather than invoking an aggregate from ISR.
    if (event.source->Owner() != nullptr) return false;
    if (xQueueSendFromISR(pEventQueue, &event, higherPriorityTaskWoken) == pdTRUE) return true;
    pDroppedEvents.fetch_add(1, std::memory_order_relaxed);
    return false;
}
