#include "Globals.h"

rtc Clock(0);
logger Logger(Serial);
filesystem FileSystem;
settings Settings;
network Network;
telnetserver TelnetServer;
ComponentManager ComponentController;
relay OnboardLedRelay("OnboardLed", 1, component::Buses::Onboard, 2);
