#include <Arduino.h>

#include "App.h"
#include "Globals.h"

void App::Start() {
    // Clock
    Clock.SetEpoch(Defaults.InitialTimeAndDate);

    // FileSystem
    if (FileSystem.Start(true) == false) {
        Serial.println("Error initializing FileSystem object");
        return;
    }

    // Logger
    if (Logger.Start() == false) {
        Serial.println("Error initializing Logger object");
        return;
    }

    Logger.LogLevel(Defaults.Log.Level);
    Logger.Endpoint(logger::Endpoints::Serial | logger::Endpoints::File | logger::Endpoints::Syslog);
    Logger.SyslogServerHost(Defaults.Log.SyslogServer);
    Logger.SyslogServerPort(Defaults.Log.SyslogPort);

    Logger.Log(Version::Info(), logger::LogLevels::Information);
    Logger.Log("Logger initialized", logger::LogLevels::Information);
    Logger.Log("FileSystem initialized", logger::LogLevels::Information);
    Logger.Log(String("Configuration initialized - file " + String(Defaults.ConfigFileName) + " read"), logger::LogLevels::Information);
    
    xTaskCreate([](void* parameter) {
        while (true) {
            Logger.Log("Hello!", logger::LogLevels::Information);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }, "FileSystemTest", 2048, nullptr, 1, nullptr);
}