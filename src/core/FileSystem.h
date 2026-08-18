#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class FileSystem {
    public:
        enum class Result : uint8_t { Ok, NotInitialized, LockTimeout, InvalidArgument, NotFound, OpenFailed, ReadFailed, WriteFailed, RemoveFailed, RenameFailed, CreateDirectoryFailed, RemoveDirectoryFailed, MountFailed };
        
        FileSystem() = default;

        bool Start(bool formatOnFail = true);
        bool IsMounted() const { return _mounted; }
        bool Exists(const char* path, TickType_t timeout = pdMS_TO_TICKS(500));
        size_t Size(const char* path, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Read(const char* path, String& output, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Read(const char* path, uint8_t* buffer, size_t bufferSize, size_t& bytesRead, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Write(const char* path, const uint8_t* data, size_t length, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Write(const char* path, const String& data, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Append(const char* path, const uint8_t* data, size_t length, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Append(const char* path, const String& data, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Remove(const char* path, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Rename(const char* source, const char* destination, TickType_t timeout = pdMS_TO_TICKS(500));
        Result CreateDirectory(const char* path, TickType_t timeout = pdMS_TO_TICKS(500));
        Result RemoveDirectory(const char* path, TickType_t timeout = pdMS_TO_TICKS(500));
    private:
        class Lock {
            public:
                Lock(SemaphoreHandle_t mutex, TickType_t timeout) : _mutex(mutex), _locked(false) { if (_mutex == nullptr) return; _locked = xSemaphoreTake(_mutex, timeout) == pdTRUE; }
                ~Lock() { if (_locked && _mutex != nullptr) xSemaphoreGive(_mutex); }
                
                bool IsLocked() const {return _locked; }
            private:
                SemaphoreHandle_t _mutex = nullptr;
                bool _locked;
        };
        
        SemaphoreHandle_t _mutex = nullptr;
        bool _mounted = false;
};