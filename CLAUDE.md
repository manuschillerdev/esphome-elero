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
    │   ├── cover_core.{h,cpp}          # Pure C++ cover logic (position tracking, state mapping, polling)
    │   ├── light_core.{h,cpp}          # Pure C++ light logic (brightness, dimming, state mapping)
    │   ├── device_manager.h            # IDeviceManager interface, HubMode, DeviceType enums
    │   ├── nvs_config.h               # NvsDeviceConfig (60-byte POD struct for NVS persistence)
    │   ├── nvs_device_manager_base.{h,cpp}  # Base class for NVS-backed device managers
    │   ├── dynamic_entity_base.{h,cpp} # DynamicEntityBase (shared persistence/activation)
    │   ├── EleroDynamicCover.{h,cpp}   # Dynamic cover slot (MQTT mode)
    │   ├── EleroDynamicLight.{h,cpp}   # Dynamic light slot (MQTT mode)
    │   ├── EleroRemoteControl.{h,cpp}  # Passive remote control tracker
    │   ├── command_sender.h            # Non-blocking TX with retries
    │   ├── sensor/                    # RSSI sensor platform
    │   │   └── __init__.py
    │   └── text_sensor/               # Blind status text sensor platform
    │       └── __init__.py
    ├── elero_mqtt/                     # MQTT mode component (NVS + MQTT HA discovery)
    │   ├── __init__.py                # Schema & codegen (slot allocation, hub wiring)
    │   ├── mqtt_device_manager.{h,cpp} # MqttDeviceManager (extends NvsDeviceManagerBase)
    │   ├── mqtt_publisher.h           # Abstract MQTT interface (testable without ESPHome)
    │   └── esphome_mqtt_adapter.h     # Adapts ESPHome MQTT client to MqttPublisher
    ├── elero_nvs/                      # Native+NVS mode component (NVS + ESPHome native API)
    │   ├── __init__.py                # Schema & codegen (slot allocation)
    │   ├── native_nvs_device_manager.{h,cpp}  # NativeNvsDeviceManager
    │   ├── NativeNvsCover.{h,cpp}     # cover::Cover + DynamicEntityBase + CoverCore
    │   └── NativeNvsLight.{h,cpp}     # light::LightOutput + DynamicEntityBase + LightCore
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

### Three Operating Modes

The system supports three mutually exclusive operating modes:

| | Native Mode | MQTT Mode | Native+NVS Mode |
|---|---|---|---|
| **Devices defined in** | YAML (compile-time) | NVS (runtime via CRUD API) | NVS (runtime, reboot to apply) |
| **Home Assistant API** | ESPHome native API | MQTT HA discovery | ESPHome native API |
| **Device manager** | `NativeDeviceManager` (no-op) | `MqttDeviceManager` | `NativeNvsDeviceManager` |
| **Component** | `elero:` only | `elero:` + `elero_mqtt:` | `elero:` + `elero_nvs:` |
| **Entity classes** | `EleroCover`, `EleroLight` | `EleroDynamicCover`, `EleroDynamicLight` | `NativeNvsCover`, `NativeNvsLight` |

**Core logic extraction:** All entity classes compose pure C++ core classes (`CoverCore`, `LightCore`) that contain all RF/device logic with zero ESPHome dependencies. This eliminates duplication and enables unit testing on the host.

**MQTT mode** enables runtime device management without reflashing:
- Devices stored as `NvsDeviceConfig` (60-byte POD struct) via ESPHome Preferences
- Pre-allocated slot pool: static arrays of `EleroDynamicCover`/`EleroDynamicLight`/`EleroRemoteControl`
- CRUD operations via WebSocket or programmatic API
- HA discovery publishes/removes MQTT config topics dynamically
- Remote controls auto-discovered from observed RF command packets

