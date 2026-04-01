# State Reporting: RF to Home Assistant

How device state flows from RF packets through the firmware to Home Assistant entities. Covers all three operating modes (Native, MQTT, Native+NVS) and the unified snapshot layer that ensures consistency.

---

## Home Assistant Entities Per Device

### Cover

| Data | HA Type | Native Mode | MQTT Mode | Parity |
|------|---------|-------------|-----------|--------|
| Position | `cover` | `cover::Cover.position` (0.0–1.0) | `/position` (0–100) | **Consistent** — same snapshot, different scale |
| Operation | `cover` | `COVER_OPERATION_*` enum | `/state` (opening/closing/open/closed/stopped) | **Consistent** — native maps enum, MQTT uses `ha_state` string |
| Device class | `cover` | ESPHome traits | Discovery `device_class` + `/attributes` | **Consistent** |
| Tilt | `cover` | `cover.tilt` (0.0/1.0) | `/tilt_state` (0/100) | **Consistent** — both binary via snapshot |
| RSSI | `sensor` | Auto-sensor `{name} RSSI` | Discovery `sensor/{id}_rssi` | **Consistent** |
| Blind State | `text_sensor` | Auto-sensor `{name} Status` | Discovery `sensor/{id}_state` | **Consistent** |
| Problem | `binary_sensor` | Auto-sensor `{name} Problem` | Discovery `binary_sensor/{id}_problem` | **Consistent** |
| Command Source | `text_sensor` | Auto-sensor `{name} Command Source` | `/attributes` JSON | **Consistent** — native as entity, MQTT as attribute |
| Problem Type | `text_sensor` | Auto-sensor `{name} Problem Type` | `/attributes` JSON | **Consistent** — native as entity, MQTT as attribute |

**`auto_sensors` (default `true`)** automatically creates all diagnostic sensors alongside each cover entity. Set `auto_sensors: false` to disable.

**Implementation note:** RSSI, Status, and Problem sensors are published via the hub's address-keyed sensor maps (direct path from `dispatch_packet()` using `is_problem_state()` from `state_snapshot.h`). Command Source and Problem Type are published from `EspCoverShell::sync_and_publish_()` using snapshot data. Both paths derive from the same underlying `Device` state.

### Light

| Data | HA Type | Native Mode | MQTT Mode | Parity |
|------|---------|-------------|-----------|--------|
| On/Off | `light` | `light::LightState.is_on()` | `{"state":"ON"/"OFF"}` | **Consistent** |
| Brightness | `light` | `light::LightState.brightness` (0.0–1.0) | `{"brightness": 0–100}` | **Consistent** — same snapshot, different scale |
| RSSI | `sensor` | Auto-sensor `{name} RSSI` | Discovery `sensor/{id}_rssi` | **Consistent** |
| Status | `text_sensor` | Auto-sensor `{name} Status` | Discovery `sensor/{id}_state` | **Consistent** |
| Problem | `binary_sensor` | Not exposed | Discovery `binary_sensor/{id}_problem` + `/problem` | **Gap** — native lights don't auto-generate problem sensor |
| Command Source | — | Not exposed | `/attributes` JSON | **Gap** — tracked in `LightDevice` but not surfaced natively |
| Problem Type | — | Not exposed | `/attributes` JSON | **Gap** — same |

### Remote (MQTT mode only)

| Data | HA Type | Native Mode | MQTT Mode | Parity |
|------|---------|-------------|-----------|--------|
| RSSI | `sensor` | N/A (remotes not tracked in native) | Discovery `sensor/{id}` | N/A |
| Attributes | json_attributes | N/A | address, last_command, last_target, last_channel | N/A |

Remotes are only auto-discovered in NVS-enabled modes (MQTT, Native+NVS). Native mode doesn't track remotes.

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

All data that both modes expose produces identical values from the same source:

