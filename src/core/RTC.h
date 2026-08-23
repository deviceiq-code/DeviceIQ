#pragma once

#include <Arduino.h>
#include <time.h>
#include <esp_timer.h>

class rtc {
public:
    rtc(time_t initialEpoch) : pBaseEpoch(initialEpoch), pBaseMicros(esp_timer_get_time()) {}

    void SetEpoch(time_t epoch);
    time_t GetEpoch() const;

    bool NTPUpdate(const String& ntpserver);

    String GetDate() const;
    String GetTime() const;
    inline String GetDateTime() const { return GetDate() + " " + GetTime(); }

    void SetDateFormat(const char* format);
    void SetTimeFormat(const char* format);

    int8_t TimeZone() const;
    void TimeZone(int8_t value);
private:
    time_t pBaseEpoch;
    int64_t pBaseMicros;
    int8_t pTimeZone = -3;
    mutable portMUX_TYPE pMutex = portMUX_INITIALIZER_UNLOCKED;

    static constexpr uint8_t FORMAT_SIZE = 24;

    char pDateFormat[FORMAT_SIZE] = "%d/%m/%Y";
    char pTimeFormat[FORMAT_SIZE] = "%H:%M:%S";

    void GetTimeInfo(struct tm& timeInfo) const;
};