# State Reporting: RF to Home Assistant

How device state flows from RF packets through the firmware to Home Assistant entities. Covers both operating modes (Native API + NVS, MQTT) and the unified snapshot layer that keeps them consistent.

> **Mode legend used below:**
> - **Native Mode** = `elero_nvs:` adapter — `NvsAdapter` instantiates `EspCoverShell` / `EspLightShell` at boot from NVS-restored slots; entities are surfaced via the ESPHome native API.
> - **MQTT Mode** = `elero_mqtt:` adapter — `MqttAdapter` publishes HA discovery + state topics.
>
> Both modes always have NVS persistence enabled (RFC-002). Devices are added at runtime through the web UI or restored from a [JSON backup](BACKUP-RESTORE.md).

---

## Home Assistant Entities Per Device

### Cover

| Data | HA Type | Native Mode | MQTT Mode | Parity |
|------|---------|-------------|-----------|--------|
| Position | `cover` | `cover::Cover.position` (0.0–1.0) | `/position` (0–100) | **Consistent** — same snapshot, different scale |
| Operation | `cover` | `COVER_OPERATION_*` enum | `/state` (opening/closing/open/closed/stopped) | **Consistent** — native maps enum, MQTT uses `ha_state` string |
| Device class | `cover` | ESPHome traits | Discovery `device_class` + `/attributes` | **Consistent** |
| Tilt | `cover` | `cover.tilt` (0.0/1.0) | `/tilt_state` (0/100) | **Consistent** — both binary via snapshot |
| RSSI | `sensor` | Not surfaced as an HA entity | Discovery `sensor/{id}_rssi` | **Gap** — see "Diagnostic sensors" below |
| Blind State | `text_sensor` | Not surfaced as an HA entity | Discovery `sensor/{id}_state` | **Gap** |
| Problem | `binary_sensor` | Not surfaced as an HA entity | Discovery `binary_sensor/{id}_problem` | **Gap** |
| Command Source | `text_sensor` | Not surfaced as an HA entity | `/attributes` JSON | **Gap** |
| Problem Type | `text_sensor` | Not surfaced as an HA entity | `/attributes` JSON | **Gap** |

**Diagnostic sensors in native mode (post RFC-002).** With YAML-defined devices removed, the auto-sensor codegen path in the deleted `components/elero/cover/__init__.py` is gone too. `NvsAdapter` currently only registers the cover entity itself — the per-device RSSI / Status / Problem / Command Source / Problem Type sensors are not created. The data is still computed in the snapshot layer and is visible via the web UI's Devices tab (and via MQTT when `elero_mqtt:` is configured); only the ESPHome native-API surface is missing them. Restoring parity requires teaching `NvsAdapter` to register sensors per slot at boot — tracked as a follow-up.

### Light

| Data | HA Type | Native Mode | MQTT Mode | Parity |
|------|---------|-------------|-----------|--------|
| On/Off | `light` | `light::LightState.is_on()` | `{"state":"ON"/"OFF"}` | **Consistent** |
| Brightness | `light` | `light::LightState.brightness` (0.0–1.0) | `{"brightness": 0–100}` | **Consistent** — same snapshot, different scale |
| RSSI | `sensor` | Not surfaced as an HA entity | Discovery `sensor/{id}_rssi` | **Gap** — see "Diagnostic sensors" above |
| Status | `text_sensor` | Not surfaced as an HA entity | Discovery `sensor/{id}_state` | **Gap** |
| Problem | `binary_sensor` | Not surfaced as an HA entity | Discovery `binary_sensor/{id}_problem` + `/problem` | **Gap** |
| Command Source | — | Not surfaced as an HA entity | `/attributes` JSON | **Gap** |
| Problem Type | — | Not surfaced as an HA entity | `/attributes` JSON | **Gap** |

### Remote

| Data | HA Type | Native Mode | MQTT Mode | Parity |
|------|---------|-------------|-----------|--------|
| RSSI | `sensor` | Not surfaced as an HA entity | Discovery `sensor/{id}` | **Gap** — registry tracks remotes in both modes; only MqttAdapter publishes them |
| Attributes | json_attributes | — | address, last_command, last_target, last_channel | **Gap** — same |

