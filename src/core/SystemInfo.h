#pragma once

#include <Arduino.h>

namespace SystemInfo {
    enum class MemoryUnit : uint8_t { Bytes, Kilobytes, Megabytes };

    void Hardware(String& output);
    void Memory(String& output, MemoryUnit unit = MemoryUnit::Bytes);
    [[nodiscard]] bool FileSystem(String& output);
}
