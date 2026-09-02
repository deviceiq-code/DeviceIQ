#include "DeviceControl.h"

#include "Globals.h"

void DeviceRestart() {
    if (!Settings.SaveComponentsState()) {
        Logger.Log("Error saving component state before restart", logger::LogLevels::Error);
    }

    Logger.Log("Device restart requested", logger::LogLevels::Information);
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP.restart();
}
