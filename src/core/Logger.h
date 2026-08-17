#pragma once

#include <Arduino.h>

class Logger {
    public: 
        enum class Type : char {Information = 'I', Warning = 'W', Error = 'E', Debug = 'D'};
        
        Logger(HardwareSerial& serialPort) : _serialport(serialPort) {}
        
        bool Start();
        void Log(Type type, const char* message);
        inline void Log(Type type, const String& message) { Log(type, message.c_str()); }
    private:
        static constexpr uint16_t QUEUE_LENGTH = 20;
        static constexpr uint16_t MESSAGE_SIZE = 128;
        
        struct LogMessage {Type type; char text[MESSAGE_SIZE];};
        
        HardwareSerial& _serialport;
        QueueHandle_t _queue = nullptr;
        TaskHandle_t _taskHandle = nullptr;
        
        inline static void TaskEntry(void* parameter) { Logger* logger = static_cast<Logger*>(parameter); logger->Task(); }
        void Task();
};