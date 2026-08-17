#include "Logger.h"
#include "Globals.h"

bool Logger::Start() {
    _serialport.begin(115200);
    _queue = xQueueCreate(QUEUE_LENGTH, sizeof(LogMessage));

    if (_queue == nullptr) return false;

    BaseType_t result = xTaskCreate(TaskEntry, "Logger", 2048, this, 1, &_taskHandle);

    if (result != pdPASS) return false;

    return true;
}

void Logger::Log(Type type, const char* message) {
    if (_queue == nullptr || message == nullptr) return;

    LogMessage logMessage;

    logMessage.type = type;
    strncpy(logMessage.text, message, MESSAGE_SIZE - 1);
    logMessage.text[MESSAGE_SIZE - 1] = '\0';

    xQueueSend(_queue, &logMessage, 0);
}

void Logger::Task() {
    LogMessage message;

    while (true) {
        if (xQueueReceive(_queue, &message, portMAX_DELAY) == pdTRUE) {
            _serialport.print("[");
            _serialport.print(SystemClock.GetDateTime());
            _serialport.print("] ");

            _serialport.print("[");
            _serialport.print(static_cast<char>(message.type));
            _serialport.print("] ");

            _serialport.println(message.text);
        }
    }
}