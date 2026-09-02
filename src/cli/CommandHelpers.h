#pragma once

#include <Arduino.h>

namespace cli {
    String BoolValue(bool value);
    // "configured" or "not set" - never echoes the actual value back.
    String PasswordState(const String& value);
    void AppendSetting(String& output, const String& name, const String& value);
    bool ParseBool(const String& value, bool& parsed);
    bool ParseUInt16(const String& value, uint16_t& parsed, bool allowZero);
    // Joins parameters[first..] with single spaces, so a command can accept a
    // value containing spaces (the Telnet line parser splits on them).
    String JoinParameters(String* parameters, size_t first);
}
