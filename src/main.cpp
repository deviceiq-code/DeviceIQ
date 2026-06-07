#include <Arduino.h>
#include "core/App.h"

App app;

void setup() { app.Start(); }
void loop() { vTaskDelete(NULL); }