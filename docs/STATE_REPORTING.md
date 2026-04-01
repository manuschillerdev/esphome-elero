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

All output adapters compute state from the same `CoverStateSnapshot` / `LightStateSnapshot` structs. Snapshots are ephemeral — computed on demand from `(Device, now)`, never cached or persisted. `problem_type` is always a valid string (`PROBLEM_TYPE_NONE` when no problem) — callers never null-check.

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
  │  5. notify_state_changed_(dev)
  │     → all OutputAdapters
  │
  ├──────────────────┬───────────────────┬────────────────────┐
  │                  │                   │                    │
  ▼                  ▼                   ▼                    ▼
EspCoverShell     MqttAdapter         EleroWebServer       (MatterAdapter)
EspLightShell     (MQTT mode)         (all modes)          (future)
(Native/NVS)        │                   │
  │                  │                   │
  │ loop() detects   │ on_state_changed  │ on_rf_packet
  │ last_notify_ms   │                   │
  ▼                  ▼                   ▼
compute_*_         compute_*_          build_rf_json(pkt)
snapshot()         snapshot()            │
  │                  │                   ▼
  │                  │                ws_broadcast("rf", json)
  │                  │                  → Browser (derives state client-side)
  │                  │
  ▼                  ▼
┌──────────────┐  ┌──────────────────────────────┐
│ cover/light  │  │ MQTT topics:                  │
│  .position   │  │  /state (ha_state)            │
│  .tilt       │  │  /position                    │
│  .operation  │  │  /rssi                        │
│  .publish()  │  │  /blind_state or /light_state │
│              │  │  /problem                     │
│ text_sensors:│  │  /attributes (JSON)           │
│  cmd_source  │  │  /tilt_state                  │
│  problem_type│  └──────────────────────────────┘
└──────────────┘           │
       │                   ▼
       ▼              Home Assistant
  Home Assistant      (MQTT entities)
  (native entities)
```

### Key design properties

1. **Single snapshot computation.** All adapters call `compute_cover_snapshot()` / `compute_light_snapshot()`. State derivation logic exists in exactly one place. `problem_type` is always a valid string (`PROBLEM_TYPE_NONE` when no problem active) — no null checks at consumer sites.

2. **Snapshots are ephemeral.** Computed from `(Device, now)` on demand. No caching, no persistence. After reboot, FSM starts at `Idle{POSITION_CLOSED}` and the first CHECK poll (within seconds via staggered offsets) restores real state from the blind.

3. **No lateral adapter coupling.** Each adapter reads `Device` independently. MqttAdapter doesn't know about EspCoverShell. EleroWebServer doesn't know about MqttAdapter.

4. **Two publish paths for native mode.** RSSI, text_sensor, and problem binary_sensor are published directly from `dispatch_packet()` via address-keyed sensor maps on the hub — these call `is_problem_state()` from `state_snapshot.h` (single derivation point). Cover position/operation + command_source/problem_type go through the registry → shell → snapshot path.

5. **MQTT topics are centralized.** Topic suffixes (`mqtt_topic::STATE`, etc.), HA discovery component types (`ha_discovery::COVER`, etc.), and topic construction (`MqttContext::topic()`, `object_id()`, `publish()`) are defined once in `mqtt_context.h`. Zero string concatenation at adapter call sites.

6. **WebSocket is raw RF, not snapshots.** The web server forwards raw `RfPacketInfo` to the browser. The browser derives all state client-side. The `config` event on connect sends current device state using snapshots.

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

| Topic | Published | Content |
|-------|-----------|---------|
| `{prefix}/cover/{addr}/state` | Every state change | `"opening"` / `"closing"` / `"open"` / `"closed"` / `"stopped"` |
| `{prefix}/cover/{addr}/position` | Every state change | `0`–`100` |
| `{prefix}/cover/{addr}/rssi` | Every state change | dBm (rounded) |
| `{prefix}/cover/{addr}/blind_state` | Every state change | Raw RF state name (`"top"`, `"moving_up"`, etc.) |
| `{prefix}/cover/{addr}/problem` | Every state change | `"ON"` / `"OFF"` |
| `{prefix}/cover/{addr}/attributes` | Every state change | JSON: `{last_seen, command_source, tilted, device_class, problem_type}` |
| `{prefix}/cover/{addr}/tilt_state` | Every state change (if tilt) | `"0"` / `"100"` |
| `{prefix}/cover/{addr}/set` | Subscribed | `"open"` / `"close"` / `"stop"` |
| `{prefix}/cover/{addr}/tilt` | Subscribed (if tilt) | Any payload triggers tilt |

### Light topics

| Topic | Published | Content |
|-------|-----------|---------|
| `{prefix}/light/{addr}/state` | Every state change | JSON: `{"state":"ON"/"OFF", "brightness": 0–100}` |
| `{prefix}/light/{addr}/rssi` | Every state change | dBm (rounded) |
| `{prefix}/light/{addr}/light_state` | Every state change | Raw RF state name |
| `{prefix}/light/{addr}/problem` | Every state change | `"ON"` / `"OFF"` |
| `{prefix}/light/{addr}/attributes` | Every state change | JSON: `{last_seen, command_source, problem_type}` |
| `{prefix}/light/{addr}/set` | Subscribed | JSON `{"state":"ON"}` or string `"on"`/`"off"` |

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
