#include <Arduino.h>
#include "core/App.h"

App app;

void setup() { app.begin(); }
void loop() { vTaskDelete(NULL); }