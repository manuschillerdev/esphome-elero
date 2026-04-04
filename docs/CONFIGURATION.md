# Configuration Reference: Elero ESPHome Component

Complete reference for all configurable parameters.

---

## Hub: `elero`

The central hub controls SPI communication with the CC1101 radio module.

```yaml
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26
  freq0: 0x7a
  freq1: 0x71
  freq2: 0x21
```

| Parameter | Type | Required | Default | Description |
|---|---|---|---|---|
| `cs_pin` | GPIO pin | Yes | - | SPI chip select pin for the CC1101 |
| `gdo0_pin` | GPIO pin (input) | Yes | - | CC1101 GDO0 interrupt pin |
| `freq0` | Hex (0x00-0xFF) | No | `0x7a` | CC1101 frequency register FREQ0 |
| `freq1` | Hex (0x00-0xFF) | No | `0x71` | CC1101 frequency register FREQ1 |
| `freq2` | Hex (0x00-0xFF) | No | `0x21` | CC1101 frequency register FREQ2 |

> The hub extends the ESPHome SPI configuration. `spi:` must be configured separately with `clk_pin`, `mosi_pin`, and `miso_pin`.

### Frequency Variants

| Variant | freq0 | freq1 | freq2 | Notes |
|---|---|---|---|---|
| Standard 868 MHz | `0x7a` | `0x71` | `0x21` | Default setting |
| Alternative 868 MHz | `0xc0` | `0x71` | `0x21` | Most common alternative |

---

## Platform: `cover`

Each roller blind is configured as a separate cover entry.

```yaml
cover:
  - platform: elero
    name: "Schlafzimmer"
    dst_address: 0xa831e5
    channel: 4
    src_address: 0xf0d008
    open_duration: 25s
    close_duration: 22s
    supports_tilt: false
    payload_1: 0x00
    payload_2: 0x04
    type: 0x6a
    type2: 0x00
    hop: 0x0a
    command_up: 0x20
    command_down: 0x40
    command_stop: 0x10
    command_check: 0x00
    command_tilt: 0x24
```

### Required Parameters

| Parameter | Type | Description |
|---|---|---|
| `name` | String | Display name in Home Assistant |
| `dst_address` | Hex (24-bit, 0x0-0xFFFFFF) | RF address of the blind (destination address) |
| `channel` | Integer (0-255) | RF channel of the blind |
| `src_address` | Hex (24-bit, 0x0-0xFFFFFF) | RF address of the remote control to emulate (source address) |

### Optional Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `open_duration` | Duration | `0s` | Travel time to fully open. Required for time-based position tracking. If set, `close_duration` must also be set. |
| `close_duration` | Duration | `0s` | Travel time to fully close. Required for time-based position tracking. If set, `open_duration` must also be set. |
| `supports_tilt` | Boolean | `false` | Enables tilt support (e.g. for venetian blinds). |
| `auto_sensors` | Boolean | `true` | Automatically creates RSSI and status sensors for this blind. Set to `false` to configure them manually. |

### Protocol Parameters

These values are read from the log of the original remote control. They do not need to be specified if they match the defaults.

| Parameter | Type | Default | Description |
|---|---|---|---|
| `payload_1` | Hex (0x00-0xFF) | `0x00` | First payload byte |
| `payload_2` | Hex (0x00-0xFF) | `0x04` | Second payload byte |
| `type` | Hex (0x00-0xFF) | `0x6a` | Message type (0x6a=command, 0xca=status) |
| `type2` | Hex (0x00-0xFF) | `0x00` | Secondary type byte |
| `hop` | Hex (0x00-0xFF) | `0x0a` | Hop counter |

### Command Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `command_up` | Hex (0x00-0xFF) | `0x20` | Command code: open blind |
| `command_down` | Hex (0x00-0xFF) | `0x40` | Command code: close blind |
| `command_stop` | Hex (0x00-0xFF) | `0x10` | Command code: stop blind |
| `command_check` | Hex (0x00-0xFF) | `0x00` | Command code: query status |
| `command_tilt` | Hex (0x00-0xFF) | `0x24` | Command code: tilt |

---

## Platform: `light`

Each Elero light (e.g. a home light with an Elero receiver) is configured as a separate light entry. The light appears in Home Assistant as a full light entity -- with on/off and optional brightness control.

```yaml
light:
  - platform: elero
    name: "Wohnzimmerlicht"
    dst_address: 0xc41a2b
    channel: 6
    src_address: 0xf0d008
    dim_duration: 5s        # Optional: 0s = on/off only, >0 = brightness controllable
    payload_1: 0x00
    payload_2: 0x04
    type: 0x6a
    type2: 0x00
    hop: 0x0a
    command_on: 0x20
    command_off: 0x40
    command_dim_up: 0x20
    command_dim_down: 0x40
    command_stop: 0x10
    command_check: 0x00
```

### Required Parameters

