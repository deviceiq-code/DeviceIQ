#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

class logger {
    public: 
        enum Endpoints : uint8_t { NoLog = 0b00000000, Serial = 0b00000001, Syslog = 0b00000010, File = 0b00000100 };
        enum LogLevels : uint8_t { Error = 0b00000001, Warning = 0b00000010, Information = 0b00000100, Debug = 0b00001000, All = 0b11111111};
        
        logger(HardwareSerial& serialPort) : _serialport(serialPort) {}
        
        bool Start();

        [[nodiscard]] const String& SyslogServerHost() const noexcept { return _syslogServerHost; }
        void SyslogServerHost(String value) noexcept { value.trim(); value.toLowerCase(); if (_syslogServerHost == value) return; _syslogServerHost = std::move(value); _syslogAddressValid = false; }
        void SyslogServerPort(uint16_t value) { _syslogServerPort = value; }
        uint16_t SyslogServerPort() const { return _syslogServerPort; }
        Endpoints Endpoint() const { return _endpoint; }
        void Endpoint(Endpoints value) { _endpoint = value; }
        void Endpoint(uint8_t value) { _endpoint = static_cast<Endpoints>(value); }
        LogLevels LogLevel() const { return _logLevel; }
        void LogLevel(LogLevels value) { _logLevel = value; }
        void LogLevel(uint8_t value) { _logLevel = static_cast<LogLevels>(value); }

        bool Log(const char* message, LogLevels loglevel);
        inline bool Log(const String& message, LogLevels loglevel) { return Log(message.c_str(), loglevel); }
    private:
        static constexpr uint16_t QUEUE_LENGTH = 20;
        static constexpr uint16_t MESSAGE_SIZE = 128;

        IPAddress _syslogAddress;
        HardwareSerial& _serialport;
        QueueHandle_t _queue = nullptr;
        TaskHandle_t _taskHandle = nullptr;
        Endpoints _endpoint = Endpoints::Serial;
        LogLevels _logLevel = LogLevels::All;
        String _syslogServerHost = "";
        uint16_t _syslogServerPort = 0;
        WiFiUDP _UDPClient;
        bool _udpReady = false;
        bool _syslogAddressValid = false;

        struct LogMessage { char text[MESSAGE_SIZE]; LogLevels loglevel; };

        void LogToSerial(const char* message, LogLevels loglevel);
        void LogToFile(const char* message, LogLevels loglevel);
        void LogToSyslog(const char* message, LogLevels loglevel);

        bool ResolveSyslogAddress();
        
        inline static void TaskEntry(void* parameter) { logger* _logger = static_cast<logger*>(parameter); _logger->Task(); }
        void Task();
};