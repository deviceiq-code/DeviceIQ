# DeviceIQ

DeviceIQ is home/building automation firmware for the ESP32-S3, built with **PlatformIO**, the **Arduino framework**, and **FreeRTOS**. Hardware I/O (relays, buttons, blinds, thermometers) is described entirely in a JSON configuration file — no recompilation needed to add, remove, or rewire a device.

---

## Features

- Component model for Relay, Button, Blinds (a group of 2 relays + up to 2 buttons), and Thermometer (DHT11/12/21/22, DS18B20), plus PCF8574 I²C expanders for onboard I/O beyond the native GPIO count
- Declarative automation: bind component events to `log(...)` or `compset(selector property=value)` actions directly in the configuration file
- MQTT publishing with Home Assistant MQTT Discovery (switch, binary_sensor, sensor, cover, event)
- Administrative CLI over Telnet: authenticated sessions (PBKDF2-SHA256, per-IP rate limiting), configurable idle timeout and session limit, admin/guest permission levels
- WiFi station mode with fallback SoftAP and exponential-backoff reconnect
- NTP time sync
- Configuration and component runtime state persisted separately on LittleFS, both written atomically (temp file + rename) so a power loss mid-write can't corrupt either file

---

## Project structure

```text
DeviceIQ/
├── platformio.ini
├── data/
│   └── config.json        # provisioned configuration, uploaded to LittleFS
└── src/
    ├── main.cpp
    ├── core/               # app lifecycle, settings, network, telnet server,
    │                       # logger, filesystem, MQTT client, automation engine
    ├── components/         # component base class, manager, and concrete
    │                       # component types (Relay, Button, Blinds, Thermometer)
    └── cli/                # Telnet command registration, grouped by domain
                             # (network, telnet, log, users, components, system)
```

---

## Configuration

On boot, DeviceIQ reads `/config.json` from LittleFS. It holds:

- Logging, network, MQTT, Telnet, and user account settings
- The component catalog: for each component, a `Setup` section (class, bus, address, wiring), a `Properties` section (seed values used only until runtime state exists), and an `Events` section (automation bindings)

Component runtime state that changes frequently (a relay's on/off state, a blind's position) is persisted separately to `/state.json`, written every few seconds while something has changed. This keeps the credential-bearing `config.json` untouched by routine state changes and out of the write path most of the time. On boot, `state.json` — when present — overrides the seed `Properties` values from `config.json` per component ID; if it's missing (first boot, or after being lost), the seed values apply.

Example component entry:

```json
"1": {
    "Setup": {
        "Name": "GarageLights",
        "Class": "Relay",
        "Bus": "Onboard",
        "Address": 2,
        "Type": "NormallyOpen",
        "DriveMode": "ActiveHigh"
    },
    "Properties": {},
    "Events": {
        "Changed": "log(%NAME% changed)"
    }
}
```

---

## Telnet CLI

Connect to the configured Telnet port (default `23`) and run `help` for the full command list. Some commands require an administrative session (`logon <username> <password>`); the built-in `help <command>` shows detailed usage.

| Command | Purpose |
|---|---|
| `net` | Show/configure WiFi, IP, and reconnect settings |
| `telnet` | Show/configure the Telnet server itself (port, idle timeout, session limit) |
| `log` | View or clear the device log; configure log endpoint, level, and syslog target |
| `comp` | List, inspect, and manage components (`set`, `trigger`, `add`, `remove`, `rename`) |
| `logon` | Authenticate the current session as a specific user |
| `ping` | ICMP ping from the device |
| `hwinfo`, `mem`, `fs`, `ver` | Hardware, memory, filesystem, and version information |
| `dumpcfg` | Print the raw configuration file |
| `reboot` | Restart the device |

---

## Building

Requires [PlatformIO](https://platformio.org/).

```sh
pio run                        # build
pio run --target upload        # flash the firmware
pio run --target uploadfs      # upload data/ (config.json) to LittleFS
pio device monitor             # serial monitor
```

Target board: ESP32-S3 N16R8 (16 MB flash, octal PSRAM). See `platformio.ini` for the exact board definition and library dependencies.
