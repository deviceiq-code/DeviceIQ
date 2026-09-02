#pragma once

// Persists component state, logs, and restarts the device. Shared by the
// Telnet "reboot" command and the web "Restart" action.
void DeviceRestart();
