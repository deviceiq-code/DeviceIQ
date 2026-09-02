#include "CLI.h"

#include "ComponentCommands.h"
#include "DateTimeCommands.h"
#include "LogCommands.h"
#include "MQTTCommands.h"
#include "NTPCommands.h"
#include "NetworkCommands.h"
#include "SystemCommands.h"
#include "TelnetCommands.h"
#include "UserCommands.h"
#include "WebCommands.h"
#include "WebhookCommands.h"

bool cli::RegisterCommands() {
    // Every group attempts registration regardless of earlier failures, so a
    // single broken group does not hide failures in the others.
    const bool system = RegisterSystemCommands();
    const bool user = RegisterUserCommands();
    const bool component = RegisterComponentCommands();
    const bool network = RegisterNetworkCommands();
    const bool telnet = RegisterTelnetCommands();
    const bool log = RegisterLogCommands();
    const bool ntp = RegisterNTPCommands();
    const bool datetime = RegisterDateTimeCommands();
    const bool mqtt = RegisterMQTTCommands();
    const bool web = RegisterWebCommands();
    const bool webhooks = RegisterWebhookCommands();

    return system && user && component && network && telnet && log && ntp && datetime && mqtt && web && webhooks;
}
