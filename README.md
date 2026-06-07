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

Example:

```json
{
  "deviceName": "LivingRoomController",
  "wifi": {
    "ssid": "MyWiFi",
    "password": "secret"
  },
  "components": [
    {
      "type": "relay",
      "name": "Relay1",
      "pin": 26
    }
  ]
}
```

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
