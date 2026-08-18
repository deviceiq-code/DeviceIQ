#include <Arduino.h>

#include "App.h"
#include "Globals.h"

void App::Start() {
    // FileSystem
    if (SystemFileSystem.Start(true) == false) {
        Serial.println("Error initializing FileSystem object");
        return;
    }

    // Logger
    if (SystemLogger.Start() == false) {
        Serial.println("Error initializing Logger object");
        return;
    }

    // Configuration
    if (!SystemFileSystem.Exists("/configuration.json")) {
        if (SystemFileSystem.Write("/configuration.json", "{}") != FileSystem::Result::Ok) {
                SystemLogger.Log(Logger::Type::Error, "Error creating Configuration file"
            );

            return;
        }
    }

    if (SystemConfiguration.Start("/configuration.json") == false) {
        SystemLogger.Log(Logger::Type::Error, "Error initializing Configuration");
        return;
    }

    SystemLogger.Log(Logger::Type::Information, Version::Info());
    SystemLogger.Log(Logger::Type::Information, "Logger initialized");
    SystemLogger.Log(Logger::Type::Information, "FileSystem initialized");
    SystemLogger.Log(Logger::Type::Information, String("Configuration initialized - file " + String("/configuration.json") + " read"));
    
    xTaskCreate([](void* parameter) {
        while (true) {
            SystemLogger.Log(Logger::Type::Information, "Hello!");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }, "FileSystemTest", 2048, nullptr, 1, nullptr);
}