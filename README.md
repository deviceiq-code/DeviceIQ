# DeviceIQ

DeviceIQ is a modular ESP32-based automation and orchestration platform built with **PlatformIO**, **Arduino Framework**, and **FreeRTOS**.

The project is designed to provide a scalable foundation for:

- Home automation
- Industrial monitoring
- IoT orchestration
- Distributed device management
- Event-driven scripting
- Remote configuration and control

DeviceIQ focuses on reliability, modularity, low memory usage, and clean architecture for embedded systems.

---

# Features

## Core Features

- ESP32-based architecture
- PlatformIO development environment
- FreeRTOS task-based execution
- Modular component system
- JSON-based device configuration
- Event-driven automation engine
- Lightweight script interpreter
- UDP device discovery
- TCP communication
- Remote orchestration support
- Persistent configuration storage

---

# Architecture

DeviceIQ is organized into independent modules to simplify maintenance and future expansion.

```text
+-------------------+
|       App         |
+-------------------+
         |
         v
+-------------------+
|   Task Manager    |
+-------------------+
   |      |      |
   v      v      v
Network  Components  Automation
```

Each subsystem runs independently using FreeRTOS tasks.

---

# Project Structure

```text
DeviceIQ/
├── platformio.ini
├── README.md
├── include/
├── src/
│   ├── core/
│   ├── config/
│   ├── network/
│   ├── components/
│   ├── automation/
│   └── tasks/
├── data/
├── lib/
└── test/
```

---

# Modules

## Core

Responsible for application startup, logging, and system orchestration.

### Includes

- App lifecycle
- Logging
- Task management
- Global initialization

---

## Config

Handles persistent device configuration using JSON files.

### Responsibilities

- Load configuration
- Save configuration
- Validate settings
- Manage runtime configuration

---

## Network

Provides communication infrastructure.

### Features

- Wi-Fi connection management
- UDP discovery
- TCP communication
- Remote commands
- Device identification

---

## Components

Represents hardware abstractions.

### Examples

- Relay
- Button
- Thermometer (DHT11, DHT12, DHT21, DHT22, and DS18B20)
- Blinds
- PWM Output
- RGB Controller

Each component exposes:

- States
- Events
- Actions

---

## Automation

Processes events and executes automation scripts.

### Example

```text
Button1.Clicked -> Relay1.Toggle()
```

Future scripting support may include:

```text
if()
delay()
log()
open()
close()
toggle()
```

---

# FreeRTOS Design

DeviceIQ is fully task-oriented.

## Planned Tasks

| Task | Responsibility |
|---|---|
| NetworkTask | Wi-Fi, UDP, TCP |
| ComponentTask | Hardware polling |
| AutomationTask | Event processing |
| ConfigTask | Persistent storage |
| WatchdogTask | Health monitoring |

---

# Configuration

Configuration files are stored as JSON.

## Wi-Fi fallback and reconnection

Network connection attempts run independently from components and automations.
After the initial `Connection Timeout`, DeviceIQ enables its fallback access
point and continues reconnecting to the configured Wi-Fi in `WIFI_AP_STA`
mode. The fallback access point therefore remains available while station
reconnection attempts are in progress.

Reconnect intervals use exponential backoff between `Reconnect Initial
Interval` and `Reconnect Maximum Interval`. After the station reconnects, the
fallback access point remains active for `Fallback AP Retention` seconds and
is then disabled. A value of `0` disables it immediately after reconnection.

```json
"Network": {
  "Connection Timeout": 30,
  "Reconnect Enabled": true,
  "Reconnect Initial Interval": 5,
  "Reconnect Maximum Interval": 60,
  "Fallback AP Enabled": true,
  "Fallback AP SSID": "",
  "Fallback AP Password": "DeviceIQ-Setup",
  "Fallback AP Retention": 300
}
```

An empty fallback SSID uses the device hostname. Change the example fallback
password before deployment. Existing configurations using `Online Checking`
and `Online Checking Timeout` are still accepted and migrated when saved.

Example:

```json
{
  "ComponentSchemaVersion": 1,
  "Components": {
    "1": {
      "Setup": {
        "Name": "OnboardLed",
        "Class": "Relay",
        "Bus": "Onboard",
        "Address": 2,
        "Type": "NormallyOpen",
        "DriveMode": "ActiveHigh"
      },
      "Properties": {
        "Enabled": true,
        "State": false
      },
      "Events": {
        "Changed": "log(%NAME% changed)"
      }
    },
    "2": {
      "Setup": {
        "Name": "WallButton",
        "Class": "Button",
        "Bus": "Onboard",
        "Address": 4,
        "ActiveLevel": "Low",
        "InputMode": "PullUp",
        "DebounceTimeMs": 50,
        "LongClickTimeMs": 1000,
        "MultiClickTimeMs": 400
      },
      "Properties": {
        "Enabled": true
      },
      "Events": {
        "Pressed": "log(%NAME% pressed)",
        "Released": "log(%NAME% released)",
        "Clicked": "compset(GarageLights state=toggle)"
      }
    }
  }
}
```

