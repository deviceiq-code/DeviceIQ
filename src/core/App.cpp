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

    // Settings
    const bool configurationLoaded = Settings.Load();

    // Logger
    Logger.LogLevel(Settings.Log.LogLevel());
    Logger.Endpoint(Settings.Log.Endpoint());
    Logger.SyslogServerHost(Settings.Log.SyslogServerHost());
    Logger.SyslogServerPort(Settings.Log.SyslogServerPort());

    if (Logger.Start() == false) {
        Serial.println("Error initializing Logger object");
        return;
    }

    Logger.Log(Version::Info(), logger::LogLevels::Information);
    Logger.Log("Logger initialized", logger::LogLevels::Information);
    Logger.Log("FileSystem initialized", logger::LogLevels::Information);

    if (configurationLoaded) {
        Logger.Log(String("Configuration initialized - file " + String(Defaults.ConfigFileName) + " read"), logger::LogLevels::Information);
    } else {
        Logger.Log(String("Configuration initialized with defaults - file " + String(Defaults.ConfigFileName) + " not loaded"), logger::LogLevels::Warning);
    }

    xTaskCreate([](void* parameter) {
        while (true) {
            Logger.Log("Hello!", logger::LogLevels::Information);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }, "FileSystemTest", 2048, nullptr, 1, nullptr);
}
