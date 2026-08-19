#include "Logger.h"
#include "Globals.h"

bool Logger::Start() {
    if (_taskHandle != nullptr) return true;

    _serialport.begin(115200);
    _queue = xQueueCreate(QUEUE_LENGTH, sizeof(LogMessage));

    if (_queue == nullptr) return false;

    BaseType_t result = xTaskCreate(TaskEntry, "Logger", 4096, this, 1, &_taskHandle);
    if (result != pdPASS) {
        vQueueDelete(_queue);
        _queue = nullptr;
        _taskHandle = nullptr;
        return false;
    }

    return true;
}

bool Logger::ResolveSyslogAddress() {
    if (_syslogAddressValid) return true;

    if (_syslogAddress.fromString(_syslogServerHost)) {
        _syslogAddressValid = true;
        return true;
    }

    IPAddress resolved;
    if (!WiFi.hostByName(_syslogServerHost.c_str(), resolved)) {
        return false;
    }

    _syslogAddress = resolved;
    _syslogAddressValid = true;
    return true;
}

bool Logger::Log(const char* message, LogLevels loglevel) {
    if (_queue == nullptr || message == nullptr) return false;
    if ((_logLevel & loglevel) == 0) return true;

    LogMessage logMessage;

    logMessage.loglevel = loglevel;
    strncpy(logMessage.text, message, MESSAGE_SIZE - 1);
    logMessage.text[MESSAGE_SIZE - 1] = '\0';

    return xQueueSend(_queue, &logMessage, 0) == pdTRUE;
}

void Logger::LogToSerial(const char* message, LogLevels loglevel) {
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

    String line = "[" + SystemClock.GetDateTime() + "] [" + levelChar + "] " + message;
    _serialport.println(line);
}

void Logger::LogToFile(const char* message, LogLevels loglevel) {
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

    String line = "[" + SystemClock.GetDateTime() + "] [" + levelChar + "] " + message + "\n";
    SystemFileSystem.Append(Defaults.LogFileName, line);
}

void Logger::LogToSyslog(const char* message, LogLevels loglevel) {
    #if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
    if (xPortInIsrContext()) return;
    #endif

    if (_syslogServerHost.isEmpty() || _syslogServerPort == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (!WiFi.localIP()) return;

    if (!_udpReady) {
        if (!_UDPClient.begin(0)) return;
        _udpReady = true;
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

    time_t now = SystemClock.GetEpoch();
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

    if (!_UDPClient.beginPacket(_syslogAddress, _syslogServerPort)) {
        _UDPClient.stop();
        _udpReady = false;
        return;
    }

    if (_UDPClient.write(reinterpret_cast<const uint8_t*>(packet.c_str()), packet.length()) != packet.length()) {
        _UDPClient.stop();
        _udpReady = false;
        return;
    }

    if (!_UDPClient.endPacket()) {
        _UDPClient.stop();
        _udpReady = false;
    }
}

void Logger::Task() {
    LogMessage message;

    while (true) {
        if (xQueueReceive(_queue, &message, portMAX_DELAY) == pdTRUE) {
            if (_endpoint & Endpoints::Serial) {
                LogToSerial(message.text, message.loglevel);
            }

            if (_endpoint & Endpoints::File) {
                LogToFile(message.text, message.loglevel);
            }

            if (_endpoint & Endpoints::Syslog) {
                LogToSyslog(message.text, message.loglevel);
            }
        }
    }
}