#pragma once

#include "Defaults.h"
#include "RTC.h"
#include "Logger.h"
#include "FileSystem.h"
#include "Settings.h"
#include "Network.h"
#include "TelnetServer.h"
#include "Version.h"
#include "Automation.h"
#include "EspNowBus.h"
#include "MQTTClient.h"
#include "components/ComponentManager.h"
#include "components/Relay.h"
#include "web/HTTPServer.h"
#include "web/WebhookServer.h"

extern rtc Clock;
extern logger Logger;
extern filesystem FileSystem;
extern settings Settings;
extern network NetworkController;
extern telnetserver TelnetServer;
extern ComponentManager ComponentController;
extern automation Automation;
extern mqttclient MQTTClient;
extern espnowbus EspNow;
extern relay OnboardLedRelay;
extern httpserver HTTPServer;
extern webhookserver WebhookServer;
