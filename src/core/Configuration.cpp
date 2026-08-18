#include "Configuration.h"
#include "Globals.h"

#include <ctype.h>

bool Configuration::Start(const char* configurationFile, uint32_t taskStackSize, UBaseType_t taskPriority) {
    if (configurationFile == nullptr || *configurationFile == '\0' || !SystemFileSystem.IsMounted()) return false;
    if (_taskHandle != nullptr) return true;

    if (_mutex == nullptr) _mutex = xSemaphoreCreateMutex();
    if (_saveMutex == nullptr) _saveMutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr || _saveMutex == nullptr) return false;

    if (!LoadConfigurationFile(configurationFile)) return false;

    const BaseType_t result = xTaskCreate(
        TaskEntry,
        "Configuration",
        taskStackSize,
        this,
        taskPriority,
        &_taskHandle
    );

    if (result != pdPASS) {
        _taskHandle = nullptr;
        return false;
    }

    return true;
}

bool Configuration::LoadConfigurationFile(const String& configurationFile) {
    if (!SystemFileSystem.IsMounted() || configurationFile.isEmpty() || _mutex == nullptr) return false;

    String content;
    if (SystemFileSystem.Read(configurationFile.c_str(), content) != FileSystem::Result::Ok) return false;

    JsonDocument document;
    if (deserializeJson(document, content) || !document.is<JsonObject>()) return false;

    Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
    if (!lock.IsLocked()) return false;

    _jsonConfiguration.clear();
    _jsonConfiguration.set(document.as<JsonVariantConst>());
    _configurationFile = configurationFile;
    _outdated = false;

    return true;
}

bool Configuration::ResetToDefaultSettings() {
    String configurationFile;

    {
        Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
        if (!lock.IsLocked() || _configurationFile.isEmpty()) return false;
        configurationFile = _configurationFile;
    }

    String defaultFile = "/def-";
    defaultFile += configurationFile[0] == '/' ? configurationFile.substring(1) : configurationFile;

    String content;
    if (SystemFileSystem.Read(defaultFile.c_str(), content) != FileSystem::Result::Ok) return false;

    JsonDocument document;
    if (deserializeJson(document, content) || !document.is<JsonObject>()) return false;

    Lock saveLock(_saveMutex, pdMS_TO_TICKS(2000));
    if (!saveLock.IsLocked() || !WriteAtomic(configurationFile, content)) return false;

    Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
    if (!lock.IsLocked()) return false;

    _jsonConfiguration.clear();
    _jsonConfiguration.set(document.as<JsonVariantConst>());
    _outdated = false;

    return true;
}

bool Configuration::SaveSettings() {
    return SaveSettingsAtomic();
}

void Configuration::Outdated() {
    Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
    if (!lock.IsLocked()) return;
    MarkOutdatedLocked();
}

bool Configuration::Critical() {
    {
        Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
        if (!lock.IsLocked()) return false;
        _outdated = false;
    }

    if (SaveSettingsAtomic()) return true;

    Outdated();
    return false;
}

void Configuration::Control() {
    bool shouldSave = false;

    {
        Lock lock(_mutex, 0);
        if (!lock.IsLocked() || !_outdated) return;

        const uint32_t now = millis();
        shouldSave = (now - _lastOutdatedMs) >= _minIntervalMs ||
                     (now - _firstOutdatedMs) >= _maxLatencyMs;

        if (shouldSave) _outdated = false;
    }

    if (shouldSave && !SaveSettingsAtomic()) Outdated();
}

void Configuration::SetMinInterval(uint32_t ms) {
    if (_mutex == nullptr) {
        _minIntervalMs = ms;
        return;
    }

    Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
    if (!lock.IsLocked()) return;
    _minIntervalMs = ms;
    if (_taskHandle != nullptr) xTaskNotifyGive(_taskHandle);
}

void Configuration::SetMaxLatency(uint32_t ms) {
    if (_mutex == nullptr) {
        _maxLatencyMs = ms;
        return;
    }

    Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
    if (!lock.IsLocked()) return;
    _maxLatencyMs = ms;
    if (_taskHandle != nullptr) xTaskNotifyGive(_taskHandle);
}

String Configuration::Get(const char* path, const char* defaultValue) const {
    Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
    if (!lock.IsLocked()) return String(defaultValue == nullptr ? "" : defaultValue);

    JsonVariantConst value = FindConst(path);
    if (value.isNull() || !value.is<const char*>()) return String(defaultValue == nullptr ? "" : defaultValue);
    return String(value.as<const char*>());
}

