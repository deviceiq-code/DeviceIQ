#include "../core/Clock.h"

String Clock::GetDate() const {
    struct tm timeInfo;
    GetTimeInfo(timeInfo);

    char buffer[32];
    strftime(buffer, sizeof(buffer), _dateFormat, &timeInfo);

    return String(buffer);
}

String Clock::GetTime() const {
    struct tm timeInfo;
    GetTimeInfo(timeInfo);

    char buffer[32];
    strftime(buffer, sizeof(buffer), _timeFormat, &timeInfo);

    return String(buffer);
}

void Clock::SetDateFormat(const char* format) {
    if (format == nullptr) return;

    strncpy(_dateFormat, format, FORMAT_SIZE - 1);
    _dateFormat[FORMAT_SIZE - 1] = '\0';
}

void Clock::SetTimeFormat(const char* format) {
    if (format == nullptr) return;

    strncpy(_timeFormat, format, FORMAT_SIZE - 1);
    _timeFormat[FORMAT_SIZE - 1] = '\0';
}