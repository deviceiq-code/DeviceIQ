#include "RTC.h"

String rtc::GetDate() const {
    struct tm timeInfo;
    GetTimeInfo(timeInfo);

    char buffer[32];
    strftime(buffer, sizeof(buffer), pDateFormat, &timeInfo);

    return String(buffer);
}

String rtc::GetTime() const {
    struct tm timeInfo;
    GetTimeInfo(timeInfo);

    char buffer[32];
    strftime(buffer, sizeof(buffer), pTimeFormat, &timeInfo);

    return String(buffer);
}

void rtc::SetDateFormat(const char* format) {
    if (format == nullptr) return;

    strncpy(pDateFormat, format, FORMAT_SIZE - 1);
    pDateFormat[FORMAT_SIZE - 1] = '\0';
}

void rtc::SetTimeFormat(const char* format) {
    if (format == nullptr) return;

    strncpy(pTimeFormat, format, FORMAT_SIZE - 1);
    pTimeFormat[FORMAT_SIZE - 1] = '\0';
}