Remotes are auto-discovered from observed RF command packets in both modes. Auto-discovered entries stay ephemeral (`updated_at == 0`) and are excluded from MQTT discovery topics and from backup snapshots until you click **Save** in the web UI. Native mode doesn't surface remotes as HA entities; the data is visible only via the web UI's Devices tab.

---

## State Snapshot Layer

Snapshots are ephemeral structs computed from `(Device, now)`. The **registry** computes them once, diffs against a per-device `Published` cache, and only notifies adapters when something actually changed — passing a `uint16_t changes` bitmask (`state_change::` flags). Adapters never compute snapshots themselves; they read pre-computed values from `dev.published`.

**Published cache** lives on `CoverDevice::Published` / `LightDevice::Published` (in `device.h`). Sentinel defaults (`position_pct{-1}`, `rssi_rounded{-999}`, `ha_state{nullptr}`) guarantee a non-zero diff on the first publish after device registration.

**Change flags** (`state_snapshot.h`): `POSITION`, `HA_STATE`, `OPERATION`, `TILT`, `PROBLEM`, `RSSI`, `STATE_STRING`, `COMMAND_SOURCE`, `BRIGHTNESS`, `ALL` (0xFFFF for reconnect/initial).

`problem_type` is always a valid string (`PROBLEM_TYPE_NONE` when no problem) — callers never null-check.

### CoverStateSnapshot

```
Source: components/elero/state_snapshot.h
Computed by: compute_cover_snapshot(const Device &dev, uint32_t now)
```

| Field | Type | Derived From | Used By Native | Used By MQTT | Used By WebSocket |
|-------|------|-------------|----------------|--------------|-------------------|
| `position` | `float` | `cover_sm::position(state, now, ctx)` | `cover.position` | `/position` topic | `config` event |
| `ha_state` | `const char*` | `ha_cover_state_str(op, pos)` | — (uses `operation`) | `/state` topic | `config` event |
| `operation` | `cover_sm::Operation` | `cover_sm::operation(state)` | `current_operation` enum | — (uses `ha_state`) | — |
| `tilted` | `bool` | `CoverDevice::tilted` | `cover.tilt` | `/attributes` JSON | `config` event |
| `is_problem` | `bool` | `is_problem_state(rf.last_state_raw)` | hub sensor map | `/problem` topic | `config` event |
| `problem_type` | `const char*` | `problem_type_str()` or `PROBLEM_TYPE_NONE` | shell text_sensor | `/attributes` JSON | — |
| `rssi` | `float` | `rf.last_rssi` | hub sensor map | `/rssi` topic | `config` event |
| `state_string` | `const char*` | `elero_state_to_string(rf.last_state_raw)` | hub sensor map | `/blind_state` topic | `config` event |
| `command_source` | `const char*` | `command_source_str(cover.last_command_source)` | shell text_sensor | `/attributes` JSON | `config` event |
| `last_seen_ms` | `uint32_t` | `rf.last_seen_ms` | — | `/attributes` JSON | `config` event |
| `device_class` | `const char*` | `ha_cover_class_str(config.ha_device_class)` | ESPHome traits | discovery + `/attributes` | `config` event |

### LightStateSnapshot

```
Source: components/elero/state_snapshot.h
Computed by: compute_light_snapshot(const Device &dev, uint32_t now)
```

| Field | Type | Derived From | Used By Native | Used By MQTT | Used By WebSocket |
|-------|------|-------------|----------------|--------------|-------------------|
| `is_on` | `bool` | `light_sm::is_on(state)` | `LightState.is_on()` | `{"state":"ON"/"OFF"}` | `config` event |
| `brightness` | `float` | `light_sm::brightness(state, now, ctx)` | `LightState.brightness` | `{"brightness": N}` | `config` event |
| `is_problem` | `bool` | `is_problem_state(rf.last_state_raw)` | — | `/problem` topic | `config` event |
| `problem_type` | `const char*` | `problem_type_str()` or `PROBLEM_TYPE_NONE` | — | `/attributes` JSON | — |
| `rssi` | `float` | `rf.last_rssi` | hub sensor map | `/rssi` topic | `config` event |
| `state_string` | `const char*` | `elero_state_to_string(rf.last_state_raw)` | hub sensor map | `/light_state` topic | `config` event |
| `command_source` | `const char*` | `command_source_str(light.last_command_source)` | — | `/attributes` JSON | — |
| `last_seen_ms` | `uint32_t` | `rf.last_seen_ms` | — | `/attributes` JSON | `config` event |

