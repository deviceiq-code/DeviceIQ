#include <Arduino.h>

#include "App.h"
#include "Globals.h"

void App::Start() {
    if (SystemLogger.Start()) {
        SystemLogger.Log(Logger::Type::Information, Version::Info());
        SystemLogger.Log(Logger::Type::Information, "Logger initialized");
    } else {
        SystemLogger.Log(Logger::Type::Error, "Error initializing Logger");
        return;
    }

    xTaskCreate(
        [](void* parameter) {
            Logger* logger = static_cast<Logger*>(parameter);

            uint16_t c = 0;
            char message[64];

            while (true) {
                snprintf(message, sizeof(message), "Mensagem periódica %u", static_cast<unsigned int>(c));
                logger->Log(Logger::Type::Information, message);
                vTaskDelay(pdMS_TO_TICKS(5000));

                c++;
            }
        },
        "LoggerTest",
        2048,
        &SystemLogger,
        1,
        nullptr
    );
}