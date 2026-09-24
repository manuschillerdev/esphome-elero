# Configuration Reference: Elero ESPHome Component

Complete reference for all YAML-configurable parameters.

> **Heads up — devices are no longer YAML-defined.** Since v0.11.0
> (RFC-002), `cover: - platform: elero` and `light: - platform: elero`
> are gone. All blinds and lights live in NVS and are managed through
> the web UI. See [`MIGRATION-yaml-to-nvs.md`](MIGRATION-yaml-to-nvs.md)
> if you're upgrading.
>
> What stays in YAML: the radio hub, the output adapter (`elero_nvs:` or
> `elero_mqtt:`), and the web UI.

---

## Hub: `elero`

The central hub controls SPI communication with the radio module. Three radios are supported:

| Radio | Chip | Interface | Status |
|---|---|---|---|
| `cc1101` | TI CC1101 | Register-based SPI | Stable (default) |
| `sx1262` | Semtech SX1262 | Command-based SPI | Experimental |
| `sx1276` | Semtech SX1276/77/78 | Register-based SPI | Experimental |

### Common Parameters

| Parameter | Type | Required | Default | Description |
|---|---|---|---|---|
| `radio` | String | No | `cc1101` | Radio driver: `cc1101`, `sx1262`, or `sx1276` |
| `cs_pin` | GPIO pin | Yes | - | SPI chip select pin |
| `irq_pin` | GPIO pin (input) | Yes | - | Interrupt pin (GDO0 for CC1101, DIO1 for SX1262, DIO0 for SX1276) |
| `gdo0_pin` | GPIO pin (input) | - | - | Deprecated alias for `irq_pin` (CC1101 backward compatibility) |
| `freq0` | Hex (0x00-0xFF) | No | `0x7a` | CC1101-format frequency register FREQ0 |
| `freq1` | Hex (0x00-0xFF) | No | `0x71` | CC1101-format frequency register FREQ1 |
| `freq2` | Hex (0x00-0xFF) | No | `0x21` | CC1101-format frequency register FREQ2 |

> The hub extends the ESPHome SPI configuration. `spi:` must be configured separately with `clk_pin`, `mosi_pin`, and `miso_pin`.

### CC1101 Configuration

```yaml
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26
```

No additional parameters required. The CC1101 is the default radio.

### SX1262 Configuration

| Parameter | Type | Required | Default | Description |
|---|---|---|---|---|
| `busy_pin` | GPIO pin (input) | Yes | - | SX1262 BUSY pin |
| `rst_pin` | GPIO pin (output) | Yes | - | SX1262 reset pin |
| `fem_pa_pin` | GPIO pin (output) | No | - | External PA enable pin (e.g. Heltec V4 GPIO46) |
| `rf_switch` | Boolean | No | `false` | Use DIO2 as RF switch control |
| `pa_power` | Integer (-3 to 22) | No | `22` | TX output power in dBm |
| `tcxo_voltage` | Float (1.6-3.3) | No | - | TCXO voltage via DIO3 (omit if using crystal) |
| `regulator_ldo` | Boolean | No | `false` | Select LDO regulation instead of DC-DC; PaperMono uses `true` |

```yaml
elero:
  radio: sx1262
  cs_pin: GPIO8
  irq_pin: GPIO14      # DIO1
  busy_pin: GPIO13
  rst_pin: GPIO12
  rf_switch: true
  tcxo_voltage: 1.8
```

### SX1276 Configuration

| Parameter | Type | Required | Default | Description |
|---|---|---|---|---|
| `rst_pin` | GPIO pin (output) | Yes | - | SX1276 reset pin |
| `pa_power` | Integer (-1 to 20) | No | `17` | TX output power in dBm |

```yaml
elero:
  radio: sx1276
  cs_pin: GPIO18
  irq_pin: GPIO26      # DIO0
  rst_pin: GPIO23
  pa_power: 17
```

### Frequency Variants

| Variant | freq0 | freq1 | freq2 | Notes |
|---|---|---|---|---|
| Standard 868 MHz | `0x7a` | `0x71` | `0x21` | Default setting |
| Alternative 868 MHz | `0xc0` | `0x71` | `0x21` | Most common alternative |

---

## PaperMono native frontend: `elero_paper`

This frontend and its `paper_mono` board support are opt-in. The core and web server
never enable them. Use [the standalone configuration](../configs/config.paper-mono.yaml)
for the board's fixed pin mapping, and see [the integration guide](PAPER_MONO.md)
for optional web/HA integration and module ownership.

| Parameter | Required | Default | Description |
|---|---|---|---|
| `board_id` | Yes | — | ID of the `paper_mono` board support component |
| `font_id`, `title_font_id` | Yes | — | Body and heading fonts |
| `spi_id`, `cs_pin`, `dc_pin`, `busy_pin` | Yes | — | Display SPI bus and control pins |
| `elero_id`, `registry_id` | No | Resolved by ESPHome | Hub and registry to observe |
| `storage` | No | `false` | Include optional microSD JSON backup/restore |

The frontend enables registry NVS persistence and passive receiver discovery. It
does not enable Wi-Fi, HTTP, MQTT, or the native API. Board support requires an
ESP32-S3 and fixes the I²C expander address to `0x4F`. Its GPIO provider supports
only output PYG10, the radio reset pin.

The web and SD workflows share the internal `elero_config` component. If you use
an explicit `external_components.components` allowlist, include this dependency
when enabling either workflow. The PaperMono example omits the allowlist so
internal dependencies resolve automatically; making components available does
not enable them. See the integration guide's module table for restricted lists.

## Adding devices (no YAML)