### ha_state mapping

The `ha_state` string maps operation and position to what Home Assistant expects:

| Operation | Position | ha_state |
|-----------|----------|----------|
| OPENING | any | `"opening"` |
| CLOSING | any | `"closing"` |
| IDLE | `POSITION_OPEN` (1.0) | `"open"` |
| IDLE | `POSITION_CLOSED` (0.0) | `"closed"` |
| IDLE | intermediate | `"stopped"` |

### Problem states

RF state bytes that trigger `is_problem = true`:

| RF Byte | Constant | problem_type |
|---------|----------|-------------|
| 0x05 | `BLOCKING` | `"blocking"` |
| 0x06 | `OVERHEATED` | `"overheated"` |
| 0x07 | `TIMEOUT` | `"timeout"` |

All other RF state bytes → `is_problem = false`, `problem_type = PROBLEM_TYPE_NONE` (`"none"`).

---

## Parity Summary

### What's consistent

The cover/light primary entity exposes the same derived values in both modes — every adapter reads the same snapshot computed by the registry:

| Data | Source | Why identical |
|------|--------|---------------|
| Cover position | `cover_sm::position()` | Both call `compute_cover_snapshot()` |
| Cover operation/ha_state | `cover_sm::operation()` | Same snapshot |
| Cover tilt | `CoverDevice::tilted` | Same flag, set in `dispatch_status_()` |
| Cover device_class | `NvsDeviceConfig::ha_device_class` | Same config field |
| Light on/off | `light_sm::is_on()` | Both call `compute_light_snapshot()` |
| Light brightness | `light_sm::brightness()` | Same snapshot |

Diagnostic data (RSSI, raw blind state, problem flag, command source) is computed identically in both modes via the snapshot layer. The difference is whether it's *surfaced* to HA — see "Known gaps" below.

### Known gaps

| Gap | Reason | Severity |
|-----|--------|----------|
| Native mode doesn't surface per-device diagnostic sensors (RSSI / Status / Problem / Command Source / Problem Type) | RFC-002 removed the YAML cover/light platforms, which is where the auto-sensor codegen used to live. `NvsAdapter` only registers the cover/light entity itself today. | Medium — data is computed and visible via the web UI; needs a follow-up to teach `NvsAdapter` to register sensors per slot |
| Remotes not exposed as HA entities in native mode | Same root cause as above (`NvsAdapter` only handles cover/light) | Low — visible in the web UI |
| Native diagnostic data would be separate entities, MQTT uses json_attributes | ESPHome native API has no json_attributes equivalent | Cosmetic once the gap above is closed |
| `last_seen_ms` not exposed as native entity | Raw `millis()` (uptime-relative) isn't useful as an HA sensor without NTP | Intentional — available in MQTT attributes for clients that want it |

---

## Dataflow: RF Packet to Home Assistant

