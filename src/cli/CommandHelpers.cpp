#include "CommandHelpers.h"

#include <cstdlib>

String cli::BoolValue(bool value) {
    return value ? "true" : "false";
}

void cli::AppendSetting(String& output, const String& name, const String& value) {
    output += name + ": " + value + "\r\n";
}

bool cli::ParseBool(const String& value, bool& parsed) {
    if (value.equalsIgnoreCase("true") || value.equalsIgnoreCase("on") || value == "1" || value.equalsIgnoreCase("yes")) {
        parsed = true;
        return true;
    }
    if (value.equalsIgnoreCase("false") || value.equalsIgnoreCase("off") || value == "0" || value.equalsIgnoreCase("no")) {
        parsed = false;
        return true;
    }
    return false;
}

bool cli::ParseUInt16(const String& value, uint16_t& parsed, bool allowZero) {
    if (value.isEmpty()) return false;
    char* end = nullptr;
    const unsigned long number = std::strtoul(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || number > UINT16_MAX || (!allowZero && number == 0)) return false;
    parsed = static_cast<uint16_t>(number);
    return true;
}
