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

bool ComponentManager::Start() noexcept {
    if (IsStarted()) return true;

    if (pCommandQueue == nullptr) {
        pCommandQueue = xQueueCreateStatic(
            COMMAND_QUEUE_LENGTH,
            sizeof(ComponentCommand),
            pCommandQueueStorage,
            &pCommandQueueControl
        );
    }

    if (pEventQueue == nullptr) {
        pEventQueue = xQueueCreateStatic(
            EVENT_QUEUE_LENGTH,
            sizeof(ComponentEvent),
            pEventQueueStorage,
            &pEventQueueControl
        );
    }

    if (pCommandQueue == nullptr || pEventQueue == nullptr) return false;

    for (size_t index = 0; index < pComponentCount; ++index) {
        if (pComponents[index]->Configured()) continue;
        if (!pComponents[index]->Configure()) return false;
        pComponents[index]->SetConfigured(true);
    }

    for (size_t index = 0; index < pComponentCount; ++index) {
        if (pComponents[index]->Initialized()) continue;
        if (!pComponents[index]->Initialize()) return false;
        pComponents[index]->SetInitialized(true);
    }

    const BaseType_t result = xTaskCreate(TaskEntry, "Components", TASK_STACK_SIZE, this, TASK_PRIORITY, &pTaskHandle);
    if (result != pdPASS) {
        pTaskHandle = nullptr;
        return false;
    }

    return true;
}

bool ComponentManager::SendCommand(const ComponentCommand& command, TickType_t timeout) noexcept {
    if (pCommandQueue == nullptr || command.target == nullptr || !IsRegistered(command.target)) return false;
    return xQueueSend(pCommandQueue, &command, timeout) == pdTRUE;
}

bool ComponentManager::SendCommandFromISR(const ComponentCommand& command, BaseType_t* higherPriorityTaskWoken) noexcept {
    if (pCommandQueue == nullptr || command.target == nullptr) return false;
    return xQueueSendFromISR(pCommandQueue, &command, higherPriorityTaskWoken) == pdTRUE;
}

bool ComponentManager::ReceiveEvent(ComponentEvent& event, TickType_t timeout) noexcept {
    if (pEventQueue == nullptr) return false;
    return xQueueReceive(pEventQueue, &event, timeout) == pdTRUE;
}

component* ComponentManager::FindByName(const String& name) const noexcept {
    for (size_t index = 0; index < pComponentCount; ++index) {
        if (pComponents[index]->Name() == name) return pComponents[index];
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

    if (command.code == ComponentCommand::Enable) {
        command.target->SetEnabled(true);
        command.target->EnabledChanged(true);
        return;
    }

    if (command.code == ComponentCommand::Disable) {
        command.target->EnabledChanged(false);
        command.target->SetEnabled(false);
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
    return xQueueSend(pEventQueue, &event, 0) == pdTRUE;
}

bool ComponentManager::PublishFromISR(const ComponentEvent& event, BaseType_t* higherPriorityTaskWoken) noexcept {
    if (pEventQueue == nullptr || event.source == nullptr) return false;
    return xQueueSendFromISR(pEventQueue, &event, higherPriorityTaskWoken) == pdTRUE;
}
