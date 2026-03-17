# ADR-0002: Architecture Rewrite -- Unified Device + Variant State Machines + Output Adapters

**Status:** In Progress
**Date:** 2026-03-16

## Problem

The codebase has 6+ entity classes across three operating modes, with overlapping state and multiple inheritance:

```
EleroBlindBase (abstract)                    EleroLightBase (abstract)
  |                                            |
  +-- EleroCover         (Native mode)         +-- EleroLight         (Native mode)
  |   cover::Cover + Component                 |   light::LightOutput + Component
  |                                            |
  +-- EleroDynamicCover  (MQTT mode)           +-- EleroDynamicLight  (MQTT mode)
  |   DynamicEntityBase                        |   DynamicEntityBase
  |                                            |
  +-- NativeNvsCover     (Native+NVS mode)     +-- NativeNvsLight     (Native+NVS mode)
      cover::Cover + DynamicEntityBase             light::LightOutput + DynamicEntityBase
```

| # | Issue | Impact |
|---|-------|--------|
| 1 | 6+ entity classes with duplicated state (CoverCore.position AND cover::Cover.position must stay in sync) | Bugs when state diverges |
| 2 | Adding a new output mode (e.g. Matter over Thread) requires 2+ new classes per device type | Linear growth in class count |
| 3 | 5 device manager implementations (IDeviceManager, NativeDeviceManager, NvsDeviceManagerBase, MqttDeviceManager, NativeNvsDeviceManager) | Fragmented CRUD/persistence logic |
| 4 | Entity lifecycle encoded as boolean flags (`active_`, `registered_`, `enabled`) | Invalid flag combinations possible at runtime |
| 5 | Multiple inheritance (e.g. `NativeNvsCover : cover::Cover, DynamicEntityBase`) | Diamond problems, unclear ownership |

## Decision

Rewrite to a 4-layer architecture where device behavior and output format are fully decoupled.

### Layer 1 -- Radio Core (kept as-is)

CC1101 SPI driver, TX/RX state machine, packet encode/decode/encrypt. No changes needed.

### Layer 2 -- Device Domain

A single `Device` struct composes all device state via `std::variant`:

```cpp
struct Device {
    NvsDeviceConfig config;           // persisted identity + RF params
    RfMeta rf;                        // last_seen, last_rssi, last_state_raw
    std::variant<CoverDevice,
                 LightDevice,
                 RemoteDevice> kind;  // type-specific state machine
};
```

Cover and light state machines use `std::variant` for states:

```cpp
// Cover states
struct Idle { float position; };        // 0.0 = closed, 1.0 = open
struct Opening { float start_pos; uint32_t started_at; };
struct Closing { float start_pos; uint32_t started_at; };
struct Stopping { float position; };

using CoverState = std::variant<Idle, Opening, Closing, Stopping>;
```

Position and brightness are **derived** from `(state, now, config)` -- never stored separately. This eliminates the dual-position sync bug.

### Layer 3 -- Device Registry

Single source of truth for all devices. Replaces all 5 device manager classes.

Responsibilities:
- CRUD operations (upsert, remove, enable/disable)
- NVS persistence (unified `"elero_device"` preference keys)
- RF packet dispatch to matching devices
- Observer notification (adapters subscribe to state changes)

### Layer 4 -- Output Adapters

Thin, stateless translators implementing an `OutputAdapter` interface:

```cpp
class OutputAdapter {
public:
    virtual ~OutputAdapter() = default;
    virtual void on_device_added(const Device &dev) = 0;
    virtual void on_device_removed(uint32_t address, DeviceType type) = 0;
    virtual void on_state_changed(const Device &dev) = 0;
};
```

Each output mode is an adapter implementation:
- `WebSocketAdapter` -- broadcasts state to web UI clients
- `MqttAdapter` -- publishes HA discovery + state topics
- `NativeApiAdapter` -- registers with ESPHome native API
- `MatterAdapter` -- future, zero changes to layers 1-3

Adding a new mode = implementing the interface. No changes to device logic, no new entity classes.

### Design principles

- **Composition over inheritance.** `Device` composes a variant, not an inheritance chain.
- **Invalid states are unrepresentable.** `std::variant` state machines prevent illegal transitions at compile time. No boolean flag soup.
- **Position/brightness are pure functions.** Derived from `(state, now, config)`, never stored as independent mutable fields.
- **Output mode is an adapter concern.** Device behavior is defined once in the state machine; adapters translate to wire format.

## New Files

| File | Purpose |
|------|---------|
| `device_type.h` | Shared `DeviceType`/`HubMode` enums (extracted from `device_manager.h`) |
| `overloaded.h` | `std::visit` helper |
| `cover_sm.h/cpp` | Cover state machine (Idle/Opening/Closing/Stopping) |
| `light_sm.h/cpp` | Light state machine (Off/On/DimmingUp/DimmingDown) |
| `poll_timer.h` | Poll timing logic |
| `device.h` | `Device`, `CoverDevice`, `LightDevice`, `RemoteDevice`, `RfMeta`, lifecycle helpers |
| `output_adapter.h` | `OutputAdapter` interface |
| `device_registry.h/cpp` | `DeviceRegistry` (CRUD, NVS, RF dispatch, observers) |

## Tests

72 new unit tests (43 cover state machine + 29 light state machine), all passing alongside 300 existing tests (372 total).

## Consequences

- Adding Matter over Thread = implementing `MatterAdapter`, zero changes to layers 1-3.
- WebSocket, MQTT, and ESPHome native API become interchangeable adapters.
- Single `Device` struct replaces 6+ entity classes -- one place for state, one place for behavior.
- NVS persistence uses unified `"elero_device"` preference keys -- existing NVS data from the old scheme requires re-discovery on first boot after upgrade.
- Net reduction in class count: 6 entity classes + 5 device managers replaced by 1 `Device` struct + 1 `DeviceRegistry` + N adapters.