String Configuration::GetAt(const char* arrayPath, size_t index, const char* subPath, const char* defaultValue) const {
    Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
    if (!lock.IsLocked()) return String(defaultValue == nullptr ? "" : defaultValue);

    JsonVariantConst value = FindConstAt(arrayPath, index, subPath);
    if (value.isNull() || !value.is<const char*>()) return String(defaultValue == nullptr ? "" : defaultValue);
    return String(value.as<const char*>());
}

uint16_t Configuration::Elements(const char* path) const {
    Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
    if (!lock.IsLocked()) return 0;

    JsonVariantConst value = FindConst(path);
    if (value.is<JsonArray>()) return value.as<JsonArrayConst>().size();
    if (value.is<JsonObject>()) return value.as<JsonObjectConst>().size();
    return 0;
}

void Configuration::TaskEntry(void* parameter) {
    static_cast<Configuration*>(parameter)->Task();
}

void Configuration::Task() {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (true) {
            uint32_t waitMs = 0;
            uint32_t retryMs = 0;
            bool shouldSave = false;

            {
                Lock lock(_mutex, portMAX_DELAY);
                if (!_outdated) break;

                const uint32_t now = millis();
                const uint32_t idleElapsed = now - _lastOutdatedMs;
                const uint32_t totalElapsed = now - _firstOutdatedMs;

                if (idleElapsed >= _minIntervalMs || totalElapsed >= _maxLatencyMs) {
                    _outdated = false;
                    shouldSave = true;
                    retryMs = _minIntervalMs;
                } else {
                    const uint32_t untilIdle = _minIntervalMs - idleElapsed;
                    const uint32_t untilMaximum = _maxLatencyMs - totalElapsed;
                    waitMs = min(untilIdle, untilMaximum);
                }
            }

            if (shouldSave) {
                if (!SaveSettingsAtomic()) {
                    Outdated();
                    vTaskDelay(pdMS_TO_TICKS(retryMs));
                }
                break;
            }

            TickType_t waitTicks = pdMS_TO_TICKS(waitMs);
            if (waitTicks == 0) waitTicks = 1;
            ulTaskNotifyTake(pdTRUE, waitTicks);
        }
    }
}

bool Configuration::SaveSettingsAtomic() {
    Lock saveLock(_saveMutex, pdMS_TO_TICKS(2000));
    if (!saveLock.IsLocked() || !SystemFileSystem.IsMounted()) return false;

    String configurationFile;
    String content;

    {
        Lock lock(_mutex, DEFAULT_LOCK_TIMEOUT);
        if (!lock.IsLocked() || _configurationFile.isEmpty()) return false;

        configurationFile = _configurationFile;
        if (serializeJson(_jsonConfiguration, content) == 0) return false;
    }

    return WriteAtomic(configurationFile, content);
}

bool Configuration::WriteAtomic(const String& path, const String& content) {
    const String temporaryPath = path + ".tmp";
    SystemFileSystem.Remove(temporaryPath.c_str());

    if (SystemFileSystem.Write(temporaryPath.c_str(), content) != FileSystem::Result::Ok) return false;

    if (SystemFileSystem.Rename(temporaryPath.c_str(), path.c_str()) == FileSystem::Result::Ok) return true;

    const FileSystem::Result removeResult = SystemFileSystem.Remove(path.c_str());
    if (removeResult != FileSystem::Result::Ok && removeResult != FileSystem::Result::NotFound) {
        SystemFileSystem.Remove(temporaryPath.c_str());
        return false;
    }

    if (SystemFileSystem.Rename(temporaryPath.c_str(), path.c_str()) == FileSystem::Result::Ok) return true;

    SystemFileSystem.Remove(temporaryPath.c_str());
    return false;
}

void Configuration::MarkOutdatedLocked() {
    const uint32_t now = millis();

    if (!_outdated) _firstOutdatedMs = now;
    _lastOutdatedMs = now;
    _outdated = true;

    if (_taskHandle != nullptr) xTaskNotifyGive(_taskHandle);
}

String Configuration::Trim(const String& value) {
    int first = 0;
    int last = static_cast<int>(value.length()) - 1;

    while (first <= last && isspace(static_cast<unsigned char>(value[first]))) ++first;
    while (last >= first && isspace(static_cast<unsigned char>(value[last]))) --last;

    return first > last ? String() : value.substring(first, last + 1);
}

