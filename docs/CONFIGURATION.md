# Configuration Reference: Elero ESPHome Component

Complete reference for all YAML-configurable parameters.

Requires **ESPHome 2026.9.0 or newer**, with ESP32 using ESP-IDF or Arduino.
See [ESPHome compatibility](ESPHOME-COMPATIBILITY.md) for native API lifecycle details.

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
| `movement_mode` | `standard` / `step` | `standard` | **(cover)** Step mode makes Open/Close use targeted continuous travel. |
| `supports_tilt` | Boolean | `false` | **(cover)** Expose the motor's saved tilt preset; not arbitrary slat angles. |
| `command_up` | Hex byte | `0x20` | **(cover)** Full opening command |
| `command_down` | Hex byte | `0x40` | **(cover)** Full closing command |
| `command_stop` | Hex byte | `0x10` | **(cover)** Stop command |
| `command_check` | Hex byte | `0x00` | **(cover)** Status query command |
| `command_tilt` | Hex byte | `0x24` | **(cover)** Saved tilt command |
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

### Step receiver movement mode

Set `movement_mode` to `step` for a receiver whose ordinary UP/DOWN commands
only make short movements. The cover editor calls this **Step receiver**.
Save the device to apply the mode. It applies to normal Open/Close commands from
all adapters, calibrated position commands, and saved groups.

| Action | Wire command | Packet type / destination |
|---|---|---|
| Open in step mode, or explicit `full_up` | `0x21` | `0x6a`, the cover's 24-bit receiver address |
| Close in step mode, or explicit `full_down` | `0x41` | `0x6a`, the cover's 24-bit receiver address |
| Explicit `step_up` | `0x20`, then release `0x00` | `0x44`, the paired remote channel |
| Explicit `step_down` | `0x40`, then release `0x00` | `0x44`, the paired remote channel |
| Saved tilt / intermediate preset | `command_tilt` / `0x44` | Existing channel-button path |
| Stop / status query | `command_stop` / `command_check` | Existing receiver-addressed path |

Full-travel packets use `type2=0x10`, `hop=0x00`, as in the captured long-press
transactions. Each full-travel action uses the existing three-packet repetition
and follow-up CHECK. A short step uses three press packets followed by three
release packets with the existing 10 ms repeat interval. Both phases are
reserved before the step is accepted, so a full queue cannot accept only a press.
This is a short pulse, not a reproduction of every remote's hold duration.

Step mode takes precedence over `command_up` and `command_down`; those custom
values are retained and become active again in standard mode. In standard mode,
UP/DOWN overrides that resolve to `0x21` or `0x41` also use targeted full-travel
packets. Other custom commands keep their existing packet format. Explicit
`full_up`/`full_down` and `step_up`/`step_down` bypass the UP/DOWN overrides.

Both native API and MQTT discovery expose four Home Assistant buttons for each
enabled cover: **Short press up/down** and **Full up/down**. Button presses use
the same registry action dispatch as WebSocket `cmd` and the MQTT cover command
topic. No adapter selects packet bytes, estimates positions, or maintains a
button state. The public action IDs remain `step_up`, `step_down`, `full_up`, and
`full_down`; “short press” describes the transmitted action without promising a
fixed movement distance.

Native API registers the buttons at boot with stable address/action identities
and names that remain valid for the firmware lifetime. After device changes,
follow the native mode reboot prompt to update entity registration. Existing
buttons resolve their cover through the registry on each press; disabled or
removed covers are rejected even before a reboot. A reused registry slot does
not change which cover an existing button controls.

MQTT publishes retained `button` discovery configs under the same HA device as
the cover, with the gateway's availability topic. Each button uses the existing
cover `/set` subscription and its canonical action as `payload_press`. Commands
are not retained. Rename and reconnect retain stable discovery identities;
disable/remove clears all four button configs. Startup cleanup includes the
button domain and removes only stale discovery owned by this gateway.

The explicit actions are also available directly through WebSocket `cmd` and
the MQTT cover command topic. Step actions apply to one configured cover at a time; the RF
channel can still contain several paired receivers. On standard roller shutter
receivers, even a short press can start full travel. Test the actual receiver.

