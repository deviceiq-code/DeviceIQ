#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "Component.h"

class ComponentManager final : private ComponentRuntime {
    public:
        ComponentManager() = default;
        ~ComponentManager() = default;

        ComponentManager(const ComponentManager&) = delete;
        ComponentManager& operator=(const ComponentManager&) = delete;

        // Components must have static or otherwise application-long lifetime.
        // Registration is allowed only before Start().
        [[nodiscard]] bool Register(component& component) noexcept;
        [[nodiscard]] bool Start() noexcept;

        [[nodiscard]] bool SendCommand(const ComponentCommand& command, TickType_t timeout = 0) noexcept override;
        [[nodiscard]] bool SendCommandFromISR(const ComponentCommand& command, BaseType_t* higherPriorityTaskWoken) noexcept override;
        [[nodiscard]] bool ReceiveEvent(ComponentEvent& event, TickType_t timeout = portMAX_DELAY) noexcept;

        [[nodiscard]] bool IsStarted() const noexcept { return pTaskHandle != nullptr; }
        [[nodiscard]] size_t Count() const noexcept { return pComponentCount; }
        [[nodiscard]] component* FindByName(const String& name) const noexcept;
        [[nodiscard]] component* FindByID(int16_t id) const noexcept;
        [[nodiscard]] component* At(size_t index) const noexcept;

    private:
        static constexpr size_t MAX_COMPONENTS = 32;
        static constexpr UBaseType_t COMMAND_QUEUE_LENGTH = 16;
        static constexpr UBaseType_t EVENT_QUEUE_LENGTH = 32;
        static constexpr uint32_t TASK_STACK_SIZE = 4096;
        static constexpr UBaseType_t TASK_PRIORITY = 2;
        static constexpr uint32_t CONTROL_INTERVAL_MS = 10;

        static void TaskEntry(void* parameter);
        void Task();
        void ProcessCommands();
        void ProcessCommand(const ComponentCommand& command);
        [[nodiscard]] bool IsRegistered(const component* component) const noexcept;

        bool Publish(const ComponentEvent& event) noexcept override;
        bool PublishFromISR(const ComponentEvent& event, BaseType_t* higherPriorityTaskWoken) noexcept override;

        component* pComponents[MAX_COMPONENTS]{};
        size_t pComponentCount = 0;

        StaticQueue_t pCommandQueueControl{};
        uint8_t pCommandQueueStorage[COMMAND_QUEUE_LENGTH * sizeof(ComponentCommand)]{};
        QueueHandle_t pCommandQueue = nullptr;

        StaticQueue_t pEventQueueControl{};
        uint8_t pEventQueueStorage[EVENT_QUEUE_LENGTH * sizeof(ComponentEvent)]{};
        QueueHandle_t pEventQueue = nullptr;

        TaskHandle_t pTaskHandle = nullptr;
};
