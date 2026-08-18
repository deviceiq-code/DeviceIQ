#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

class Configuration {
    public:
        enum class SaveUrgency : uint8_t { Deferred, Critical };

        bool Start(const char* configurationFile, uint32_t taskStackSize = 4096, UBaseType_t taskPriority = 1);
        bool LoadConfigurationFile(const String& configurationFile);
        bool ResetToDefaultSettings();
        bool SaveSettings();

        void Outdated();
        bool Critical();
        void Control();

        void SetMinInterval(uint32_t ms);
        void SetMaxLatency(uint32_t ms);

        template<typename T>
        T Get(const char* path, T defaultValue = T{}) const {
            Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
            if (!lock.IsLocked()) return defaultValue;
            JsonVariantConst value = FindConst(path);
            return (!value.isNull() && value.is<T>()) ? value.as<T>() : defaultValue;
        }

        String Get(const char* path, const char* defaultValue = "") const;

        template<typename T>
        T GetAt(const char* arrayPath, size_t index, const char* subPath, T defaultValue = T{}) const {
            Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
            if (!lock.IsLocked()) return defaultValue;
            JsonVariantConst value = FindConstAt(arrayPath, index, subPath);
            return (!value.isNull() && value.is<T>()) ? value.as<T>() : defaultValue;
        }

        String GetAt(const char* arrayPath, size_t index, const char* subPath, const char* defaultValue = "") const;

        template<typename T>
        bool Set(const char* path, const T& value, SaveUrgency urgency = SaveUrgency::Deferred) {
            {
                Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
                if (!lock.IsLocked()) return false;
                JsonVariant destination = Ensure(path);
                if (destination.isNull()) return false;
                destination.set(value);
                MarkOutdatedLocked();
            }
            return urgency == SaveUrgency::Critical ? Critical() : true;
        }

        bool Set(const char* path, const String& value, SaveUrgency urgency = SaveUrgency::Deferred) {
            return Set(path, value.c_str(), urgency);
        }

        template<typename T>
        bool SetAt(const char* arrayPath, size_t index, const T& value, SaveUrgency urgency = SaveUrgency::Deferred) {
            {
                Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
                if (!lock.IsLocked()) return false;
                JsonVariant destination = EnsureArrayElement(arrayPath, index);
                if (destination.isNull()) return false;
                destination.set(value);
                MarkOutdatedLocked();
            }
            return urgency == SaveUrgency::Critical ? Critical() : true;
        }

        template<typename T>
        bool SetAt(const char* arrayPath, size_t index, const char* subPath, const T& value, SaveUrgency urgency = SaveUrgency::Deferred) {
            {
                Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
                if (!lock.IsLocked()) return false;
                JsonVariant element = EnsureArrayElement(arrayPath, index);
                if (element.isNull()) return false;
                JsonVariant destination = EnsureFrom(element, subPath);
                if (destination.isNull()) return false;
                destination.set(value);
                MarkOutdatedLocked();
            }
            return urgency == SaveUrgency::Critical ? Critical() : true;
        }

        uint16_t Elements(const char* path) const;

    private:
        static constexpr TickType_t DEFAULT_LOCK_TIMEOUT = pdMS_TO_TICKS(500);

        class Lock {
            public:
                Lock(SemaphoreHandle_t mutex, TickType_t timeout)
                    : _mutex(mutex), _locked(_mutex != nullptr && xSemaphoreTake(_mutex, timeout) == pdTRUE) {}
                ~Lock() { if (_locked) xSemaphoreGive(_mutex); }
                bool IsLocked() const { return _locked; }

            private:
                SemaphoreHandle_t _mutex;
                bool _locked;
        };

        mutable SemaphoreHandle_t _mutex = nullptr;
        SemaphoreHandle_t _saveMutex = nullptr;
        TaskHandle_t _taskHandle = nullptr;
        String _configurationFile;
        JsonDocument _jsonConfiguration;
        bool _outdated = false;
        uint32_t _firstOutdatedMs = 0;
        uint32_t _lastOutdatedMs = 0;
        uint32_t _minIntervalMs = 500;
        uint32_t _maxLatencyMs = 5000;

        static void TaskEntry(void* parameter);
        void Task();
        bool SaveSettingsAtomic();
        bool WriteAtomic(const String& path, const String& content);
        void MarkOutdatedLocked();

        static String Trim(const String& value);
        JsonVariantConst FindConst(const char* path) const;
        JsonVariantConst FindConstAt(const char* arrayPath, size_t index, const char* subPath = nullptr) const;
        JsonVariant Ensure(const char* path);
        JsonVariant EnsureFrom(JsonVariant base, const char* subPath);
        JsonVariant EnsureArray(const char* arrayPath);
        JsonVariant EnsureArrayElement(const char* arrayPath, size_t index);
};