| Data | Source | Why identical |
|------|--------|---------------|
| Cover position | `cover_sm::position()` | Both call `compute_cover_snapshot()` |
| Cover operation/ha_state | `cover_sm::operation()` | Same snapshot |
| Cover tilt | `CoverDevice::tilted` | Same flag, set in `dispatch_status_()` |
| Cover device_class | `NvsDeviceConfig::ha_device_class` | Same config field |
| Cover RSSI | `rf.last_rssi` | Hub sensor map (native) / snapshot (MQTT) — same value |
| Cover blind state | `elero_state_to_string()` | Hub sensor map (native) / snapshot (MQTT) — same function |
| Cover problem | `is_problem_state()` | Hub sensor map calls it directly, MQTT reads snapshot — same function |
| Cover command_source | `CoverDevice::last_command_source` | Shell text_sensor (native) / attributes JSON (MQTT) |
| Cover problem_type | `problem_type_str()` | Shell text_sensor (native) / attributes JSON (MQTT) |
| Light on/off | `light_sm::is_on()` | Both call `compute_light_snapshot()` |
| Light brightness | `light_sm::brightness()` | Same snapshot |
| Light RSSI | `rf.last_rssi` | Hub sensor map (native) / snapshot (MQTT) |
| Light status | `elero_state_to_string()` | Hub sensor map (native) / snapshot (MQTT) |

### Known gaps

| Gap | Reason | Severity |
|-----|--------|----------|
| Light problem/command_source/problem_type not in native mode | No auto_sensors for these on `EspLightShell` yet | Low — MQTT has them, native doesn't surface them as entities |
| Native diagnostic data as separate entities, MQTT uses json_attributes | ESPHome native API has no json_attributes equivalent | Cosmetic — same data, different HA presentation |
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
  ├──────────────────────────────────┐
  │                                  │
  │  Registry dispatch               │  Direct sensor publish (native mode)
  ▼                                  ▼
DeviceRegistry::on_rf_packet()       Elero::dispatch_packet() (continued)
  │                                    │
  │  1. notify_rf_packet_(pkt)         ├─ rssi_sensor->publish_state(rssi)
  │     → all adapters get raw RF      ├─ text_sensor->publish_state(state_string)
  │                                    └─ problem_sensor->publish_state(is_problem_state(...))
  │  2. Classify packet:                       │
  │     Status (0xCA) → find(src)              │  Hub's address-keyed sensor maps
  │     Command (0x6A) → find(dst)             │  (USE_SENSOR / USE_TEXT_SENSOR /
  │                                            │   USE_BINARY_SENSOR guards)
  │  3. Update Device:                         ▼
  │     rf.last_seen_ms = now          ┌────────────────────────┐
  │     rf.last_rssi = rssi            │  Home Assistant         │
  │     rf.last_state_raw = state      │  RSSI sensor            │
  │                                    │  Status text_sensor     │
  │  4. dispatch_status_()             │  Problem binary_sensor  │
  │     → cover_sm / light_sm          └────────────────────────┘
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
│ sensors:     │  └──────────────────────────────┘
│  (if RSSI)   │           │
│  (if STATE)  │           ▼
│  (if PROBLEM)│      Home Assistant
│  etc.        │      (MQTT entities)
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

5. **Two publish paths for native mode.** RSSI, text_sensor, and problem binary_sensor are published directly from `dispatch_packet()` via address-keyed sensor maps on the hub — these call `is_problem_state()` from `state_snapshot.h` (single derivation point). Cover position/operation + command_source/problem_type go through the registry → shell path, using `last_changes` bitmask for selective publish.

6. **MQTT topics are centralized.** Topic suffixes (`mqtt_topic::STATE`, etc.), HA discovery component types (`ha_discovery::COVER`, etc.), and topic construction (`MqttContext::topic()`, `object_id()`, `publish()`) are defined once in `mqtt_context.h`. Zero string concatenation at adapter call sites.

7. **WebSocket is raw RF, not snapshots.** The web server forwards raw `RfPacketInfo` to the browser. The browser derives all state client-side. The `config` event on connect sends current device state using snapshots.

8. **MQTT reconnect forces full republish.** `republish_all_()` resets each device's `Published` cache to defaults and calls `on_state_changed(dev, state_change::ALL)`, guaranteeing all topics are republished to the fresh broker.

### Timing

| Event | Latency |
|-------|---------|
| RF packet → interrupt | <1 ms (hardware) |
| interrupt → RF task pickup | <1 ms (Core 0 dedicated task) |
| RF task → dispatch_packet | queue transit, typically <1 loop tick |
| dispatch_packet → sensor publish | synchronous (same loop tick) |
| dispatch_packet → registry dispatch | synchronous |
| registry → adapter notification | synchronous |
| adapter → HA publish | synchronous (native) or async (MQTT) |
| Movement position updates | throttled to 1/sec (`PUBLISH_THROTTLE_MS`) |
| Poll interval (idle) | configurable, default 5 min |
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
