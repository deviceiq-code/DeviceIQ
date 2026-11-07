# DeviceIQ

DeviceIQ is home/building automation firmware for the ESP32-S3 and ESP32-WROOM-32, built with **PlatformIO**, the **Arduino framework** (Arduino-ESP32 3.3.x / ESP-IDF 5.5, via [pioarduino](https://github.com/pioarduino/platform-espressif32)), and **FreeRTOS**. Hardware I/O (relays, buttons, doorbells, blinds, thermometers, RGB LEDs) is described entirely in a JSON configuration file — no recompilation needed to add, remove, or rewire a device. It can be managed from a built-in Web UI, a Telnet CLI, MQTT/Home Assistant, or HTTP webhooks.

---

## Features

- **Components:** Relay (latching or momentary/pulse), Button (click, long click, double/triple click), Doorbell, Blinds (a group of 2 relays + up to 2 buttons, with time-based position tracking), Thermometer (DHT11/12/21/22, DS18B20), and RGB_LED (3 PWM pins, color + on/off)
- **ESP-NOW remote nodes:** Relay, Button, and Doorbell components can live on remote ESP-01 boards running the generic [DeviceIQ-Node](#esp-now-nodes) firmware, over an authenticated protocol (per-session HMAC, replay protection), with adoption, identify, replace, rename, and removal of nodes
- **Declarative automation:** bind component events to `log(...)` or `compset(selector property=value)` actions directly in the configuration file
- **Web UI:** dashboard, component editor, settings, user management, log viewer, and over-the-air updates (see [Web UI](#web-ui))
- **MQTT** publishing with Home Assistant MQTT Discovery (switch, binary_sensor, sensor, cover, light, event — including a `doorbell` event entity that HA's HomeKit Bridge exposes as a native Doorbell accessory); removed components are unpublished from HA
- **Webhooks:** token-protected HTTP endpoints on a separate port to read or set any component from other systems
- **Telnet CLI:** authenticated sessions (PBKDF2-SHA256, per-IP rate limiting), configurable idle timeout and session limit, admin/guest permission levels
- **Logging** to Serial, Syslog, and/or a file on LittleFS, with per-level filtering
- **Networking:** WiFi station mode (DHCP or static IP) with a fallback SoftAP for recovery and exponential-backoff reconnect
- **Clock:** NTP sync with time zone, or manual date/time when NTP is off; 64-bit `time_t` (no 2038 rollover)
- **Persistence:** configuration and component runtime state are stored separately on LittleFS, both written atomically (temp file + rename) so a power loss mid-write can't corrupt either file

---

## Project structure

```text
DeviceIQ/
├── platformio.ini
├── data/                   # Web UI assets, uploaded to LittleFS - no config.json
│                           # here on purpose, see "Configuration" below
├── docs/
│   └── CONFIGURATION.md    # full config.json reference (format and defaults)
├── releases/               # release notes
├── scripts/                # OTA package builder + PlatformIO post-build hook
└── src/
    ├── main.cpp
    ├── core/               # app lifecycle, settings, network, clock, telnet
    │                       # server, logger, filesystem, MQTT client,
    │                       # automation engine, ESP-NOW bus and protocol
    ├── components/         # component base class, manager, and concrete types
    │                       # (Relay, Button, Doorbell, Blinds, Thermometer, RGB_LED)
    ├── web/                # Web UI/REST API server and webhooks server
    └── cli/                # Telnet commands, grouped by domain
```

---

## Configuration

On boot, DeviceIQ reads `/config.json` from LittleFS. It holds:

- Log, network, clock/NTP, Web Server, Webhooks, MQTT, Telnet, ESP-NOW, and user account settings
- The ESP-NOW node table (adopted nodes and their keys)
- The component catalog: for each component, a `Setup` section (class, bus, address, wiring), a `Properties` section (seed values used only until runtime state exists), and an `Events` section (automation bindings)

There's no `config.json` committed to `data/`, and that's deliberate: it would get flashed as-is to every device's LittleFS on `uploadfs`, baking in whatever Wi-Fi/MQTT credentials and component catalog it last held. Instead, a device with no `config.json` (first boot, or after `Restore Factory Defaults`) generates one on its own from the built-in defaults, including a fresh `admin`/`user` account pair (`admin`/`admin1234` and `user`/`user1234` — change them right away). See [`docs/CONFIGURATION.md`](docs/CONFIGURATION.md) for the full format and every default value.

Component runtime state that changes frequently (a relay's on/off state, a blind's position, an RGB LED's color) is persisted separately to `/state.json`, written every few seconds while something has changed. This keeps the credential-bearing `config.json` untouched by routine state changes and out of the write path most of the time. On boot, `state.json` — when present — overrides the seed `Properties` values from `config.json` per component ID; if it's missing (first boot, or after being lost), the seed values apply.

The configuration can also be exported, imported, and reset to factory defaults from the Web UI (Setup → Configuration).

Example component entries:

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
},
"2": {
    "Setup": {
        "Name": "FrontDoor",
        "Class": "Doorbell",
        "Bus": "EspNow",
        "Node": "porch",
        "Address": 2
    },
    "Properties": {},
    "Events": {
        "Pressed": "compset(GarageLights state=on)"
    }
}
```

---

## Web UI

Served on the configured Web Server port (default `80`), with login sessions, an idle timeout, and admin/non-admin permissions. Every page shows the device hostname, so it's easy to tell devices apart.

| Page | Purpose |
|---|---|
| Dashboard | Live state and controls for every component, as a flat grid or grouped by class, with a configurable refresh interval; ESP-NOW components show their node's online/offline state |
| Component | Add, edit, and remove components, including bus/node selection and event bindings |
| Setup | Settings tabs: Network, Web UI and CLI, Clock and NTP, Integrations (MQTT, Webhooks, ESP-NOW node manager), Log, and Configuration (export/import, factory reset, restart) |
| Users | Add, remove, and rename accounts, set passwords and admin rights |
| Log | View, export, and clear the device log, with auto-refresh |
| Update | Upload a combined firmware + filesystem OTA package (ESP32-S3 with PSRAM only) |
| About | Version, hardware, memory, and filesystem information |

The same server exposes the JSON API the pages use (`/api/components`, `/api/nodes`, `/api/settings`, `/api/users`, `/api/log`, ...).

---

## Webhooks

When enabled, a second HTTP server (default port `81`) accepts requests authenticated by a token (15–30 letters/numbers) passed as the `token` parameter:

```text
GET|POST /component/get?token=<token>[&id=<id>]
GET|POST /component/set?token=<token>&id=<id>&property=<property>&value=<value>
```

Configure it from Setup → Integrations or with the `webhooks` CLI command.

---

## ESP-NOW nodes

Relay, Button, and Doorbell components can be wired to a remote node instead of the controller's own GPIO: set `"Bus": "EspNow"` and `"Node": "<node name>"`, with `Address` being the GPIO on that node. Nodes are ESP-01 boards running the generic **DeviceIQ-Node** firmware (a separate project); the controller pushes each node its pin configuration, so nodes need no per-device setup.

To add a node, open discovery, power the node on, and adopt it by MAC address under a name — from Setup → Integrations or with `nodes discover on` / `nodes adopt <mac> <name>`. A failed board can be swapped with `replace`, which gives the new board the old node's name and components. Up to 12 nodes are supported.

Frames are authenticated with a truncated HMAC-SHA256 tag keyed per node and bound to fresh per-session nonces plus a strictly increasing sequence number, so captured frames can't be replayed. The wire format in [`EspNowProtocol.h`](src/core/EspNowProtocol.h) must stay identical to DeviceIQ-Node's copy.

---

## Telnet CLI

Connect to the configured Telnet port (default `23`) and run `help` for the full command list. Changes require an administrative session (`logon <username> <password>`); `help <command>` shows detailed usage.

| Command | Purpose |
|---|---|
| `net` | Show/configure WiFi, IP, fallback AP, and reconnect settings |
| `telnet` | Show/configure the Telnet server itself (port, idle timeout, session limit) |
| `web` | Show/configure the Web Server (enabled, port, idle timeout, session limit) |
| `webhooks` | Show/configure the webhooks server (enabled, token, port) |
| `user` | Manage user accounts (`add`, `remove`, `rename`, `set-admin`, `set-password`) |
| `mqtt` | Show/configure MQTT and Home Assistant discovery |
| `nodes` | List and manage ESP-NOW nodes (`discover`, `adopt`, `replace`, `identify`, `rename`, `remove`) |
| `comp` | List, inspect, and manage components (`list`, `tree`, `status`, `set`, `trigger`, `rename`, `event`, `add`, `remove`) |
| `log` | View or clear the device log; configure log endpoint, level, and syslog target |
| `ntp` | Show/configure NTP sync and time zone |
| `datetime` | Show the clock, or set it manually when NTP is disabled |
| `logon` | Authenticate the current session as a specific user |
| `ping` | ICMP ping from the device |
| `hwinfo`, `mem`, `fs`, `ver` | Hardware, memory, filesystem, and version information |
| `dumpcfg` | Print the raw configuration file |
| `reboot` | Restart the device |

---

## Building

Requires [PlatformIO](https://platformio.org/). The platform is pinned to pioarduino `55.03.311` (Arduino-ESP32 3.3.11, ESP-IDF 5.5.5) in `platformio.ini`. Two environments are defined - pick one with `-e` (omitting it builds both):

```sh
pio run -e esp32dev                        # build, ESP32-S3 N16R8
pio run -e esp32dev --target upload        # flash the firmware
pio run -e esp32dev --target uploadfs      # upload data/ (Web UI assets) to LittleFS
pio device monitor -e esp32dev             # serial monitor

pio run -e esp32-wroom32                   # same, for a plain ESP32-WROOM-32
```

- **`esp32dev`** - ESP32-S3 N16R8 (16 MB flash, octal PSRAM). Combined-package OTA upload via the Web UI needs the PSRAM this board has.
- **`esp32-wroom32`** - plain ESP32-WROOM-32 (4 MB flash, no PSRAM). Same firmware, no code differences - only `platformio.ini` changes (board id, partition table). Reflash via cable only; the Web UI's Update page is hidden on this board since its upload buffer needs PSRAM this board doesn't have.

Changes under `data/` (Web UI) only reach the device with `uploadfs` or an OTA package — a firmware-only upload keeps the old pages.

### OTA package

The Web UI's Update page takes a single file containing both the firmware and the LittleFS image, with a versioned header and CRC32 checks. It's built automatically by a post-build hook once both images exist:

```sh
pio run -e esp32dev                        # firmware.bin
pio run -e esp32dev --target buildfs       # littlefs.bin -> OTA package is generated
```

To build it by hand, run `python3 scripts/package_ota.py`. The version comes from the `DEVICEIQ_VERSION_*` flags in `platformio.ini`.

See `platformio.ini` for the exact board definitions and library dependencies, and [`releases/`](releases/) for release notes.
