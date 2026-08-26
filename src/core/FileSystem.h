#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class filesystem {
    public:
        enum class Result : uint8_t { Ok, NotInitialized, LockTimeout, InvalidArgument, NotFound, OpenFailed, ReadFailed, WriteFailed, RemoveFailed, RenameFailed, CreateDirectoryFailed, RemoveDirectoryFailed, MountFailed };
        struct Statistics {
            size_t totalBytes = 0;
            size_t usedBytes = 0;
            size_t fileBytes = 0;
            size_t largestFileBytes = 0;
            size_t files = 0;
            size_t directories = 0;
        };
        
        filesystem() = default;

        bool Start(bool formatOnFail = true);
        bool IsMounted() const { return pMounted; }
        bool Exists(const char* path, TickType_t timeout = pdMS_TO_TICKS(500));
        bool GetStatistics(Statistics& output, TickType_t timeout = pdMS_TO_TICKS(500));
        bool Exists(const String& path, TickType_t timeout = pdMS_TO_TICKS(500)) { return Exists(path.c_str(), timeout); }
        size_t Size(const char* path, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Read(const char* path, String& output, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Read(const char* path, uint8_t* buffer, size_t bufferSize, size_t& bytesRead, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Write(const char* path, const uint8_t* data, size_t length, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Write(const char* path, const String& data, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Append(const char* path, const uint8_t* data, size_t length, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Append(const char* path, const String& data, TickType_t timeout = pdMS_TO_TICKS(500));
        Result AppendRotating(const char* path, const uint8_t* data, size_t length, size_t maxFileSize, TickType_t timeout = pdMS_TO_TICKS(500));
        Result AppendRotating(const char* path, const String& data, size_t maxFileSize, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Remove(const char* path, TickType_t timeout = pdMS_TO_TICKS(500));
        Result Remove(const String& path, TickType_t timeout = pdMS_TO_TICKS(500)) { return Remove(path.c_str(), timeout); }
        Result Rename(const char* source, const char* destination, TickType_t timeout = pdMS_TO_TICKS(500));
        Result CreateDirectory(const char* path, TickType_t timeout = pdMS_TO_TICKS(500));
        Result RemoveDirectory(const char* path, TickType_t timeout = pdMS_TO_TICKS(500));
    private:
        class Lock {
            public:
                Lock(SemaphoreHandle_t mutex, TickType_t timeout) : pMutex(mutex), pLocked(false) { if (pMutex == nullptr) return; pLocked = xSemaphoreTake(pMutex, timeout) == pdTRUE; }
                ~Lock() { if (pLocked && pMutex != nullptr) xSemaphoreGive(pMutex); }
                
                bool IsLocked() const {return pLocked; }
            private:
                SemaphoreHandle_t pMutex = nullptr;
                bool pLocked;
        };
        
        SemaphoreHandle_t pMutex = nullptr;
        bool pMounted = false;
};
