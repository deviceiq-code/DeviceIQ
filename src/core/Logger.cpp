#include "Logger.h"
#include "Globals.h"

bool logger::Start() {
    if (pTaskHandle != nullptr) return true;

    pSerialport.begin(115200);
    pQueue = xQueueCreate(QUEUE_LENGTH, sizeof(LogMessage));

    if (pQueue == nullptr) return false;

    BaseType_t result = xTaskCreate(TaskEntry, "Logger", 4096, this, 1, &pTaskHandle);
    if (result != pdPASS) {
        vQueueDelete(pQueue);
        pQueue = nullptr;
        pTaskHandle = nullptr;
        return false;
    }

    return true;
}

bool logger::ResolveSyslogAddress() {
    if (pSyslogAddressValid) return true;

    if (pSyslogAddress.fromString(pSyslogServerHost)) {
        pSyslogAddressValid = true;
        return true;
    }

    IPAddress resolved;
    if (!WiFi.hostByName(pSyslogServerHost.c_str(), resolved)) {
        return false;
    }

    pSyslogAddress = resolved;
    pSyslogAddressValid = true;
    return true;
}

bool logger::Log(const char* message, LogLevels loglevel) {
    if (pQueue == nullptr || message == nullptr) return false;
    if ((pLogLevel & loglevel) == 0) return true;

    LogMessage logMessage;

    logMessage.loglevel = loglevel;
    strncpy(logMessage.text, message, MESSAGE_SIZE - 1);
    logMessage.text[MESSAGE_SIZE - 1] = '\0';

    return xQueueSend(pQueue, &logMessage, 0) == pdTRUE;
}

void logger::LogToSerial(const char* message, LogLevels loglevel) {
    char levelChar = 'U';

    switch (loglevel) {
        case LogLevels::Error: {
            levelChar = 'E';
        } break;
        case LogLevels::Warning: {
            levelChar = 'W';
        } break;
        case LogLevels::Information: {
            levelChar = 'I';
        } break;
        case LogLevels::Debug: {
            levelChar = 'D';
        } break;
        default: {

        } break;
    }

    String line = "[" + Clock.GetDateTime() + "] [" + levelChar + "] " + message;
    pSerialport.println(line);
}

void logger::LogToFile(const char* message, LogLevels loglevel) {
    char levelChar = 'U';

    switch (loglevel) {
        case LogLevels::Error: {
            levelChar = 'E';
        } break;
        case LogLevels::Warning: {
            levelChar = 'W';
        } break;
        case LogLevels::Information: {
            levelChar = 'I';
        } break;
        case LogLevels::Debug: {
            levelChar = 'D';
        } break;
        default: {

        } break;
    }

    char line[MESSAGE_SIZE + 32];
    const int length = snprintf(
        line,
        sizeof(line),
        "%lu|%c|%s\n",
        static_cast<unsigned long>(Clock.GetEpoch()),
        levelChar,
        message
    );

    if (length <= 0 || static_cast<size_t>(length) >= sizeof(line)) return;

    FileSystem.AppendRotating(Defaults.LogFileName, reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(length), Defaults.Log.MaxFileSize);
}

void logger::LogToSyslog(const char* message, LogLevels loglevel) {
    #if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
    if (xPortInIsrContext()) return;
    #endif

    if (pSyslogServerHost.isEmpty() || pSyslogServerPort == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (!WiFi.localIP()) return;

    if (!pUDPReady) {
        if (!pUDPClient.begin(0)) return;
        pUDPReady = true;
    }

    if (!ResolveSyslogAddress()) return;

    uint8_t severity;

    switch (loglevel) {
        case LogLevels::Error: {
            severity = 3;
        } break;
        case LogLevels::Warning: {
            severity = 4;
        } break;
        case LogLevels::Information: {
            severity = 6;
        } break;
        case LogLevels::Debug: {
            severity = 7;
        } break;
        default: {

        } return;
    }

    time_t now = Clock.GetEpoch();
    struct tm timeInfo;
    gmtime_r(&now, &timeInfo);

    char timestamp[21];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeInfo);

    String packet;
    packet.reserve(strlen(message) + 80);
    packet += '<';
    packet += String(8 + severity); // facility user = 1
    packet += ">1 ";
    packet += timestamp;
    packet += ' ';
    packet += Defaults.Network.Hostname();
    packet += " " + String(Version::ProductFamily) + " - - - ";
    packet += message;

    if (!pUDPClient.beginPacket(pSyslogAddress, pSyslogServerPort)) {
        pUDPClient.stop();
        pUDPReady = false;
        return;
    }

    if (pUDPClient.write(reinterpret_cast<const uint8_t*>(packet.c_str()), packet.length()) != packet.length()) {
        pUDPClient.stop();
        pUDPReady = false;
        return;
    }

    if (!pUDPClient.endPacket()) {
        pUDPClient.stop();
        pUDPReady = false;
    }
}

void logger::Task() {
    LogMessage message;

    while (true) {
        if (xQueueReceive(pQueue, &message, portMAX_DELAY) == pdTRUE) {
            if (pEndpoint & Endpoints::Serial) {
                LogToSerial(message.text, message.loglevel);
            }

            if (pEndpoint & Endpoints::File) {
                LogToFile(message.text, message.loglevel);
            }

            if (pEndpoint & Endpoints::Syslog) {
                LogToSyslog(message.text, message.loglevel);
            }
        }
    }
}
