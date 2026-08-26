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
- Sensor
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
  "Components": [
    {
      "Name": "OnboardLed",
      "ID": 1,
      "Class": "Relay",
      "Bus": "Onboard",
      "Address": 2,
      "Type": "NormallyOpen",
      "DriveMode": "ActiveHigh",
      "Properties": {
        "Enabled": true,
        "State": false
      },
      "Events": {
        "Changed": "log(%NAME% changed)"
      }
    },
    {
      "Name": "WallButton",
      "ID": 2,
      "Class": "Button",
      "Bus": "Onboard",
      "Address": 4,
      "ActiveLevel": "Low",
      "InputMode": "PullUp",
      "DebounceTimeMs": 50,
      "LongClickTimeMs": 1000,
      "MultiClickTimeMs": 400,
      "Properties": {
        "Enabled": true
      },
      "Events": {
        "Pressed": "log(%NAME% pressed)",
        "Released": "log(%NAME% released)",
        "Clicked": "compset(GarageLights state=toggle)"
      }
    }
  ]
}
```

At startup, onboard components are validated and created from this array. If the
section is missing or invalid, DeviceIQ falls back to its built-in components.

Relay runtime state is persisted back to `/config.json` every `General.Save
State Pooling` seconds when it changes. Button input state is
physical and is not persisted. A power loss before the next persistence cycle
can therefore lose the most recent change.

### Blinds groups

A `Blinds` component owns two relays and, optionally, two buttons. Owned
components remain in the catalog as hardware definitions, but become private:
they are not published through MQTT, cannot be targeted by automation, and
their configured `Events` are ignored with a startup warning. All motor control
passes through the group, which guarantees relay interlocking and a delay when
reversing direction.

```json
{
  "Name": "BedroomBlinds",
  "ID": 7,
  "Class": "Blinds",
  "Bus": "Group",
  "Relay Up": "BedroomBlindRelayUp",
  "Relay Down": "BedroomBlindRelayDown",
  "Button Up": "BedroomBlindButtonUp",
  "Button Down": "BedroomBlindButtonDown",
  "StepTimeMs": 250,
  "ReversalDelayMs": 250,
  "Properties": {
    "Enabled": true,
    "Position": 0
  },
  "Events": {}
}
```

`StepTimeMs` is the estimated travel time for one percent, so `250` represents
approximately 25 seconds from fully closed to fully open. Holding a direction
button moves while pressed and stops on release. A double click starts complete
travel. `Clicked`, `LongClicked`, and `TripleClicked` are private no-ops.

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
comp status [component_name|#component_id]
comp set [component_name|#component_id] state=on
comp set [component_name|#component_id] enabled=false
comp trigger [component_name|#component_id] event [value=integer]
comp rename [component_name|#component_id] name=newname
comp remove [component_name|#component_id]
comp add relay name=Lamp address=5
comp add button name=WallButton address=6 inputMode=PullUp
comp add blinds name=BedroomBlinds relayUp=UpRelay relayDown=DownRelay buttonUp=UpButton buttonDown=DownButton
```

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

### System diagnostics CLI

```text
hwinfo
mem
mem b
mem kb
mem mb
fs
```

`hwinfo` is public and reports the ESP32 model, revision, cores, clocks,
internal chip temperature, MAC addresses, flash, firmware, memory, SDK, reset
reason, uptime, and FreeRTOS task count. `mem` and `fs` require an
administrative session. `mem` shows bytes by default; `b`, `kb`, and `mb`
select bytes, KiB, and MiB. It reports total, used, available, minimum-free, and
largest-block values for internal heap and PSRAM. `fs` shows LittleFS capacity,
usage, entry counts, and known DeviceIQ file sizes.

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