| Parameter | Type | Description |
|---|---|---|
| `name` | String | Display name in Home Assistant |
| `dst_address` | Hex (24-bit, 0x0-0xFFFFFF) | RF address of the light (destination address) |
| `channel` | Integer (0-255) | RF channel of the light |
| `src_address` | Hex (24-bit, 0x0-0xFFFFFF) | RF address of the remote control to emulate (source address) |

### Optional Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `dim_duration` | Duration | `0s` | Dimming travel time from 0% to 100%. `0s` = on/off only (`ColorMode::ON_OFF`); value > 0 = brightness control enabled (`ColorMode::BRIGHTNESS`). |

### Protocol Parameters

These values are read from the log of the original remote control (same meaning as for `cover`).

| Parameter | Type | Default | Description |
|---|---|---|---|
| `payload_1` | Hex (0x00-0xFF) | `0x00` | First payload byte |
| `payload_2` | Hex (0x00-0xFF) | `0x04` | Second payload byte |
| `type` | Hex (0x00-0xFF) | `0x6a` | Message type (0x6a=command, 0xca=status) |
| `type2` | Hex (0x00-0xFF) | `0x00` | Secondary type byte |
| `hop` | Hex (0x00-0xFF) | `0x0a` | Hop counter |

### Command Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `command_on` | Hex (0x00-0xFF) | `0x20` | Command code: turn light on |
| `command_off` | Hex (0x00-0xFF) | `0x40` | Command code: turn light off |
| `command_dim_up` | Hex (0x00-0xFF) | `0x20` | Command code: dim up (only when `dim_duration > 0`) |
| `command_dim_down` | Hex (0x00-0xFF) | `0x40` | Command code: dim down (only when `dim_duration > 0`) |
| `command_stop` | Hex (0x00-0xFF) | `0x10` | Command code: stop dimming |
| `command_check` | Hex (0x00-0xFF) | `0x00` | Command code: query status |

---

## Automatic Sensors (`auto_sensors`)

Diagnostic sensors are automatically created for each cover and light block when `auto_sensors: true` is set (the default). Separate `sensor:` / `text_sensor:` / `binary_sensor:` platform blocks are no longer needed.

Sensors automatically created per device:

| Sensor | Type | Description |
|---|---|---|
| RSSI | `sensor` (dBm, device_class: signal_strength) | Signal strength of the last packet |
| Status | `text_sensor` | Last blind status as text |
| Problem | `binary_sensor` | `true` on blocking/overheated/timeout |
| Command source | `text_sensor` | Last command source |
| Problem type | `text_sensor` | Type of problem |

To disable automatic sensor creation, set `auto_sensors: false` in the cover/light block.

> **Migration:** Standalone sensor platforms (`sensor: platform: elero`, `text_sensor: platform: elero`) have been removed. Sensors are now automatically created by cover/light entities via `auto_sensors: true`.

### RSSI Reference Values

| RSSI (dBm) | Rating |
|---|---|
| > -50 | Excellent |
| -50 to -70 | Good |
| -70 to -85 | Acceptable |
| < -85 | Weak / unreliable |

### Possible Status Values

| Value | Description |
|---|---|
| `top` | Blind fully open (upper end position) |
| `bottom` | Blind fully closed (lower end position) |
| `intermediate` | Blind at an intermediate position |
| `tilt` | Blind in tilt position |
| `top_tilt` | Blind at top, tilted |
| `bottom_tilt` | Blind at bottom, tilted |
| `moving_up` | Blind moving up |
| `moving_down` | Blind moving down |
| `start_moving_up` | Blind starting to move up |
| `start_moving_down` | Blind starting to move down |
| `stopped` | Blind stopped (intermediate position) |
| `blocking` | Blind blocked (error!) |
| `overheated` | Motor overheated (error!) |
| `timeout` | Timeout (error!) |
| `on` | Turned on |
| `unknown` | Unknown state |

---

## Web UI: `elero_web`

Optional web interface for device discovery and YAML generation. Accessible at `http://<device-ip>/elero`.

```yaml
# web_server_base is automatically loaded by elero_web.
# Declare it explicitly to configure the port:
web_server_base:
  port: 80

elero_web:
```

**Prerequisites:**
- `web_server_base` is automatically loaded by `elero_web`. Do **not** use `web_server:` -- that re-enables the default ESPHome UI at `/`. Navigating to `/` will redirect automatically to `/elero`.

**Features:**
- **Device discovery** -- view RF packets from Elero devices in real time
- **Configured devices** -- status of blinds and lights
- **Raw TX** -- send test commands for debugging
- **Logs** -- ESPHome logs in real time

**WebSocket communication:**

The web UI communicates via WebSocket (`/elero/ws`) for real-time updates. See `docs/ARCHITECTURE.md` for the complete protocol.

| Endpoint | Description |
|---|---|
| `/` | Redirect to `/elero` |
| `/elero` | Web UI (HTML) |
| `/elero/ws` | WebSocket for real-time communication |

**Server -> Client Events:**

