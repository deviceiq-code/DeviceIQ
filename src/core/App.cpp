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
    if (!SystemFileSystem.Exists(Defaults.ConfigFileName)) {
        if (SystemFileSystem.Write(Defaults.ConfigFileName, "{}") != FileSystem::Result::Ok) {
                SystemLogger.Log(Logger::Type::Error, "Error creating Configuration file"
            );

            return;
        }
    }

    if (SystemConfiguration.Start(Defaults.ConfigFileName) == false) {
        SystemLogger.Log(Logger::Type::Error, "Error initializing Configuration");
        return;
    }

    SystemLogger.Log(Logger::Type::Information, Version::Info());
    SystemLogger.Log(Logger::Type::Information, "Logger initialized");
    SystemLogger.Log(Logger::Type::Information, "FileSystem initialized");
    SystemLogger.Log(Logger::Type::Information, String("Configuration initialized - file " + String(Defaults.ConfigFileName) + " read"));
    
    xTaskCreate([](void* parameter) {
        while (true) {
            SystemLogger.Log(Logger::Type::Information, "Hello!");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }, "FileSystemTest", 2048, nullptr, 1, nullptr);
}