```
CC1101 (868 MHz transceiver)
  │
  │  GDO0 interrupt (packet received)
  ▼
Elero::interrupt()                         ← ISR (any core)
  │  Sets atomic flag: received_ = true
  │  vTaskNotifyGiveFromISR → wakes RF task
  ▼
rf_task_func_ (Core 0)                    ← Dedicated FreeRTOS task
  │  Wakes on notification
  │  Drains FIFO from CC1101 over SPI
  │  Decode + AES-128 decrypt + CRC check
  │  Posts RfPacketInfo to rx_queue
  ▼
Elero::loop() (Core 1)                    ← ESPHome main loop
  │  Drains rx_queue (non-blocking)
  ▼
Elero::dispatch_packet(pkt)                ← Core 1, no SPI
  │
  │  RfPacketInfo (src, dst, channel, type, state, command, rssi, raw)
  │
  ▼
DeviceRegistry::on_rf_packet()
  │
  │  1. notify_rf_packet_(pkt)
  │     → all adapters get raw RF
  │
  │  2. Classify packet:
  │     Status (0xCA) → find(src)
  │     Command (0x6A) → find(dst)
  │
  │  3. Update Device:
  │     rf.last_seen_ms = now
  │     rf.last_rssi = rssi
  │     rf.last_state_raw = state
  │
  │  4. dispatch_status_()
  │     → cover_sm / light_sm
  │     → update tilted flag
  │     → update last_command_source
  │     → changed?
  │
  │  5. notify_state_changed_(dev, now)
  │     → compute snapshot
  │     → diff_and_update_*(snap, dev.published)
  │     → if changes == 0: return (no adapter calls)
  │     → set dev.last_changes + dev.last_notify_ms
  │     → on_state_changed(dev, changes) for all adapters
  │
  ├──────────────────┬───────────────────┬────────────────────┐
  │                  │                   │                    │
  ▼                  ▼                   ▼                    ▼
EspCoverShell     MqttAdapter         EleroWebServer       (MatterAdapter)
EspLightShell     (MQTT mode)         (all modes)          (future)
(Native/NVS)        │                   │
  │                  │                   │
  │ loop() detects   │ on_state_changed  │ on_state_changed
  │ last_notify_ms   │ (dev, changes)    │ (dev, changes)
  │ reads last_changes                   │
  ▼                  ▼                   ▼
reads              reads              build state JSON
dev.published      dev.published      from dev.published
  │                  │                   │
  │ publishes only   │ publishes only    ▼
  │ changed fields   │ changed topics  ws_broadcast("state", json)
  │                  │                  → Browser
  ▼                  ▼
┌──────────────┐  ┌──────────────────────────────┐
│ cover/light  │  │ MQTT topics (only changed):   │
│  .position   │  │  /state (if HA_STATE)         │
│  .tilt       │  │  /position (if POSITION)      │
│  .operation  │  │  /rssi (if RSSI)              │
│  .publish()  │  │  /blind_state (if STATE_STR)  │
│  (if POS|OP| │  │  /problem (if PROBLEM)        │
│   HA|TILT)   │  │  /attributes (if CMD_SRC|...) │
│              │  │  /tilt_state (if TILT)         │
│  diagnostic  │  └──────────────────────────────┘
│  sensors not │           │
│  yet wired   │           ▼
│  in NVS mode │      Home Assistant
│  (see Gaps)  │      (MQTT entities)
└──────────────┘
       │
       ▼
  Home Assistant
  (native entities)
```

### Key design properties

1. **Registry owns all publish decisions.** `notify_state_changed_()` computes the snapshot once, diffs it against the device's `Published` cache (`diff_and_update_cover/light`), and only calls adapters when changes != 0. Adapters never compute snapshots — they read pre-computed values from `dev.published` and use the `changes` bitmask to publish only changed fields.

2. **Per-field change tracking.** The `uint16_t changes` bitmask (`state_change::POSITION`, `HA_STATE`, `RSSI`, etc.) tells each adapter exactly which fields changed. During a 25s cover movement: ~25 position-only ticks (1 MQTT topic each) + ~2-3 RF state changes (~7 topics each). String comparisons in the diff use pointer identity (all strings are compile-time literals).

3. **Snapshots are ephemeral, Published cache is persistent per-device.** Snapshots are computed from `(Device, now)` on demand. The `Published` cache on `CoverDevice`/`LightDevice` stores quantized last-published values (int position_pct, pointer-stable strings). After reboot, FSM starts at `Idle{POSITION_CLOSED}` and Published defaults guarantee a full initial publish.

4. **No lateral adapter coupling.** Each adapter reads `Device.published` independently. MqttAdapter doesn't know about EspCoverShell. EleroWebServer doesn't know about MqttAdapter.

5. **Single publish path per device.** Every per-device update now flows through the registry → adapter pipeline. The earlier hub-side "address-keyed sensor maps" path (where `Elero::dispatch_packet()` directly published to per-blind RSSI / status / problem sensors) was tied to the YAML cover/light setup that wired those sensors to the hub at codegen. With YAML-defined devices removed, that path is gone and per-device diagnostic sensors are not yet recreated by `NvsAdapter` — see "Known gaps".

6. **MQTT topics are centralized.** Topic suffixes (`mqtt_topic::STATE`, etc.), HA discovery component types (`ha_discovery::COVER`, etc.), and topic construction (`MqttContext::topic()`, `object_id()`, `publish()`) are defined once in `mqtt_context.h`. Zero string concatenation at adapter call sites.