Component schema 1 is mandatory. `Components` is an object whose decimal keys
are the stable component IDs. Every component has exactly three sections:
`Setup` for its name, construction, and hardware settings; `Properties` for
persistent runtime values; and `Events` for automation bindings. IDs are not
repeated inside `Setup`. Other component layouts are rejected and are not
migrated automatically.

At startup, configured components are validated and created from this object. If
the schema or component catalog is invalid, no components are installed.

Relay runtime state is persisted back to `/config.json` every `General.Save
State Pooling` seconds when it changes. Button input state is
physical and is not persisted. A power loss before the next persistence cycle
can therefore lose the most recent change.

### Thermometers

`Thermometer` supports DHT11, DHT12, DHT21, DHT22, and DS18B20 sensors. DHT11,
DHT21, DHT22, and DS18B20 use `Bus: "Onboard"`, where `Address` is the GPIO.
DHT12 supports either one-wire on `Onboard` or I2C; its default I2C address is
`92` (`0x5C`).

```json
{
  "8": {
    "Setup": {
      "Name": "RoomClimate",
      "Class": "Thermometer",
      "Bus": "Onboard",
      "Address": 8,
      "Type": "DHT22",
      "PollingIntervalMs": 5000
    },
    "Properties": {
      "Enabled": true
    },
    "Events": {
      "TemperatureChanged": "log(%NAME% temperature changed)",
      "HumidityChanged": "log(%NAME% humidity changed)",
      "ReadFailed": "log(%NAME% read failed)"
    }
  }
}
```

`TemperatureChanged` and `HumidityChanged` carry the reading multiplied by 100
in the integer event value. `Changed` is emitted after either measurement
changes. DS18B20 conversion is asynchronous and does not block the component
task. MQTT Discovery creates a temperature sensor and, for DHT models, a
humidity sensor in Home Assistant.

### Blinds groups

A `Blinds` component owns two relays and, optionally, two buttons. Owned
components remain in the catalog as hardware definitions, but become private:
they are not published through MQTT, cannot be targeted by automation, and
their configured `Events` are ignored with a startup warning. All motor control
passes through the group, which guarantees relay interlocking and a delay when
reversing direction.

```json
{
  "7": {
    "Setup": {
      "Name": "BedroomBlinds",
      "Class": "Blinds",
      "Bus": "Group",
      "RelayUp": 3,
      "RelayDown": 4,
      "ButtonUp": 5,
      "ButtonDown": 6,
      "OpenStepTimeMs": 280,
      "CloseStepTimeMs": 240,
      "OpenCorrectionFactor": 0.35,
      "CloseCorrectionFactor": 0.20,
      "EndstopMarginMs": 2000,
      "ReversalDelayMs": 250
    },
    "Properties": {
      "Enabled": true,
      "Position": 0
    },
    "Events": {}
  }
}
```

`OpenStepTimeMs` and `CloseStepTimeMs` are the estimated travel times for one
percent in each direction and are required. Omitting either makes the `Blinds`
definition invalid. The invalid group and its dedicated relays/buttons are not
installed, while unrelated components continue to start. In the example, a
complete opening is estimated at 28 seconds and a complete closing at 24
seconds.

`OpenCorrectionFactor` and `CloseCorrectionFactor` redistribute each
direction's total travel time using a monotonic nonlinear curve. They are
optional and default to `0.0`, which gives linear movement. Valid values range
from `0.0` through `0.95`. Opening correction makes the beginning slower and
the end faster; closing correction applies the mirrored behavior. The factors
change only the shape of the estimate, not its total travel time.

`EndstopMarginMs` is optional and defaults to `0`. For targets `0` and `100`,
the corresponding relay remains energized for this additional period after the
estimated endpoint. The position is confirmed as fully closed or fully open
only after the margin expires. This assumes that the motor's internal limit
switch safely interrupts it at the physical endpoint. Intermediate positions
never use the margin.

Holding a direction button moves while pressed and stops on release. A double
click starts complete travel. `Clicked`, `LongClicked`, and `TripleClicked` are
private no-ops.

The public properties are `state` (`open`, `close`, or `stop`) and `position`
(`0` through `100`). Home Assistant receives one MQTT cover for the group; its
member relays and buttons are intentionally hidden.

Component events are handled by the automation task. Event names must exist in
the component's event descriptors. The first supported actions are:

```text
log(message)
compset(component_name property=value)
compset(#component_id property=value)
```

