#pragma once

#include <Arduino.h>
#include <time.h>
#include <esp_timer.h>

class Clock {
public:
    Clock(time_t initialEpoch) : _baseEpoch(initialEpoch), _baseMicros(esp_timer_get_time()) {}

    inline void SetEpoch(time_t epoch) { _baseEpoch = epoch; _baseMicros = esp_timer_get_time(); }
    inline time_t GetEpoch() const { int64_t elapsedMicros = esp_timer_get_time() - _baseMicros; time_t elapsedSeconds = elapsedMicros / 1000000; return _baseEpoch + elapsedSeconds; }

    String GetDate() const;
    String GetTime() const;
    inline String GetDateTime() const { return GetDate() + " " + GetTime(); }

    void SetDateFormat(const char* format);
    void SetTimeFormat(const char* format);
private:
    time_t _baseEpoch;
    int64_t _baseMicros;

    static constexpr uint8_t FORMAT_SIZE = 24;

    char _dateFormat[FORMAT_SIZE] = "%d/%m/%Y";
    char _timeFormat[FORMAT_SIZE] = "%H:%M:%S";

    inline void GetTimeInfo(struct tm& timeInfo) const { time_t currentEpoch = GetEpoch(); gmtime_r(&currentEpoch, &timeInfo); }
};