#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// Serves the DeviceIQ web UI (login, dashboard, setup) from LittleFS and
// authenticates against the same user store as the Telnet CLI. Named
// "httpserver" (not "webserver") to avoid colliding with the Arduino
// WebServer class this is built on.
class httpserver {
    public:
        // Hard upper bound on session slots (physically allocated). The
        // runtime-configurable session limit (MaxSessions) is clamped to
        // this range.
        static constexpr size_t MIN_SESSIONS = 1;
        static constexpr size_t MAX_SESSIONS = 8;

        httpserver() noexcept;
        ~httpserver();
        httpserver(const httpserver&) = delete;
        httpserver& operator=(const httpserver&) = delete;

        [[nodiscard]] bool Start();
        void Stop();

        void Enabled(bool value) noexcept;
        [[nodiscard]] bool Enabled() const noexcept;
        void Port(uint16_t value) noexcept;
        [[nodiscard]] uint16_t Port() const noexcept;
        // Applies immediately; no restart required.
        void IdleTimeout(uint32_t valueMs) noexcept;
        [[nodiscard]] uint32_t IdleTimeout() const noexcept;
        // Applies immediately; no restart required. Clamped to
        // [MIN_SESSIONS, MAX_SESSIONS].
        void MaxSessions(size_t value) noexcept;
        [[nodiscard]] size_t MaxSessions() const noexcept;

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

        struct Session {
            char token[33] = {0};
            String username;
            bool admin = false;
            bool active = false;
            TickType_t lastActivityAt = 0;
        };

        static constexpr uint32_t TASK_STACK_SIZE = 8192;
        static constexpr UBaseType_t TASK_PRIORITY = 1;
        static constexpr TickType_t TASK_DELAY = pdMS_TO_TICKS(10);
        static constexpr uint32_t STOP_NOTIFICATION = 1UL << 0;

        StaticSemaphore_t pMutexStorage{};
        SemaphoreHandle_t pMutex = nullptr;
        TaskHandle_t pTaskHandle = nullptr;
        uint16_t pPort = 80;
        bool pEnabled = true;
        uint32_t pIdleTimeoutMs = 1800000; // 30 minutes
        size_t pMaxSessions = 4;
        Session pSessions[MAX_SESSIONS];

        static void TaskEntry(void* parameter);
        void Task();
        void RegisterRoutes(WebServer& server);

        void HandleIndex(WebServer& server);
        void HandleDashboard(WebServer& server);
        void HandleConfig(WebServer& server);
        void HandleStyle(WebServer& server);
        void HandleLoginPost(WebServer& server);
        void HandleLogoutPost(WebServer& server);
        void HandleSessionGet(WebServer& server);
        void HandleNotFound(WebServer& server);

        void ServeFile(WebServer& server, const char* path, const char* contentType);
        void ServeProtectedFile(WebServer& server, const char* path, const char* contentType, bool requireAdmin);

        [[nodiscard]] Session* FindSession(const String& token) noexcept;
        [[nodiscard]] Session* CreateSession(const String& username, bool admin) noexcept;
        void DestroySession(const String& token) noexcept;
        [[nodiscard]] static String SessionTokenFromCookie(WebServer& server);
        [[nodiscard]] Session* AuthenticatedSession(WebServer& server) noexcept;
        [[nodiscard]] static String GenerateToken();
};
