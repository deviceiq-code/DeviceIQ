#pragma once

#include "Defaults.h"
#include "RTC.h"
#include "Logger.h"
#include "FileSystem.h"
#include "Settings.h"
#include "Network.h"
#include "TelnetServer.h"
#include "Version.h"
#include "components/ComponentManager.h"
#include "components/Relay.h"

extern rtc Clock;
extern logger Logger;
extern filesystem FileSystem;
extern settings Settings;
extern network Network;
extern telnetserver TelnetServer;
extern ComponentManager ComponentController;
extern relay OnboardLedRelay;