**Native+NVS mode** enables runtime device management with ESPHome native API:
- Same NVS persistence and CRUD as MQTT mode
- Entities registered with ESPHome native API during `setup()` — discovered by HA automatically
- Post-setup CRUD writes to NVS but changes only apply on reboot (ESPHome can't register new entities after initial connection)
- No MQTT broker required — uses ESPHome's built-in native API

**IDeviceManager** interface (`device_manager.h`) decouples the hub from mode-specific logic:
- Hub holds `IDeviceManager*` — `nullptr` in native mode, `MqttDeviceManager*` or `NativeNvsDeviceManager*` in NVS modes
- Hub calls `device_manager_->on_rf_packet()` for every decoded packet
- Web server calls `dm->upsert_device()` / `dm->remove_device()` for CRUD
- `NativeDeviceManager` is a no-op implementation for native mode

### Component Hierarchy

```
Elero (hub, SPIDevice + Component)
├── CoverCore (pure C++ — position tracking, state mapping, polling)
│   └── Composed by: EleroCover, EleroDynamicCover, NativeNvsCover
├── LightCore (pure C++ — brightness, dimming, state mapping)
│   └── Composed by: EleroLight, EleroDynamicLight, NativeNvsLight
├── EleroBlindBase (abstract interface)
│   ├── EleroCover (cover::Cover + Component + EleroBlindBase)          ← Native mode
│   ├── EleroDynamicCover (DynamicEntityBase + EleroBlindBase)          ← MQTT mode
│   └── NativeNvsCover (cover::Cover + Component + DynamicEntityBase)   ← Native+NVS mode
├── EleroLightBase (abstract interface)
│   ├── EleroLight (light::LightOutput + Component + EleroLightBase)    ← Native mode
│   ├── EleroDynamicLight (DynamicEntityBase + EleroLightBase)          ← MQTT mode
│   └── NativeNvsLight (light::LightOutput + Component + DynamicEntityBase) ← Native+NVS mode
├── DynamicEntityBase (shared persistence + activation lifecycle)
│   ├── EleroDynamicCover
│   └── EleroDynamicLight
├── EleroRemoteControl (passive RF observer, MQTT mode only)
├── NvsDeviceManagerBase (abstract base for NVS-backed managers)
│   └── MqttDeviceManager (NvsDeviceManagerBase — MQTT mode)
├── NativeNvsDeviceManager (IDeviceManager + Component — Native+NVS mode)
├── sensor::Sensor (RSSI, registered per blind address)
├── text_sensor::TextSensor (status, registered per blind address)
├── EleroWebServer (Component, Mongoose HTTP/WS)
│   └── EleroWebSwitch (switch::Switch + Component)
└── Auto-registered sensors/text sensors per cover (optional)
```

The `EleroBlindBase` / `EleroLightBase` abstract classes decouple the hub from entity implementations. `DynamicEntityBase` extracts shared NVS persistence, activation lifecycle, and `CommandSender` configuration shared between `EleroDynamicCover` and `EleroDynamicLight`.

### Core Data Flow

1. `Elero::setup()` configures CC1101 registers over SPI and attaches a GPIO interrupt on `gdo0_pin`.
2. When the CC1101 signals a received packet (GDO0 interrupt), `Elero::interrupt()` sets `received_ = true`.
3. `Elero::loop()` detects `received_`, reads the FIFO, decodes and decrypts the packet, then:
   - Dispatches state to matching `EleroBlindBase` via `set_rx_state()`
   - Calls `on_rf_packet_` callback → server broadcasts to WebSocket clients
   - Calls `device_manager_->on_rf_packet()` → MQTT mode tracks remotes
4. `EleroCover::loop()` / `EleroDynamicCover::loop()` handles polling timers, position recomputation, and drains the command queue.

### Dynamic Entity Lifecycle (MQTT mode)

All dynamic entities follow the same lifecycle:
```
set_preference() → restore() → activate() → [loop/update_config] → deactivate()
```

Key patterns:
- `active_` vs `registered_` flags: active = slot has config, registered = hub dispatches RF to it
- `set_config_enabled()` for non-destructive enable/disable (no deactivate/reactivate)
- `update_config()` for non-destructive config update (1 NVS write, no crash window)
- `deactivate()` is destructive: clears NVS, unregisters from hub, resets all state
- State callbacks must be set BEFORE `activate()` to avoid missing state changes
- `upsert_device()` creates or updates a device — address+type identify the device, all other fields are updateable

### Dependency Injection

The server and device manager receive events via callbacks, not direct coupling:

```cpp
// Hub calls this on every decoded RF packet
void Elero::set_rf_packet_callback(std::function<void(const RfPacketInfo&)> cb) {
  on_rf_packet_ = std::move(cb);
}

// Hub delegates to device manager (MQTT mode)
void Elero::set_device_manager(IDeviceManager *mgr) { device_manager_ = mgr; }
```

This keeps the hub independent of the web server and MQTT implementations.

---

## Key Classes and Files

### `components/elero/elero.h` / `elero.cpp`

**Class:** `Elero : public spi::SPIDevice<...>, public Component`
**Namespace:** `esphome::elero`

Critical public API:
- `register_cover(EleroBlindBase*)` — called by each `EleroCover` at setup
- `send_command(t_elero_command*)` — encodes, encrypts, and transmits a command
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
| `ELERO_MAX_COMMAND_QUEUE` | 10 | Max commands per blind to prevent OOM |

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

### `components/elero/dynamic_entity_base.h` / `dynamic_entity_base.cpp`

**Class:** `DynamicEntityBase` (MQTT mode only)

Shared base for `EleroDynamicCover` and `EleroDynamicLight`. Provides:
- NVS persistence: `restore()`, `save_config()`, `clear_config()`
- Activation lifecycle: `activate()`, `deactivate()`, `update_config()`
- Hub registration: `register_with_hub()`, `unregister_from_hub()` (via virtual hooks)
- `CommandSender` configuration from `NvsDeviceConfig`
- Common RX metadata tracking (`last_seen_ms_`, `last_rssi_`, `last_state_raw_`)

Derived classes implement: `entity_tag_()`, `entity_type_str_()`, `is_matching_type_()`, `do_register_()`, `do_unregister_()`, `reset_entity_state_()`.

### `components/elero_mqtt/mqtt_device_manager.h` / `mqtt_device_manager.cpp`

**Class:** `MqttDeviceManager : public IDeviceManager, public Component`

Key behaviors:
- Implements `IDeviceManager` for MQTT mode
- Manages pre-allocated slot pools for covers, lights, and remotes
- On `setup()`: restores slots from NVS, sets state callbacks, activates
- On `loop()`: detects MQTT (re)connection, publishes discoveries, loops active entities
- CRUD operations: `upsert_device()` creates or updates devices (address+type is the key)
- Auto-discovers remotes from RF command packets (`track_remote_()`)
- Publishes MQTT HA discovery configs and state topics
- `notify_crud_()` broadcasts events to WS clients via `CrudEventCallback`

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
- On connect: sends `config` event with blinds array, mode (`native`/`mqtt`), and CRUD capability
- On RF packet (via callback from hub): broadcasts `rf` event to all clients
- On log (via ESPHome logger callback): broadcasts `log` event to clients
- On `cmd` message: routes command to hub's `send_command()`
- On `raw` message: routes to hub's `send_raw_command()`
- In MQTT mode: handles `upsert_device`/`remove_device` via `IDeviceManager`
- Receives CRUD event broadcasts from `MqttDeviceManager` via `CrudEventCallback`

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
| `config` | Sent on connect: device info, configured blinds/lights, frequency, mode |
| `rf` | Every decoded RF packet: addresses, state, RSSI, raw bytes |
| `log` | ESPHome log entries with `elero.*` tags |
| `device_upserted` | NVS modes: device was created or updated (address, type) |
| `device_removed` | NVS modes: device was removed (address) |

**Client → Server Messages:**

| Type | Description |
|------|-------------|
| `cmd` | Send command to blind/light: `{"type":"cmd", "address":"0xADDRESS", "action":"up"}` |
| `raw` | Send raw RF packet for testing: `{"type":"raw", "dst_address":"0x...", "channel":5, ...}` |
| `upsert_device` | NVS modes: create or update device from NvsDeviceConfig fields |
| `remove_device` | NVS modes: remove device by `dst_address` + `device_type` |

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
- `dst_address` — 3-byte hex destination address of the motor (e.g., `0xa831e5`)
- `channel` — RF channel number of the blind
- `src_address` — 3-byte hex source address of the remote control paired with the blind

Optional parameters (with defaults):
- `poll_interval` (default `5min`, or `never`) — how often to query blind status
- `open_duration` / `close_duration` (default `0s`) — enables position tracking
- `supports_tilt` (default `false`)
- `auto_sensors` (default `true`) — auto-generate RSSI and status text sensors for this cover
- `payload_1` (default `0x00`), `payload_2` (default `0x04`)
- `type` (default `0x6a`), `type2` (default `0x00`)
- `hop` (default `0x0a`)
- `command_up/down/stop/check/tilt` — override RF command bytes if non-standard

### Light (`light: platform: elero`)

Required parameters:
- `dst_address` — 3-byte hex destination address of the light receiver (e.g., `0xc41a2b`)
- `channel` — RF channel number of the light
- `src_address` — 3-byte hex source address of the remote control paired with the light

Optional parameters (with defaults):
- `dim_duration` (default `0s`) — time for dimming from 0% to 100%; `0s` = on/off only, `>0` = brightness control
- `payload_1` (default `0x00`), `payload_2` (default `0x04`)
- `type` (default `0x6a`), `type2` (default `0x00`)
- `hop` (default `0x0a`)
- `command_on/off/dim_up/dim_down/stop/check` — override RF command bytes if non-standard

### Sensors

```yaml
sensor:
  - platform: elero
    dst_address: 0xa831e5     # Required: which blind
    name: "Blind RSSI"        # Unit: dBm

text_sensor:
  - platform: elero
    dst_address: 0xa831e5
    name: "Blind Status"      # Values: see state constants above
```

### MQTT Mode (`elero_mqtt`)

Enables runtime device management via NVS persistence and MQTT HA discovery. Requires `mqtt:` component.

```yaml
elero_mqtt:
  topic_prefix: elero              # MQTT topic prefix (default: "elero")
  discovery_prefix: homeassistant  # HA discovery prefix (default: "homeassistant")
  max_covers: 16                   # Pre-allocated cover slots (1–32, default: 16)
  max_lights: 8                    # Pre-allocated light slots (1–32, default: 8)
  max_remotes: 16                  # Pre-allocated remote slots (1–32, default: 16)
  device_name: "Elero Gateway"     # HA device name (default: "Elero Gateway")
```

When `elero_mqtt` is present, no covers/lights should be defined in YAML — devices are added at runtime via the web UI or MQTT API and persisted in NVS.

### Native+NVS Mode (`elero_nvs`)

Enables runtime device management via NVS persistence with ESPHome native API (no MQTT broker required). Requires `api:` component.

```yaml
elero_nvs:
  max_covers: 16                   # Pre-allocated cover slots (1–32, default: 16)
  max_lights: 8                    # Pre-allocated light slots (1–32, default: 8)
  max_remotes: 16                  # Pre-allocated remote slots (1–32, default: 16)
```

Devices are added via the web UI CRUD API and persisted in NVS. On boot, active devices are registered with ESPHome's native API. Post-boot CRUD writes to NVS but new entities only appear after a reboot.

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

1. Add `elero_web` to your configuration
2. Flash the device
3. Open `http://<device-ip>/elero`
4. Operate each blind with its original remote
5. RF packets appear in the web UI — addresses not in config are "discovered"
6. Add the discovered addresses to your YAML config

---

## Testing

### Unit tests (host)

Pure C++ core logic is tested with GoogleTest on the host machine:

```bash
cd tests/unit && cmake -B build && cmake --build build && ctest --test-dir build
```

Tests cover `CoverCore` (position tracking, state mapping, polling) and `LightCore` (brightness, dimming, state mapping).

### Compile tests

```bash
esphome compile tests/test.esp32-minimal.yaml   # Native mode
esphome compile tests/test.esp32-mqtt.yaml       # MQTT mode
esphome compile tests/test.esp32-nvs.yaml        # Native+NVS mode
```

### Hardware validation

1. Flash the firmware and verify the CC1101 initialises (check `esphome logs` for `[I][elero:...]` messages)
2. Open the web UI and verify RF packets appear when using the remote
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