Devices live in NVS. Pick exactly one of the two output adapters below
(`elero_nvs:` for native ESPHome API, `elero_mqtt:` for MQTT discovery),
flash, then add devices through the web UI:

1. Open `http://<device-ip>/elero`.
2. Press a button on a physical Elero remote — it auto-appears in the UI.
3. Click **Save** to persist the discovered cover/light to NVS.
4. (Optional) Hub → Backup & Restore → **Download backup**. Keep the
   JSON safe for chip-swap recovery.

To restore from a backup (or migrate from older YAML): Hub → Backup & Restore → **Restore from backup**.

### Per-device fields managed in the UI

These are stored in `NvsDeviceConfig` (see `components/elero/nvs_config.h`)
and edited via the web UI's device editor. Fields marked **(cover)** and
**(light)** apply to that device type only.

| Field | Type | Default | Description |
|---|---|---|---|
| `dst_address` | Hex (24-bit) | required | RF address of the blind/light |
| `src_address` | Hex (24-bit) | required | Emulated remote address |
| `channel` | Integer (0-255) | required | RF channel |
| `name` | String (≤23 chars) | empty | Display name in HA |
| `enabled` | Boolean | `true` | Whether the device is published to HA |
| `open_duration_ms` | Integer | `0` | **(cover)** Travel time fully open. `0` = no position tracking. |
| `close_duration_ms` | Integer | `0` | **(cover)** Travel time fully closed. Pair with `open_duration_ms`. |
| `supports_tilt` | Boolean | `false` | **(cover)** Tilt-capable blind |
| `ha_device_class` | Enum | `shutter` | **(cover)** `shutter`/`blind`/`awning`/`curtain`/`shade`/`garage` |
| `dim_duration_ms` | Integer | `0` | **(light)** Dimming travel. `0` = on/off only. |
| `hop` | Hex byte | `0x0a` | Protocol hop counter |
| `payload_1` | Hex byte | `0x00` | Protocol payload byte 1 |
| `payload_2` | Hex byte | `0x04` | Protocol payload byte 2 |
| `msg_type` | Hex byte | `0x6a` | Message type (`0x6a` = command) |
| `type2` | Hex byte | `0x00` | Secondary type byte |

The protocol bytes (`hop`, `payload_*`, `msg_type`, `type2`) are rarely
overridden — defaults match every Elero motor seen in the wild. Only
touch them if you've sniffed an unusual remote and confirmed it differs.

---

## Web UI: `elero_web`

Web interface for device CRUD, RF discovery, backup/restore, and live RF/log tailing. Accessible at `http://<device-ip>/elero`.

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
- **Devices tab** — add, edit, remove blinds/lights/remotes.
- **Hub tab** — display name override, frequency presets, raw TX, **Backup & Restore**.
- **Packets tab** — live RF traffic.
- **Logs** — ESPHome `elero.*` logs in real time.

### WebSocket protocol

The web UI talks to the device over WebSocket at `/elero/ws`. The full protocol is documented as an AsyncAPI spec at `components/elero_web/frontend/app/asyncapi.yaml` — that file is the authoritative source.

| Endpoint | Description |
|---|---|
| `/` | Redirect to `/elero` |
| `/elero` | Web UI (HTML, served gzipped) |
| `/elero/ws` | WebSocket for real-time communication |

Server → client events include `config`, `rf`, `log`, `device_upserted`, `device_removed`, `state_changed`, `hub_config`, `config_snapshot`, `import_result`, `error`. Client → server messages include `cmd`, `raw`, `upsert_device`, `remove_device`, `set_hub_config`, `export_config`, `import_config`, `restart`.

**Why Mongoose?** ESPHome ships different HTTP backends per framework (Arduino vs ESP-IDF). Mongoose provides one API for both.

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

Prerequisites: `elero_web` must be present.

---

## Output adapter (pick one)

### MQTT mode: `elero_mqtt`

Surfaces devices to Home Assistant via MQTT discovery. Requires the ESPHome `mqtt:` component.

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
| `device_name` | String | No | `Elero Gateway` | YAML default for the gateway device name. The web UI can override this at runtime; the override is persisted to NVS. |

Notes:
- Devices added through the web UI appear in HA immediately (no reboot).
- Auto-discovered remotes (from observed RF) are **not** published until you save them.

### Native API mode: `elero_nvs`

Surfaces devices to Home Assistant via the ESPHome native API. No MQTT broker required.

```yaml
api:
elero_nvs:
```

No configuration parameters — including the component enables it.

Notes:
- On boot, active NVS devices are registered as ESPHome cover/light entities.
- ESPHome can't add entities after the initial API connection — adding/removing devices via the UI requires a reboot to surface the change to HA. The UI prompts for this automatically.
- 48-slot pool is pre-allocated (`MAX_DEVICES`); empty slots have negligible runtime cost.

---

## Complete Example

```yaml
external_components:
  - source: github://manuschillerdev/esphome-elero

spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26

# Pick one: elero_nvs (native API) or elero_mqtt
elero_nvs:

# Web UI + Backup/Restore
web_server_base:
  port: 80

elero_web:

# Optional: HA toggle for the web UI
switch:
  - platform: elero_web
    name: "Elero Web UI"
    restore_mode: RESTORE_DEFAULT_ON
```

After flashing, open `http://<device-ip>/elero` to add devices.

See also: [Installation Guide](INSTALLATION.md) | [Backup &amp; Restore](BACKUP-RESTORE.md) | [Migration from YAML](MIGRATION-yaml-to-nvs.md) | [README](../README.md) | [Example YAML](../example.yaml)
