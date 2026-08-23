#include "RTC.h"
#include "Globals.h"

#include <NTPClient.h>
#include <WiFiUdp.h>

void rtc::SetEpoch(time_t epoch) {
    portENTER_CRITICAL(&pMutex);
    pBaseEpoch = epoch;
    pBaseMicros = esp_timer_get_time();
    portEXIT_CRITICAL(&pMutex);
}

time_t rtc::GetEpoch() const {
    time_t baseEpoch;
    int64_t baseMicros;

    portENTER_CRITICAL(&pMutex);
    baseEpoch = pBaseEpoch;
    baseMicros = pBaseMicros;
    portEXIT_CRITICAL(&pMutex);

    const time_t elapsedSeconds = (esp_timer_get_time() - baseMicros) / 1000000;
    return baseEpoch + elapsedSeconds;
}

int8_t rtc::TimeZone() const {
    portENTER_CRITICAL(&pMutex);
    const int8_t value = pTimeZone;
    portEXIT_CRITICAL(&pMutex);
    return value;
}

void rtc::TimeZone(int8_t value) {
    portENTER_CRITICAL(&pMutex);
    pTimeZone = constrain(value, -12, 14);
    portEXIT_CRITICAL(&pMutex);
}

void rtc::GetTimeInfo(struct tm& timeInfo) const {
    time_t localEpoch = GetEpoch() + static_cast<time_t>(TimeZone()) * 3600;
    gmtime_r(&localEpoch, &timeInfo);
}

String rtc::GetDate() const {
    struct tm timeInfo;
    GetTimeInfo(timeInfo);

    char buffer[32];
    strftime(buffer, sizeof(buffer), pDateFormat, &timeInfo);

    return String(buffer);
}

bool rtc::NTPUpdate(const String& ntpserver) {
    if (Network.ConnectionMode() != network::APMode::WifiClient) return false;

    WiFiUDP udpClient;

    NTPClient ntp(udpClient, ntpserver.c_str());
    ntp.begin();

    const bool updated = ntp.update();
    if (updated) SetEpoch(ntp.getEpochTime());
    ntp.end();

    return updated;
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