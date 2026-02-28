# CLAUDE.md — esphome-elero

This file provides guidance for AI assistants working in this repository. It describes the project structure, key conventions, and development workflows.

---

## Project Overview

`esphome-elero` is a custom **ESPHome external component** that enables Home Assistant to control Elero wireless motor blinds (rollers, shutters, awnings) via a **CC1101 868 MHz (or 433 MHz) RF transceiver** connected to an ESP32 over SPI.

The component is loaded directly from GitHub in an ESPHome YAML configuration:

```yaml
external_components:
  - source: github://pfriedrich84/esphome-elero
```

**Key capabilities:**
- Send open/close/stop/tilt commands to Elero blinds
- Receive status feedback (top, bottom, moving, blocked, overheated, etc.)
- Track cover position based on movement timing
- RSSI signal strength monitoring per blind
- RF discovery scan to find nearby blinds (web UI and log-based)
- Optional web UI served at `http://<device-ip>/elero` for discovery and YAML generation

**Upstream credits:**
- Encryption/decryption: [QuadCorei8085/elero_protocol](https://github.com/QuadCorei8085/elero_protocol) (MIT)
- Remote handling: [stanleypa/eleropy](https://github.com/stanleypa/eleropy) (GPLv3)
- Based on the no-longer-maintained [andyboeh/esphome-elero](https://github.com/pfriedrich84/esphome-elero)

**Available skills (USE THEM!):**

| Skill | When to use |
|-------|-------------|
| `/elero-protocol` | **Always** when modifying CC1101 TX/RX code, packet encoding/decoding, encryption |
| `/modern-cpp` | **Always** when writing or reviewing C++ code |
| `/esp32-development` | **Always** when writing C++ code (ISRs, memory, FreeRTOS, SPI) |

> **IMPORTANT:** Before writing C++ code, invoke `/modern-cpp` and `/esp32-development` skills.
> Before touching RF protocol code (elero.cpp TX/RX, packet handling), invoke `/elero-protocol`.

---

## Compatibility Matrix

**Supported targets — ESP32 only:**

| Framework | Status | Notes |
|-----------|--------|-------|
| **ESP-IDF** | ✅ Supported | Primary target, recommended |
| **Arduino** | ✅ Supported | Legacy support via ESPHome |

**NOT supported (do not add support for):**

| Target | Reason |
|--------|--------|
| ESP8266 | Insufficient RAM/flash, no SPI DMA |
| RP2040 | No CC1101 driver, different SPI API |
| LibreTiny | Out of scope |
| Host (native) | No hardware access |

The codebase uses Mongoose for HTTP/WebSocket specifically because it provides a unified API across ESP-IDF and Arduino frameworks. Do not introduce framework-specific code paths.

---

## Repository Structure

```
esphome-elero/
├── CLAUDE.md                          # This file
├── README.md                          # Main documentation (German + English)
├── example.yaml                       # Complete ESPHome config example
├── docs/
│   ├── INSTALLATION.md                # Step-by-step hardware and software setup
│   ├── CONFIGURATION.md               # Full parameter reference
│   └── examples/                      # Additional YAML examples
└── components/
    ├── elero/                         # Main hub component
    │   ├── __init__.py                # ESPHome component schema & code-gen (hub)
    │   ├── elero.h                    # C++ hub class header
    │   ├── elero.cpp                  # C++ RF protocol implementation (~625 lines)
    │   ├── cc1101.h                   # CC1101 register map & command strobes
    │   ├── cover/                     # Cover (blind) platform
    │   │   ├── __init__.py            # Cover schema & code-gen
    │   │   ├── EleroCover.h           # Cover class header
    │   │   └── EleroCover.cpp         # Cover logic, position tracking (~307 lines)
    │   ├── button/                    # Scan button platform
    │   │   ├── __init__.py
    │   │   ├── elero_button.h
    │   │   └── elero_button.cpp
    │   ├── sensor/                    # RSSI sensor platform
    │   │   └── __init__.py
    │   └── text_sensor/               # Blind status text sensor platform
    │       └── __init__.py
    └── elero_web/                     # Optional web UI component
        ├── __init__.py
        ├── elero_web_server.h
        ├── elero_web_server.cpp       # Mongoose WebSocket server (~300 lines)
        └── elero_web_ui.h             # Inline web UI HTML/JS
```

---

## Architecture

### Layered Architecture (CRITICAL)

The system is split into **four distinct layers** with strict responsibilities. Code MUST live in the correct layer.

```
┌─────────────────────────────────────────────────────────────────────────┐
│  CLIENT (Browser/Preact)                                                │
│  ─────────────────────────────────────────────────────────────────────  │
│  • Derives ALL state from: config + rf events + logs                    │
│  • Discovery = RF addresses NOT in config                               │
│  • Blind states = latest status from RF packets                         │
│  • Position = derived from movement timing                              │
│  • YAML generation = client-side from discovered addresses              │
│  • NO server round-trips for state queries                              │
└─────────────────────────────────────────────────────────────────────────┘
                              ▲
                              │ WebSocket (events down, commands up)
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  SERVER (EleroWebServer)                                                │
│  ─────────────────────────────────────────────────────────────────────  │
│  • STATELESS - no business logic, no state tracking                     │
│  • Dumb pipe: forwards RF packets → client, commands → hub              │
│  • On connect: sends config (blinds array from YAML)                    │
│  • On RF packet: broadcasts to all WebSocket clients                    │
│  • On log: forwards elero.* tagged logs to clients                      │
│  • Logger injected via callback (DI pattern)                            │
└─────────────────────────────────────────────────────────────────────────┘
                              ▲
                              │ Callbacks (on_rf_packet, logger)
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  CORE (Elero Hub)                                                       │
│  ─────────────────────────────────────────────────────────────────────  │
│  • CC1101 initialization and SPI communication                          │
│  • Non-blocking RX/TX via interrupt + loop() polling                    │
│  • Packet encoding/decoding and encryption                              │
│  • Notifies observers via callbacks (not direct coupling)               │
│  • ESPHome entity management (covers, lights, sensors)                  │
└─────────────────────────────────────────────────────────────────────────┘
                              ▲
                              │ SPI + GPIO interrupt
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  HARDWARE (CC1101)                                                      │
│  ─────────────────────────────────────────────────────────────────────  │
│  • 868 MHz RF transceiver                                               │
│  • GDO0 interrupt on packet received                                    │
│  • 64-byte TX/RX FIFO                                                   │
└─────────────────────────────────────────────────────────────────────────┘
```

### Why This Matters

**Server has NO business logic.** It doesn't:
- Track which blinds are discovered (client derives from RF packets)
- Store blind states (client derives from RF status events)
- Generate YAML (client does this)
- Know anything about "discovery mode" (client filters RF packets)

**Client derives EVERYTHING.** Given:
- `config` event on connect → list of configured blinds from YAML
- `rf` events → every RF packet with addresses, states, RSSI
- `log` events → ESPHome logs

The client can derive:
- **Configured blinds**: directly from config
- **Current states**: latest `rf` event per address with type=0xCA/0xC9 (status)
- **Discovery**: addresses in `rf` events that are NOT in config
- **RSSI**: from `rf` events
- **Logs**: from `log` events

### ESPHome Layer Design

1. **Python layer** (`__init__.py` files) — ESPHome code-generation time
   - Defines and validates YAML configuration schemas using `esphome.config_validation`
   - Generates C++ constructor calls via `esphome.codegen`
   - Declares ESPHome component dependencies (`DEPENDENCIES`, `AUTO_LOAD`)

2. **C++ layer** (`.h`/`.cpp` files) — compiled firmware running on ESP32
   - Implements the actual RF protocol, SPI communication, and entity logic
   - Runs inside the ESPHome `Component` lifecycle (`setup()`, `loop()`)

### Component Hierarchy

```
Elero (hub, SPIDevice + Component)
├── EleroBlindBase (abstract interface)
│   ├── EleroCover (cover::Cover + Component + EleroBlindBase)
│   └── EleroLight (light::LightOutput + Component + EleroBlindBase)
├── EleroScanButton (button::Button + Component)
├── sensor::Sensor (RSSI, registered per blind address)
├── text_sensor::TextSensor (status, registered per blind address)
├── EleroWebServer (Component, Mongoose HTTP/WS)
│   └── EleroWebSwitch (switch::Switch + Component)
└── Auto-registered sensors/text sensors per cover (optional)
```

The `EleroBlindBase` abstract class decouples the hub (`Elero`) from the cover implementation so `elero.h` never needs to `#include` the cover header. All communication between hub and covers goes through virtual methods.

### Core Data Flow

1. `Elero::setup()` configures CC1101 registers over SPI and attaches a GPIO interrupt on `gdo0_pin`.
2. When the CC1101 signals a received packet (GDO0 interrupt), `Elero::interrupt()` sets `received_ = true`.
3. `Elero::loop()` detects `received_`, reads the FIFO, decodes and decrypts the packet, then:
   - Dispatches state to matching `EleroBlindBase` via `set_rx_state()`
   - Calls `on_rf_packet_` callback → server broadcasts to WebSocket clients
4. `EleroCover::loop()` handles polling timers, position recomputation, and drains the command queue.

### Dependency Injection

The server receives events via callbacks, not direct coupling:

```cpp
// Hub calls this on every decoded RF packet
void Elero::set_rf_packet_callback(std::function<void(const RfPacketInfo&)> cb) {
  on_rf_packet_ = std::move(cb);
}

// LogListener interface for forwarding logs to WebSocket (ESPHome 2025.12.0+)
// Component inherits from logger::LogListener and implements on_log()
class EleroWebServer : public Component, public logger::LogListener {
  void setup() override {
    logger::global_logger->add_log_listener(this);
  }
  void on_log(uint8_t level, const char* tag, const char* msg, size_t msg_len) override {
    // Forward to WebSocket clients if tag starts with "elero"
  }
};
```

This keeps the hub independent of the web server implementation.

---

## Key Classes and Files

### `components/elero/elero.h` / `elero.cpp`

**Class:** `Elero : public spi::SPIDevice<...>, public Component`
**Namespace:** `esphome::elero`

Critical public API:
- `register_cover(EleroBlindBase*)` — called by each `EleroCover` at setup
- `send_command(t_elero_command*)` — encodes, encrypts, and transmits a command
- `start_scan()` / `stop_scan()` — toggle RF discovery mode
- `register_rssi_sensor(uint32_t addr, sensor::Sensor*)` — link RSSI sensor to a blind address
- `register_text_sensor(uint32_t addr, text_sensor::TextSensor*)` — link text sensor to a blind address
- `interrupt(Elero *arg)` — static ISR, sets `received_` flag

Key constants (defined in `elero.h`):

| Constant | Value | Purpose |
|---|---|---|
| `ELERO_MAX_PACKET_SIZE` | 57 | Maximum RF packet length (FCC spec) |
| `ELERO_POLL_INTERVAL_MOVING` | 2000 ms | Status poll while blind is moving |
| `ELERO_TIMEOUT_MOVEMENT` | 120 000 ms | Give up movement tracking after 2 min |
| `ELERO_SEND_RETRIES` | 3 | Command retry count |
| `ELERO_SEND_PACKETS` | 2 | Packets sent per command |
| `ELERO_DELAY_SEND_PACKETS` | 50 ms | Delay between packet repeats |
| `ELERO_MAX_DISCOVERED` | 20 | Max blinds tracked in scan mode |

State constants (`ELERO_STATE_*`): `UNKNOWN`, `TOP`, `BOTTOM`, `INTERMEDIATE`, `TILT`, `BLOCKING`, `OVERHEATED`, `TIMEOUT`, `START_MOVING_UP`, `START_MOVING_DOWN`, `MOVING_UP`, `MOVING_DOWN`, `STOPPED`, `TOP_TILT`, `BOTTOM_TILT`

### `components/elero/cover/EleroCover.h` / `EleroCover.cpp`

**Class:** `EleroCover : public cover::Cover, public Component, public EleroBlindBase`

Key behaviors:
- Maintains an internal `std::queue<uint8_t> commands_to_send_` for reliable delivery
- Polls blind status at a configurable interval (`poll_intvl_`, default 5 min); while moving, polls every `ELERO_POLL_INTERVAL_MOVING` (2 s)
- Tracks cover `position` (0.0–1.0) by dead-reckoning against `open_duration_` / `close_duration_` timestamps
- Supports tilt as a separate operation via `command_tilt_`
- Staggered poll offsets (`poll_offset_`) prevent all covers from polling simultaneously
- Auto-generates RSSI and status text sensors unless `auto_sensors: false` is set

### `components/elero/light/EleroLight.h` / `EleroLight.cpp`

**Class:** `EleroLight : public light::LightOutput, public Component, public EleroBlindBase`

Key behaviors:
- Implements on/off and brightness control for Elero wireless lights (dimmers)
- `dim_duration` parameter controls brightness range: `0s` = on/off only, `>0` = brightness control
- Shares the same RF protocol and command structure as covers
- Supports optional status checking via `command_check`

### `components/elero_web/elero_web_server.h` / `elero_web_server.cpp`

**Class:** `EleroWebServer : public Component`
**Optional sub-platform:** `EleroWebSwitch : public switch::Switch, public Component`

**CRITICAL: This class is a STATELESS pipe.** It does NOT:
- Track discovered blinds (client derives from RF packets)
- Store blind states (client derives from RF events)
- Generate YAML (client does this)
- Implement "scan mode" (client filters RF packets)

Key behaviors:
- Uses **Mongoose** HTTP/WebSocket library for cross-framework compatibility (Arduino + ESP-IDF)
- On connect: sends `config` event with blinds array from YAML
- On RF packet (via callback from hub): broadcasts `rf` event to all clients
- On log (via ESPHome logger callback): broadcasts `log` event to clients
- On `cmd` message: routes command to hub's `send_command()`
- On `raw` message: routes to hub's `send_raw_command()`

### Why Mongoose?

ESPHome's built-in `web_server_base` uses different implementations depending on the framework:
- **Arduino**: AsyncTCP + ESPAsyncWebServer
- **ESP-IDF**: esp_http_server

These have incompatible APIs for WebSocket handling. Mongoose provides a single, unified API that works identically on both frameworks.

### WebSocket Protocol

The web UI communicates exclusively via WebSocket at `/elero/ws`. See `docs/ARCHITECTURE.md` for the complete protocol specification.

**Server → Client Events:**

| Event | Description |
|-------|-------------|
| `config` | Sent on connect: device info, configured blinds/lights, frequency |
| `rf` | Every decoded RF packet: addresses, state, RSSI, raw bytes |
| `log` | ESPHome log entries with `elero.*` tags |

**Client → Server Messages:**

| Type | Description |
|------|-------------|
| `cmd` | Send command to blind/light: `{"type":"cmd", "address":"0xADDRESS", "action":"up"}` |
| `raw` | Send raw RF packet for testing: `{"type":"raw", "blind_address":"0x...", "channel":5, ...}` |

### HTTP Endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Redirect to `/elero` |
| `/elero` | GET | HTML web UI (static, bundled) |
| `/elero/ws` | WS | WebSocket endpoint for real-time communication |

---

## Naming Conventions

| Item | Convention | Example |
|---|---|---|
| C++ classes | PascalCase | `EleroCover`, `EleroWebServer` |
| C++ namespaces | lowercase | `esphome::elero` |
| C++ constants | `UPPER_SNAKE_CASE` with `ELERO_` prefix | `ELERO_COMMAND_COVER_UP` |
| C++ private members | trailing underscore | `gdo0_pin_`, `scan_mode_` |
| Python config keys | `snake_case` string constants | `"blind_address"`, `"gdo0_pin"` |
| YAML keys | `snake_case` | `blind_address`, `open_duration` |

---

## ESPHome Platform Conventions

When adding a new platform sub-component (e.g., a new sensor type):

1. Create `components/elero/<platform>/__init__.py` with:
   - `DEPENDENCIES = ["elero"]`
   - A `CONFIG_SCHEMA` using the appropriate platform schema builder
   - An `async def to_code(config)` that registers the component and connects it to the parent `Elero` hub
2. Create the corresponding `.h` and `.cpp` files in the same directory.
3. Add a `register_<platform>()` method to `Elero` in `elero.h` / `elero.cpp` if the hub needs to dispatch data to it.

The `CONF_ELERO_ID` pattern is used throughout to resolve the parent hub:
```python
cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
```
```python
parent = await cg.get_variable(config[CONF_ELERO_ID])
cg.add(var.set_elero_parent(parent))
```

---

## Configuration Reference (Summary)

### Hub (`elero:`)

```yaml
elero:
  cs_pin: GPIO5          # SPI chip select (required)
  gdo0_pin: GPIO26       # CC1101 GDO0 interrupt pin (required)
  freq0: 0x7a            # CC1101 FREQ0 register (optional, default 868.35 MHz)
  freq1: 0x71            # CC1101 FREQ1 register
  freq2: 0x21            # CC1101 FREQ2 register
```

Default frequency registers (`freq2=0x21, freq1=0x71, freq0=0x7a`) correspond to **868.35 MHz**. Use `freq0=0xc0` for 868.95 MHz variants.

SPI bus must be declared separately:
```yaml
spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19
```

### Cover (`cover: platform: elero`)

Required parameters:
- `blind_address` — 3-byte hex address of the motor (e.g., `0xa831e5`)
- `channel` — RF channel number of the blind
- `remote_address` — 3-byte hex address of the remote control paired with the blind

Optional parameters (with defaults):
- `poll_interval` (default `5min`, or `never`) — how often to query blind status
- `open_duration` / `close_duration` (default `0s`) — enables position tracking
- `supports_tilt` (default `false`)
- `auto_sensors` (default `true`) — auto-generate RSSI and status text sensors for this cover
- `payload_1` (default `0x00`), `payload_2` (default `0x04`)
- `pck_inf1` (default `0x6a`), `pck_inf2` (default `0x00`)
- `hop` (default `0x0a`)
- `command_up/down/stop/check/tilt` — override RF command bytes if non-standard

### Light (`light: platform: elero`)

Required parameters:
- `blind_address` — 3-byte hex address of the light receiver (e.g., `0xc41a2b`)
- `channel` — RF channel number of the light
- `remote_address` — 3-byte hex address of the remote control paired with the light

Optional parameters (with defaults):
- `dim_duration` (default `0s`) — time for dimming from 0% to 100%; `0s` = on/off only, `>0` = brightness control
- `payload_1` (default `0x00`), `payload_2` (default `0x04`)
- `pck_inf1` (default `0x6a`), `pck_inf2` (default `0x00`)
- `hop` (default `0x0a`)
- `command_on/off/dim_up/dim_down/stop/check` — override RF command bytes if non-standard

### Sensors

```yaml
sensor:
  - platform: elero
    blind_address: 0xa831e5   # Required: which blind
    name: "Blind RSSI"        # Unit: dBm

text_sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Blind Status"      # Values: see state constants above
```

### Buttons (RF scan)

```yaml
button:
  - platform: elero
    name: "Start Scan"
    scan_start: true
  - platform: elero
    name: "Stop Scan"
    scan_start: false
```

### Web UI (`elero_web`)

```yaml
# Use web_server_base (not web_server) to keep only the /elero UI
# web_server_base is auto-loaded by elero_web, but you can declare it
# explicitly to configure the port:
web_server_base:
  port: 80

elero_web:
  id: elero_web_ui   # Optional ID
```

Navigating to `http://<device-ip>/` will redirect to `/elero` automatically.

### Web UI Switch (`switch: platform: elero_web`)

Optional runtime control to enable/disable the web UI:

```yaml
switch:
  - platform: elero_web
    name: "Elero Web UI"
    restore_mode: RESTORE_DEFAULT_ON
```

When the switch is OFF, all `/elero` endpoints return HTTP 503 (Service Unavailable).

---

## Development Workflow

### Prerequisites

- ESPHome installed (`pip install esphome`)
- An ESP32 with a CC1101 module wired to SPI pins + GDO0 GPIO
- An existing Elero wireless blind system nearby for testing

### Local development

Since this is an external component consumed from GitHub, local iteration requires pointing ESPHome at a local path:

```yaml
external_components:
  - source:
      type: local
      path: /path/to/esphome-elero
```

### Building and flashing

```bash
# Validate config
esphome config my_device.yaml

# Compile only
esphome compile my_device.yaml

# Compile and flash via USB
esphome run my_device.yaml

# Stream logs over serial
esphome logs my_device.yaml

# Stream logs over Wi-Fi (OTA)
esphome logs --device <ip-address> my_device.yaml
```

### Finding blind addresses

The typical workflow for a new installation:

1. Add scan buttons and the web UI to `example.yaml`
2. Flash the device
3. Open `http://<device-ip>/elero` and press "Start Scan"
4. Operate each blind with its original remote
5. Discovered blinds appear in the web UI with addresses pre-filled
6. Download the generated YAML snippet and add it to your config

---

## Testing

There are no automated tests in this repository. Validation is done manually on real hardware:

1. Flash the firmware and verify the CC1101 initialises (check `esphome logs` for `[I][elero:...]` messages)
2. Use the RF scan to confirm blind discovery
3. Test each cover entity (open, close, stop) from Home Assistant
4. Verify RSSI and status text sensors update correctly

---

## Common Pitfalls

- **Wrong frequency**: Most European Elero motors use 868.35 MHz (`freq0=0x7a`). Some use 868.95 MHz (`freq0=0xc0`). If discovery finds nothing, try the alternate frequency.
- **SPI conflicts**: The CC1101 CS pin must not be shared with any other SPI device.
- **Using `web_server:` instead of `web_server_base:`**: Adding `web_server:` to your YAML re-enables the default ESPHome entity UI at `/`. Use `web_server_base:` (or rely on its auto-load via `elero_web`) to serve only the Elero UI at `/elero`. Navigating to `/` will redirect automatically to `/elero`.
- **Position tracking**: Leave `open_duration` and `close_duration` at `0s` if you only need open/close without position — setting incorrect durations causes wrong position estimates.
- **Poll interval `never`**: Set `poll_interval: never` for blinds that reliably push state updates (avoids unnecessary RF traffic). Internally this maps to `uint32_t` max (4 294 967 295 ms).

---

## Contributing

- Follow the existing naming conventions for C++ and Python code.
- Keep the `EleroBlindBase` interface minimal — the hub should not depend on cover internals.
- Test changes on real hardware before opening a pull request.
- Document new configuration parameters in both `README.md` and `docs/CONFIGURATION.md`.
- The primary development branch convention used by automation is `claude/<session-id>`.
