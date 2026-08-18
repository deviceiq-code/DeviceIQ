#include <Arduino.h>
#include "core/App.h"

App app;

void setup() { Serial.begin(115200); app.Start(); }    
void loop() { vTaskDelete(NULL); }