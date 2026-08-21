#pragma once

#include <Arduino.h>
#include <time.h>
#include <esp_timer.h>

class rtc {
public:
    rtc(time_t initialEpoch) : pBaseEpoch(initialEpoch), pBaseMicros(esp_timer_get_time()) {}

    inline void SetEpoch(time_t epoch) { pBaseEpoch = epoch; pBaseMicros = esp_timer_get_time(); }
    inline time_t GetEpoch() const { int64_t elapsedMicros = esp_timer_get_time() - pBaseMicros; time_t elapsedSeconds = elapsedMicros / 1000000; return pBaseEpoch + elapsedSeconds; }

    String GetDate() const;
    String GetTime() const;
    inline String GetDateTime() const { return GetDate() + " " + GetTime(); }

    void SetDateFormat(const char* format);
    void SetTimeFormat(const char* format);
private:
    time_t pBaseEpoch;
    int64_t pBaseMicros;

    static constexpr uint8_t FORMAT_SIZE = 24;

    char pDateFormat[FORMAT_SIZE] = "%d/%m/%Y";
    char pTimeFormat[FORMAT_SIZE] = "%H:%M:%S";

    inline void GetTimeInfo(struct tm& timeInfo) const { time_t currentEpoch = GetEpoch(); gmtime_r(&currentEpoch, &timeInfo); }
};