`%NAME%` is replaced with the source component name. Relay state accepts
`on`, `off`, and `toggle` in `compset`. Each event currently accepts one action.
Malformed actions, unknown events, missing targets, and rejected properties are
reported in the log without stopping component processing.

### Component CLI

```text
comp list
comp tree
comp status [component_name|#component_id]
comp set [component_name|#component_id] state=on
comp set [component_name|#component_id] enabled=false
comp trigger [component_name|#component_id] event [value=integer]
comp rename [component_name|#component_id] name=newname
comp remove [component_name|#component_id]
comp add relay name=Lamp address=5
comp add button name=WallButton address=6 inputMode=PullUp
comp add thermometer name=RoomClimate address=8 type=DHT22 pollingIntervalMs=5000
comp add thermometer name=I2CClimate bus=I2C address=92 type=DHT12
comp add blinds name=BedroomBlinds relayUp=3 relayDown=4 buttonUp=5 buttonDown=6 openStepTimeMs=280 closeStepTimeMs=240 openCorrectionFactor=0.35 closeCorrectionFactor=0.20 endstopMarginMs=2000
```

`comp tree` groups the catalog by `Onboard`, `I2C`, and `Group` buses. Blinds
entries also show their relay and button ID references as child branches.
Runtime differences are marked as `restart required`, while runtime components
removed from the configuration are marked as `pending removal`.

`state` is applied immediately in runtime and persisted automatically. Other
properties, add, rename, and remove update `/config.json` immediately and report
that a restart is required. Use the existing `reboot` command to load those
changes. Mutating subcommands require an administrative session.

`trigger` simulates any event declared by the selected runtime component. The
optional integer value defaults to `0`. Synthetic events use the normal event
pipeline, so public components reach automations and MQTT, while private Blinds
members are routed only to their owner. Examples:

```text
comp trigger WallButton Clicked
comp trigger WallButton DoubleClicked value=2
comp trigger #4 Pressed value=1
comp trigger #4 Released
```

### Network CLI

```text
net
net show
net ssid [value|clear]
net rssi
net ip [address]
net hostname [value]
net dhcp [on|off]
net gateway [address]
net netmask [address]
net dns [1|2] [address]
net passphrase [value|clear]
net connection-timeout [seconds]
net reconnect [on|off]
net reconnect-initial [seconds]
net reconnect-maximum [seconds]
net fallback [on|off]
net fallback-ssid [value|clear]
net fallback-password [value|clear]
net fallback-retention [seconds]
```

`net` and `net show` display both the current connection state and every field
from the `Network` section of `/config.json`. Omitting a value shows that
setting; supplying a value updates `/config.json` immediately and requires an
administrative session. Configuration changes take effect after `reboot`.
Passphrases are never printed. Use `clear` to configure an empty SSID or
password.

### System diagnostics CLI

```text
hwinfo
mem
mem b
mem kb
mem mb
fs
ping 192.168.1.1
ping example.com -n 10
log view
log view 50
log view all
log clear
```

`hwinfo` is public and reports the ESP32 model, revision, cores, clocks,
internal chip temperature, MAC addresses, flash, firmware, memory, SDK, reset
reason, uptime, and FreeRTOS task count. `mem` and `fs` require an
administrative session. `mem` shows bytes by default; `b`, `kb`, and `mb`
select bytes, KiB, and MiB. It reports total, used, available, minimum-free, and
largest-block values for internal heap and PSRAM. `fs` shows LittleFS capacity,
usage, entry counts, and known DeviceIQ file sizes. `ping` is a public built-in
Telnet command, accepts an IPv4 address or DNS name, sends four ICMP requests by
default, and accepts `-n` values from 1 through 20.

`log` requires an administrative session. `log view` shows the last 10 lines,
an explicit numeric argument selects another number of trailing lines, and
`all` prints the complete file. `log clear` removes the current log file; the
logger creates it again when the next file log entry is written.

---

# Goals

The primary goals of DeviceIQ are:

- Clean architecture
- High reliability
- Low memory footprint
- Extensibility
- Remote management
- Event-driven automation
- Hardware abstraction
- Scalability across multiple devices

---

# Development Environment

## Requirements

- Visual Studio Code
- PlatformIO Extension
- ESP32 Board Support

---

# Dependencies

Current planned dependencies:

- ArduinoJson
- ESPAsyncWebServer (optional)
- AsyncTCP (optional)

---

# Building

## Build Project

```bash
pio run
```

## Upload Firmware

```bash
pio run --target upload
```

## Serial Monitor

```bash
pio device monitor
```

---

# Future Plans

## Planned Features

- OTA firmware updates
- MQTT support
- Web configuration portal
- Rule editor
- Secure communication
- Multi-device orchestration
- BLE support
- Modbus integration
- Home Assistant integration
- Device clustering

---

# License

This project is currently private and under active development.

---

# Author

Fernando Almeida 
DeviceIQ Project