| Event | Description |
|---|---|
| `config` | Device configuration on connection |
| `rf` | Decoded RF packets in real time |
| `log` | ESPHome log entries with `elero.*` tags |
| `device_upserted` | NVS modes: device was created or updated (address, type) |
| `device_removed` | NVS modes: device was removed (address) |

**Client -> Server Messages:**

| Type | Description |
|---|---|
| `cmd` | Command to blind/light: `{"type":"cmd", "address":"0xADDRESS", "action":"up"}` |
| `raw` | Raw RF packet for testing: `{"type":"raw", "dst_address":"0x...", "src_address":"0x...", "channel":5, ...}` |
| `upsert_device` | NVS modes: create or update device (NvsDeviceConfig fields) |
| `remove_device` | NVS modes: remove device by `dst_address` + `device_type` |

**Why Mongoose?**

The web UI uses the Mongoose HTTP/WebSocket library instead of ESPHome's `web_server_base`. Reason: ESPHome uses different implementations depending on the framework (Arduino vs. ESP-IDF). Mongoose provides a unified API for both frameworks.

---

## Platform: `switch` (Web UI Control)

Optional runtime control to enable/disable the web UI. When disabled, all `/elero` endpoints respond with HTTP 503.

```yaml
switch:
  - platform: elero_web
    name: "Elero Web UI"
    id: elero_web_switch
    restore_mode: RESTORE_DEFAULT_ON
```

| Parameter | Type | Required | Default | Description |
|---|---|---|---|---|
| `name` | String | Yes | - | Display name in Home Assistant |
| `id` | String | No | `elero_web_switch` | Unique component ID |
| `restore_mode` | Enum | No | `RESTORE_DEFAULT_ON` | `RESTORE_DEFAULT_ON` or `RESTORE_DEFAULT_OFF` |

**Prerequisites:**
- `elero_web` must be present in the configuration
- This switch is optional; if not present, the web UI is always active

---

## MQTT Mode: `elero_mqtt`

The `elero_mqtt` component enables runtime device management via NVS persistence and MQTT Home Assistant discovery. It requires the ESPHome `mqtt:` component.

```yaml
mqtt:
  broker: 192.168.1.100

elero_mqtt:
  topic_prefix: elero
  discovery_prefix: homeassistant
  device_name: "Elero Gateway"
```

| Parameter | Type | Required | Default | Description |
|---|---|---|---|---|
| `topic_prefix` | String | No | `elero` | MQTT topic prefix for all device topics |
| `discovery_prefix` | String | No | `homeassistant` | Home Assistant MQTT discovery prefix |
| `device_name` | String | No | `Elero Gateway` | Device name in Home Assistant |

**Important notes:**
- When `elero_mqtt` is present, **no** covers or lights should be defined in YAML -- devices are added at runtime via the web UI or MQTT API.
- Devices are stored in NVS (unified 48-slot pool).
- The `mqtt:` component must be present in the ESPHome configuration.
- Remote controls are automatically discovered from observed RF command packets.

---

## Native+NVS Mode: `elero_nvs`

The `elero_nvs` component enables runtime device management via NVS persistence with the native ESPHome API (no MQTT broker required).

```yaml
elero_nvs:
```

**No configuration parameters** -- simply including the component enables NVS persistence.

**Important notes:**
- Devices are added via the web UI CRUD API and stored in NVS.
- On boot, active devices are restored from NVS and registered with the native ESPHome API.
- CRUD operations after boot write to NVS, but new entities only appear after a reboot (ESPHome limitation: entities cannot be registered after the initial connection).
- No MQTT broker required -- uses the built-in native ESPHome API.

---

## Complete Example

A complete configuration with all platforms:

```yaml
external_components:
  - source: github://pfriedrich84/esphome-elero

spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26

cover:
  - platform: elero
    name: "Schlafzimmer"
    dst_address: 0xa831e5
    channel: 4
    src_address: 0xf0d008
    open_duration: 25s
    close_duration: 22s

  - platform: elero
    name: "Wohnzimmer"
    dst_address: 0xb912f3
    channel: 5
    src_address: 0xf0d008

light:
  - platform: elero
    name: "Wohnzimmerlicht"
    dst_address: 0xc41a2b
    channel: 6
    src_address: 0xf0d008
    # dim_duration: 5s  # Enable for brightness control

# Sensors (RSSI, status, problem, etc.) are automatically created by the cover/light blocks
# when auto_sensors: true (the default). No separate sensor:/text_sensor: blocks needed.

# Web UI for discovery (do NOT use web_server: — use web_server_base: instead)
web_server_base:
  port: 80

elero_web:

# Optional: Runtime control to disable/enable the web UI
switch:
  - platform: elero_web
    name: "Elero Web UI"
    restore_mode: RESTORE_DEFAULT_ON
```

See also: [Installation Guide](INSTALLATION.md) | [README](../README.md) | [Example YAML](../example.yaml)
