#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

class telnetserver {
    public:
        static constexpr size_t MAX_COMMAND_PARAMETERS = 10;

        struct SessionInfo {
            IPAddress remoteIP;
            uint16_t remotePort = 0;
            String user;
            bool admin = false;
        };

        using command_callback_t = std::function<void(WiFiClient&, String*)>;
        using session_callback_t = std::function<void(WiFiClient&, const SessionInfo&)>;

        telnetserver() noexcept;
        ~telnetserver();
        telnetserver(const telnetserver&) = delete;
        telnetserver& operator=(const telnetserver&) = delete;

        [[nodiscard]] bool Start();
        void Stop();

        void Enabled(bool value) noexcept;
        [[nodiscard]] bool Enabled() const noexcept;
        void Port(uint16_t value) noexcept;
        [[nodiscard]] uint16_t Port() const noexcept;

        void WelcomeMessage(String value);
        [[nodiscard]] String WelcomeMessage() const;
        void OnSessionBegin(session_callback_t callback);
        void OnSessionEnd(session_callback_t callback);
        bool OnCommand(String command, String helpMessage, command_callback_t callback, bool admin = false);
        [[nodiscard]] bool SetSessionIdentity(WiFiClient& client, String username, bool admin);

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

        enum class ParserState : uint8_t { Data, Command, Option, Subnegotiation, SubnegotiationIAC };

        struct Command {
            String name;
            String help;
            command_callback_t callback;
            bool admin = false;
        };

        struct Session {
            WiFiClient client;
            String input;
            String lastInput;
            String user;
            ParserState parserState = ParserState::Data;
            uint8_t negotiationCommand = 0;
            bool active = false;
            bool admin = false;
            bool lastWasCarriageReturn = false;
        };

        static constexpr size_t MAX_SESSIONS = 3;
        static constexpr size_t MAX_COMMANDS = 16;
        static constexpr size_t MAX_INPUT_LENGTH = 128;
        static constexpr size_t MAX_BYTES_PER_CYCLE = 256;
        static constexpr uint32_t TASK_STACK_SIZE = 4096;
        static constexpr UBaseType_t TASK_PRIORITY = 1;
        static constexpr TickType_t TASK_DELAY = pdMS_TO_TICKS(10);
        static constexpr uint32_t STOP_NOTIFICATION = 1UL << 0;

        static constexpr uint8_t TELNET_SE = 240;
        static constexpr uint8_t TELNET_SB = 250;
        static constexpr uint8_t TELNET_WILL = 251;
        static constexpr uint8_t TELNET_WONT = 252;
        static constexpr uint8_t TELNET_DO = 253;
        static constexpr uint8_t TELNET_DONT = 254;
        static constexpr uint8_t TELNET_IAC = 255;

        StaticSemaphore_t pMutexStorage{};
        SemaphoreHandle_t pMutex = nullptr;
        TaskHandle_t pTaskHandle = nullptr;
        uint16_t pPort = 23;
        bool pEnabled = true;
        String pWelcomeMessage = "DeviceIQ Telnet Server";
        session_callback_t pOnSessionBegin;
        session_callback_t pOnSessionEnd;
        Command pCommands[MAX_COMMANDS];
        size_t pCommandCount = 0;
        Session pSessions[MAX_SESSIONS];

        static void TaskEntry(void* parameter);
        void Task();
        void RegisterBuiltInCommands();
        void AcceptClient(WiFiServer& server);
        void PollSessions();
        void ProcessByte(Session& session, uint8_t value);
        void ProcessLine(Session& session);
        void ProcessNegotiation(Session& session, uint8_t value);
        void CloseSession(Session& session);
        void WritePrompt(Session& session);
        [[nodiscard]] Session* FindSession(WiFiClient& client);
        [[nodiscard]] SessionInfo GetSessionInfo(const Session& session) const;
};
