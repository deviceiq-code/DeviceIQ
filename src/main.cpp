#include <Arduino.h>
#include "core/App.h"

app App;

void setup() {
    esp_log_level_set("*", ESP_LOG_NONE);
    
    Serial.begin(115200);
    Serial.print("Starting DeviceIQ...\r\n\r\n");

    App.Start();
}

void loop() { vTaskDelete(NULL); }