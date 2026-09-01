#pragma once

namespace cli {
    // Registers every administrative Telnet command. Must run before
    // TelnetServer.Start(). Returns false if any registration failed
    // (duplicate command name or command table full).
    [[nodiscard]] bool RegisterCommands();
}
