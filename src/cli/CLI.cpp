#include "CLI.h"

#include "ComponentCommands.h"
#include "NetworkCommands.h"
#include "SystemCommands.h"
#include "TelnetCommands.h"
#include "UserCommands.h"

bool cli::RegisterCommands() {
    // Every group attempts registration regardless of earlier failures, so a
    // single broken group does not hide failures in the others.
    const bool system = RegisterSystemCommands();
    const bool user = RegisterUserCommands();
    const bool component = RegisterComponentCommands();
    const bool network = RegisterNetworkCommands();
    const bool telnet = RegisterTelnetCommands();

    return system && user && component && network && telnet;
}
