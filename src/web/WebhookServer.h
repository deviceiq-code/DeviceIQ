#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// Minimal standalone HTTP server dedicated to triggering component commands
// from external automation systems (Home Assistant, Node-RED, shortcuts,
// scripts) via a shared token instead of a login session. Runs on its own
// port, entirely independent of httpserver (the browser-facing admin UI) -
// no sessions, no cookies, no file serving.
class webhookserver {
    public:
        webhookserver() noexcept;
        ~webhookserver();
        webhookserver(const webhookserver&) = delete;
        webhookserver& operator=(const webhookserver&) = delete;

        [[nodiscard]] bool Start();
        void Stop();

        void Enabled(bool value) noexcept;
        [[nodiscard]] bool Enabled() const noexcept;
        void Port(uint16_t value) noexcept;
        [[nodiscard]] uint16_t Port() const noexcept;

    private:
        class Lock {
            public:
                explicit Lock(SemaphoreHandle_t mutex) noexcept : pMutex(mutex), pLocked(mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {}
                ~Lock() { if (pLocked) xSemaphoreGive(pMutex); }
                Lock(const Lock&) = delete;
                Lock& operator=(const Lock&) = delete;
                [[nodiscard]] bool IsLocked() const noexcept { return pLocked; }
            private:
                SemaphoreHandle_t pMutex;
                bool pLocked;
        };

        static constexpr uint32_t TASK_STACK_SIZE = 8192;
        static constexpr UBaseType_t TASK_PRIORITY = 1;
        static constexpr TickType_t TASK_DELAY = pdMS_TO_TICKS(10);
        static constexpr uint32_t STOP_NOTIFICATION = 1UL << 0;

        StaticSemaphore_t pMutexStorage{};
        SemaphoreHandle_t pMutex = nullptr;
        TaskHandle_t pTaskHandle = nullptr;
        uint16_t pPort = 81;
        bool pEnabled = false;

        static void TaskEntry(void* parameter);
        void Task();
        void RegisterRoutes(WebServer& server);

        void HandleComponentSet(WebServer& server);
        void HandleComponentGet(WebServer& server);
        void HandleNotFound(WebServer& server);

        [[nodiscard]] static bool TokenValid(const String& provided) noexcept;
};