Groups containing full-travel actions send one targeted command per receiver;
they do not encode those commands as a multi-selector `0x44` packet. Group start
times are therefore not guaranteed to be simultaneous.

The mode uses a previously unused flag bit. The NVS record stays at 64 bytes,
with the same version and field offsets. Export/import includes `movement_mode`;
old backups without it restore standard mode. Older firmware ignores the new
mode and uses its original movement handling. No new angle or position feedback
is inferred. See [hardware testing](TESTING_STEP_RECEIVERS.md).

### Cover position and tilt

For percentage positioning, expand a cover in the Devices tab and enter its
measured full opening and closing travel times in seconds (the up/down fields).
Both must be greater than zero. The firmware stores these as `open_duration_ms`
and `close_duration_ms`, estimates position from elapsed travel time, and sends
STOP when an intermediate target is reached. Use a full travel to a confirmed
endpoint to establish a reference; intermediate positions remain estimates and
can drift. Do not use placeholder travel times just to enable a slider.

In MQTT mode, saving these values updates discovery and republishes known state
without moving the cover. In native API mode, reboot after changing capabilities
so Home Assistant receives the updated entity information. A HomeKit bridge can
cache accessory capabilities: after verifying `current_position` and position
control in HA, use HA's `homekit.reset_accessory` for only the affected cover if
Apple Home still has the old controls. Resetting an accessory can require restoring
its Apple Home room, name, scenes and automations; do not reset the entire bridge.

`supports_tilt` exposes the existing Elero saved-preset command and binary tilt
state (0/100). It does **not** measure a slat angle or make arbitrary percentage
tilt targets possible; the current adapters recall the same preset for tilt
requests. Enable it only for motors with a programmed tilt preset.

Cover state topics are retained at the MQTT broker so a restarted HA or a new
discovery subscription receives the last reported state. The gateway's availability
topic still marks covers unavailable when it disconnects. Removing or disabling
a cover clears its retained state, and removing position/tilt support clears the
corresponding retained values. At gateway startup, old retained cover state is
cleared until the motor is observed again; no initial 50% position is advertised.

---

## Web UI: `elero_web`

Cover command bytes are optional and also round-trip through device CRUD and
backup JSON. For Raffstore variants, set `command_up` to `"0x21"` and
`command_down` to `"0x44"` when those values match the physical transmitter.
Legacy NVS records ignore the repurposed reserved bytes; old backups use the
default commands. The 64-byte NVS record and version remain unchanged.

Saved groups contain stable member addresses and survive removal of members
until the last member is removed. Group position is the average of member
positions. Any moving member makes the group moving (opening takes precedence
if members move in opposing directions); every cover must have opening and
closing durations before group position control is offered. Disabled members
make their group unavailable without deleting its saved membership.
MQTT group updates apply immediately; native cover-group CRUD requires reboot.

Members sharing a remote and command variant use one `0x44` selector-list
transmission; different remotes or command variants use their own TX paths.
Group state is published through MQTT and `group_state_changed` WebSocket
events, and is included in the initial WebSocket config.

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
- **Hub tab** — display name override, read-only radio frequency with advanced diagnostics, raw TX, **Backup & Restore**. Set `freq0`, `freq1`, and `freq2` in YAML, then rebuild and flash to change the radio frequency.
- **Packets tab** — live RF traffic.
- **Logs** — ESPHome `elero.*` logs in real time.

### WebSocket protocol

The web UI talks to the device over WebSocket at `/elero/ws`. The full protocol is documented as an AsyncAPI spec at `components/elero_web/frontend/app/asyncapi.yaml` — that file is the authoritative source.

| Endpoint | Description |
|---|---|
| `/` | Redirect to `/elero` |
| `/elero` | Web UI (HTML, served gzipped) |
| `/elero/ws` | WebSocket for real-time communication |

Server → client events include `config`, `rf`, `log`, `device_upserted`, `device_removed`, `state_changed`, `group_upserted`, `group_removed`, `group_state_changed`, `hub_config`, `config_snapshot`, `import_result`, `error`. Client → server messages include `cmd`, `raw`, `upsert_device`, `remove_device`, `upsert_group`, `remove_group`, `group_cmd`, `set_hub_config`, `export_config`, `import_config`, `restart`.

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
