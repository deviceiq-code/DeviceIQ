#pragma once

#include <Arduino.h>

namespace cli {
    String BoolValue(bool value);
    void AppendSetting(String& output, const String& name, const String& value);
    bool ParseBool(const String& value, bool& parsed);
    bool ParseUInt16(const String& value, uint16_t& parsed, bool allowZero);
}
