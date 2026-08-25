#pragma once

#include <Arduino.h>

class App {
    public:
        App() = default;
        void Start();

    private:
        bool InitializeFileSystem();
        bool InitializeLogger();
        bool InitializeNetwork();
        bool InitializeClock();
        bool InitializeComponents();
        bool InitializeStatePersistence();
        bool InitializeTelnetServer();

        bool RegisterTelnetCommands();
        void DeviceRestart();
        void LogConfigurationStatus(bool configurationLoaded);

        static void LogNetworkStatus();
        static void ClockTaskEntry(void* parameter);
        static void StatePersistenceTaskEntry(void* parameter);
        void StatePersistenceTask();
        void ClockTask();

        TaskHandle_t pClockTaskHandle = nullptr;
        TaskHandle_t pStatePersistenceTaskHandle = nullptr;

        static constexpr uint32_t CLOCK_TASK_STACK_SIZE = 4096;
        static constexpr UBaseType_t CLOCK_TASK_PRIORITY = 1;
        static constexpr uint32_t STATE_PERSISTENCE_TASK_STACK_SIZE = 4096;
        static constexpr UBaseType_t STATE_PERSISTENCE_TASK_PRIORITY = 1;
        static constexpr uint32_t NTP_OFFLINE_RETRY_MS = 5000;
        static constexpr uint32_t NTP_FAILURE_RETRY_MS = 60000;
        static constexpr uint32_t NTP_UPDATE_INTERVAL_MS = 3600000;
};
