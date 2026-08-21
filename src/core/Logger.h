#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

class logger {
    public: 
        enum Endpoints : uint8_t { NoLog = 0b00000000, Serial = 0b00000001, Syslog = 0b00000010, File = 0b00000100 };
        enum LogLevels : uint8_t { Error = 0b00000001, Warning = 0b00000010, Information = 0b00000100, Debug = 0b00001000, All = 0b11111111};
        
        logger(HardwareSerial& serialPort) : pSerialport(serialPort) {}
        
        bool Start();

        [[nodiscard]] const String& SyslogServerHost() const noexcept { return pSyslogServerHost; }
        void SyslogServerHost(String value) noexcept { value.trim(); value.toLowerCase(); if (pSyslogServerHost == value) return; pSyslogServerHost = std::move(value); pSyslogAddressValid = false; }
        void SyslogServerPort(uint16_t value) { pSyslogServerPort = value; }
        uint16_t SyslogServerPort() const { return pSyslogServerPort; }
        Endpoints Endpoint() const { return pEndpoint; }
        void Endpoint(Endpoints value) { pEndpoint = value; }
        void Endpoint(uint8_t value) { pEndpoint = static_cast<Endpoints>(value); }
        LogLevels LogLevel() const { return pLogLevel; }
        void LogLevel(LogLevels value) { pLogLevel = value; }
        void LogLevel(uint8_t value) { pLogLevel = static_cast<LogLevels>(value); }

        bool Log(const char* message, LogLevels loglevel);
        inline bool Log(const String& message, LogLevels loglevel) { return Log(message.c_str(), loglevel); }
    private:
        static constexpr uint16_t QUEUE_LENGTH = 20;
        static constexpr uint16_t MESSAGE_SIZE = 128;

        IPAddress pSyslogAddress;
        HardwareSerial& pSerialport;
        QueueHandle_t pQueue = nullptr;
        TaskHandle_t pTaskHandle = nullptr;
        Endpoints pEndpoint = Endpoints::Serial;
        LogLevels pLogLevel = LogLevels::All;
        String pSyslogServerHost = "";
        uint16_t pSyslogServerPort = 0;
        WiFiUDP pUDPClient;
        bool pUDPReady = false;
        bool pSyslogAddressValid = false;

        struct LogMessage { char text[MESSAGE_SIZE]; LogLevels loglevel; };

        void LogToSerial(const char* message, LogLevels loglevel);
        void LogToFile(const char* message, LogLevels loglevel);
        void LogToSyslog(const char* message, LogLevels loglevel);

        bool ResolveSyslogAddress();
        
        inline static void TaskEntry(void* parameter) { logger* _logger = static_cast<logger*>(parameter); _logger->Task(); }
        void Task();
};