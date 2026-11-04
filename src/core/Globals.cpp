#include "Globals.h"

rtc Clock(0);
logger Logger(Serial);
filesystem FileSystem;
settings Settings;
network NetworkController;
telnetserver TelnetServer;
ComponentManager ComponentController;
automation Automation;
mqttclient MQTTClient;
espnowbus EspNow;
httpserver HTTPServer;
webhookserver WebhookServer;