JsonVariantConst Configuration::FindConst(const char* path) const {
    JsonVariantConst current = _jsonConfiguration.as<JsonVariantConst>();
    if (path == nullptr || *path == '\0') return current;

    const char* cursor = path;
    while (!current.isNull() && *cursor != '\0') {
        const char* separator = strchr(cursor, '|');
        const String token = Trim(separator == nullptr ? String(cursor) : String(cursor, separator - cursor));
        if (token.isEmpty()) return JsonVariantConst();

        current = current[token];
        if (separator == nullptr) break;
        cursor = separator + 1;
    }

    return current;
}

JsonVariantConst Configuration::FindConstAt(const char* arrayPath, size_t index, const char* subPath) const {
    JsonVariantConst base = FindConst(arrayPath);
    if (!base.is<JsonArray>()) return JsonVariantConst();

    JsonArrayConst array = base.as<JsonArrayConst>();
    if (index >= array.size()) return JsonVariantConst();

    JsonVariantConst current = array[index];
    if (subPath == nullptr || *subPath == '\0') return current;

    const char* cursor = subPath;
    while (!current.isNull() && *cursor != '\0') {
        const char* separator = strchr(cursor, '|');
        const String token = Trim(separator == nullptr ? String(cursor) : String(cursor, separator - cursor));
        if (token.isEmpty()) return JsonVariantConst();

        current = current[token];
        if (separator == nullptr) break;
        cursor = separator + 1;
    }

    return current;
}

JsonVariant Configuration::Ensure(const char* path) {
    JsonVariant current = _jsonConfiguration.as<JsonVariant>();
    if (current.isNull()) current.to<JsonObject>();
    if (path == nullptr || *path == '\0') return current;

    const char* cursor = path;
    while (*cursor != '\0') {
        const char* separator = strchr(cursor, '|');
        const String token = Trim(separator == nullptr ? String(cursor) : String(cursor, separator - cursor));
        if (token.isEmpty()) return JsonVariant();

        JsonObject object = current.to<JsonObject>();
        JsonVariant next = object[token];
        if (next.isNull() || (separator != nullptr && !next.is<JsonObject>())) next.to<JsonObject>();
        current = next;

        if (separator == nullptr) break;
        cursor = separator + 1;
    }

    return current;
}

JsonVariant Configuration::EnsureFrom(JsonVariant base, const char* subPath) {
    if (base.isNull() || subPath == nullptr || *subPath == '\0') return base;

    JsonVariant current = base;
    const char* cursor = subPath;

    while (*cursor != '\0') {
        const char* separator = strchr(cursor, '|');
        const String token = Trim(separator == nullptr ? String(cursor) : String(cursor, separator - cursor));
        if (token.isEmpty()) return JsonVariant();

        JsonObject object = current.to<JsonObject>();
        JsonVariant next = object[token];
        if (next.isNull() || (separator != nullptr && !next.is<JsonObject>())) next.to<JsonObject>();
        current = next;

        if (separator == nullptr) break;
        cursor = separator + 1;
    }

    return current;
}

JsonVariant Configuration::EnsureArray(const char* arrayPath) {
    if (arrayPath == nullptr || *arrayPath == '\0') return JsonVariant();

    const char* lastSeparator = strrchr(arrayPath, '|');
    const String arrayName = Trim(lastSeparator == nullptr ? String(arrayPath) : String(lastSeparator + 1));
    if (arrayName.isEmpty()) return JsonVariant();

    JsonVariant parent = lastSeparator == nullptr
        ? _jsonConfiguration.as<JsonVariant>()
        : Ensure(String(arrayPath, lastSeparator - arrayPath).c_str());

    if (parent.isNull()) return JsonVariant();

    JsonObject object = parent.to<JsonObject>();
    JsonVariant array = object[arrayName];
    if (array.isNull()) array.to<JsonArray>();

    return array.is<JsonArray>() ? array : JsonVariant();
}

JsonVariant Configuration::EnsureArrayElement(const char* arrayPath, size_t index) {
    JsonVariant arrayVariant = EnsureArray(arrayPath);
    if (arrayVariant.isNull()) return JsonVariant();

    JsonArray array = arrayVariant.as<JsonArray>();
    while (array.size() <= index) array.add<JsonObject>();

    return array[index];
}
