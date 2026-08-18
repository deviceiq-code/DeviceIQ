#include <Arduino.h>

#include "App.h"
#include "Globals.h"

void App::Test() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("Antes do LittleFS");

    bool mounted = LittleFS.begin(false);

    Serial.printf("Depois do LittleFS: %s\n",
                  mounted ? "OK" : "FALHOU");
}

void App::Start() {
    Serial.begin(115200);

    // FileSystem
    if (!SystemFileSystem.Start(true)) {
        Serial.println("Error initializing FileSystem");
        return;
    }

    // Logger
    if (!SystemLogger.Start()) {
        Serial.println("Error initializing Logger");
        return;
    }

    SystemLogger.Log(Logger::Type::Information, Version::Info());
    SystemLogger.Log(Logger::Type::Information, "Logger initialized");
    SystemLogger.Log(Logger::Type::Information, "FileSystem initialized");

    xTaskCreate([](void* parameter) {
        while (true) {
            SystemLogger.Log(Logger::Type::Information, "Hello!");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }, "FileSystemTest", 2048, nullptr, 1, nullptr);
}