#include <Arduino.h>

#include "App.h"
#include "Globals.h"

void App::Start() {
    // Clock
    SystemClock.SetEpoch(Defaults.InitialTimeAndDate);

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
            SystemLogger.Log("Error creating Configuration file", Logger::LogLevels::Error);
            return;
        }
    }

    if (SystemConfiguration.Start(Defaults.ConfigFileName) == false) {
        SystemLogger.Log("Error initializing Configuration", Logger::LogLevels::Error);
        return;
    }

    SystemLogger.LogLevel(Defaults.Log.Level);
    SystemLogger.Endpoint(Logger::Endpoints::Serial | Logger::Endpoints::File | Logger::Endpoints::Syslog);
    SystemLogger.SyslogServerHost(Defaults.Log.SyslogServer);
    SystemLogger.SyslogServerPort(Defaults.Log.SyslogPort);

    SystemLogger.Log(Version::Info(), Logger::LogLevels::Information);
    SystemLogger.Log("Logger initialized", Logger::LogLevels::Information);
    SystemLogger.Log("FileSystem initialized", Logger::LogLevels::Information);
    SystemLogger.Log(String("Configuration initialized - file " + String(Defaults.ConfigFileName) + " read"), Logger::LogLevels::Information);
    
    xTaskCreate([](void* parameter) {
        while (true) {
            SystemLogger.Log("Hello!", Logger::LogLevels::Information);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }, "FileSystemTest", 2048, nullptr, 1, nullptr);
}