7. **WebSocket is raw RF, not snapshots.** The web server forwards raw `RfPacketInfo` to the browser. The browser derives all state client-side. The `config` event on connect sends current device state using snapshots.

8. **MQTT reconnect forces full republish.** `republish_all_()` resets each device's `Published` cache to defaults and calls `on_state_changed(dev, state_change::ALL)`, guaranteeing all topics are republished to the fresh broker.

### Timing

| Event | Latency |
|-------|---------|
| RF packet → interrupt | <1 ms (hardware) |
| interrupt → RF task pickup | <1 ms (Core 0 dedicated task) |
| RF task → dispatch_packet | queue transit, typically <1 loop tick |
| dispatch_packet → registry dispatch | synchronous (same loop tick) |
| registry → adapter notification | synchronous |
| adapter → HA publish | synchronous (native) or async (MQTT) |
| Movement position updates | throttled to 1/sec (`PUBLISH_THROTTLE_MS`) |
| Poll interval (idle) | 5 min (`DEFAULT_POLL_INTERVAL_MS`) |
| Poll interval (moving) | 2 sec (`POLL_INTERVAL_MOVING`) |

---

## MQTT Topic Reference

All topics are constructed via `MqttContext::topic(DeviceType, addr, mqtt_topic::*)` and `MqttContext::object_id(DeviceType, addr, suffix)`. Constants live in `mqtt_context.h`.

### Cover topics

| Topic | Published when | Change flag | Content |
|-------|---------------|-------------|---------|
| `{prefix}/cover/{addr}/state` | HA state changes | `HA_STATE` | `"opening"` / `"closing"` / `"open"` / `"closed"` / `"stopped"` |
| `{prefix}/cover/{addr}/position` | Position changes (1s during movement) | `POSITION` | `0`–`100` |
| `{prefix}/cover/{addr}/rssi` | RSSI changes | `RSSI` | dBm (integer-rounded) |
| `{prefix}/cover/{addr}/blind_state` | RF state byte changes | `STATE_STRING` | Raw RF state name (`"top"`, `"moving_up"`, etc.) |
| `{prefix}/cover/{addr}/problem` | Problem state changes | `PROBLEM` | `"ON"` / `"OFF"` |
| `{prefix}/cover/{addr}/attributes` | Command source, problem, or tilt changes | `COMMAND_SOURCE\|PROBLEM\|TILT` | JSON: `{command_source, tilted, device_class, problem_type}` |
| `{prefix}/cover/{addr}/tilt_state` | Tilt changes (if tilt supported) | `TILT` | `"0"` / `"100"` |
| `{prefix}/cover/{addr}/set` | Subscribed | — | `"open"` / `"close"` / `"stop"` |
| `{prefix}/cover/{addr}/tilt` | Subscribed (if tilt) | — | Any payload triggers tilt |

### Light topics

| Topic | Published when | Change flag | Content |
|-------|---------------|-------------|---------|
| `{prefix}/light/{addr}/state` | On/off or brightness changes | `BRIGHTNESS` | JSON: `{"state":"ON"/"OFF", "brightness": 0–100}` |
| `{prefix}/light/{addr}/rssi` | RSSI changes | `RSSI` | dBm (integer-rounded) |
| `{prefix}/light/{addr}/light_state` | RF state byte changes | `STATE_STRING` | Raw RF state name |
| `{prefix}/light/{addr}/problem` | Problem state changes | `PROBLEM` | `"ON"` / `"OFF"` |
| `{prefix}/light/{addr}/attributes` | Command source or problem changes | `COMMAND_SOURCE\|PROBLEM` | JSON: `{command_source, problem_type}` |
| `{prefix}/light/{addr}/set` | Subscribed | — | JSON `{"state":"ON"}` or string `"on"`/`"off"` |

### Remote topics

| Topic | Published | Content |
|-------|-----------|---------|
| `{prefix}/remote/{addr}/state` | On remote activity | JSON: `{rssi, address, title, last_seen, last_channel, last_command, last_target}` |

### Discovery topics

Built via `MqttContext::publish_discovery(ha_discovery::*, object_id, payload)`:

```
{discovery_prefix}/{ha_component}/{object_id}/config
```

On device removal, empty retained payloads are published to remove all related discovery topics.
