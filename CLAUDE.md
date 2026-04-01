# CLAUDE.md — esphome-elero

## Project Overview

`esphome-elero` is a custom **ESPHome external component** that enables Home Assistant to control Elero wireless motor blinds (rollers, shutters, awnings) via a **CC1101 868 MHz (or 433 MHz) RF transceiver** connected to an ESP32 over SPI.

```yaml
external_components:
  - source: github://manuschillerdev/esphome-elero
```

**Available skills (USE THEM!):**

| Skill | When to use |
|-------|-------------|
| `/elero-protocol` | **Always** when modifying CC1101 TX/RX code, packet encoding/decoding, encryption |
| `/sx1262-driver` | **Always** when modifying SX1262 driver code, debugging TX/RX, or radio config |
| `/modern-cpp` | **Always** when writing or reviewing C++ code |
| `/esp32-development` | **Always** when writing C++ code (ISRs, memory, FreeRTOS, SPI) |

> **IMPORTANT:** Before writing C++ code, invoke `/modern-cpp` and `/esp32-development` skills.
> Before touching RF protocol code (elero.cpp TX/RX, packet handling), invoke `/elero-protocol`.
> Before touching SX1262 driver code, invoke `/sx1262-driver`.

---

## Compatibility Matrix

**Supported targets — ESP32 only:**

| Framework | Status | Notes |
|-----------|--------|-------|
| **ESP-IDF** | Supported | Primary target, recommended |
| **Arduino** | Supported | Legacy support via ESPHome |

**NOT supported (do not add support for):** ESP8266, RP2040, LibreTiny, Host (native).

The codebase uses Mongoose for HTTP/WebSocket specifically because it provides a unified API across ESP-IDF and Arduino frameworks. Do not introduce framework-specific code paths.

---

## Architecture

**Detailed diagrams and data flows are in `docs/`** — see `docs/flows/STATE_MACHINES.md` (dual-core RF task, RX/TX paths, registry loop), `docs/STATE_MACHINES.md` (TX state machine, CommandSender), and `docs/STATE_REPORTING.md` (RF-to-HA entity flow, snapshots).

### Core Design Principles

1. **Minimal base state, maximal derivation.** Store smallest possible state (RF config, FSM state, timestamps), derive everything else — position, brightness, entity status, discovery lists — from `(state, now, config)`.

2. **One path, no redundant implementations.** One `Device` struct, one `DeviceRegistry`, one set of state machines. Output adapters are thin translators — they never duplicate logic.

3. **Route and fail early.** RF packets decoded on Core 0, dispatched on Core 1 via queues. Device type resolved on upsert. Invalid configs rejected before NVS write.

4. **Solve the state-space explosion.** Variant-based `Device` + `OutputAdapter` observer pattern keeps the combinatorial surface flat: adding a new mode or device type is additive, not multiplicative.

5. **Clear, unidirectional data flow.** Hardware → RF Task (Core 0) → FreeRTOS Queues → Main Loop (Core 1) → Registry → Adapters. Commands flow reverse via `tx_queue`. No shared mutable state between cores.

### Three Operating Modes

All modes use the same `Device` struct and `DeviceRegistry` — only output adapters differ.

| | Native Mode | MQTT Mode | Native+NVS Mode |
|---|---|---|---|
| **Devices defined in** | YAML (compile-time) | NVS (runtime via CRUD API) | NVS (runtime, reboot to apply) |
| **Home Assistant API** | ESPHome native API | MQTT HA discovery | ESPHome native API |
| **Component** | `elero:` only | `elero:` + `elero_mqtt:` | `elero:` + `elero_nvs:` |
| **Output adapters** | `EspCoverShell`, `EspLightShell` | `MqttAdapter` | `EspCoverShell`, `EspLightShell` |

### Critical Architectural Guardrails

**EleroWebServer is a STATELESS pipe.** It does NOT:
- Track which blinds are discovered (client derives from RF packets)
- Store blind states (client derives from RF status events)
- Generate YAML (client does this)
- Know anything about "discovery mode" (client filters RF packets)

**Client derives EVERYTHING** from `config` + `rf` + `log` events over WebSocket. Discovery = RF addresses NOT in config. Position = derived from movement timing. YAML generation = client-side.

**DeviceRegistry is the single source of truth.** All adapters (native shell, MQTT, WebSocket) route commands through `DeviceRegistry::command_cover()` / `command_light()` — no duplicated FSM logic.

### Observer Pattern + Centralized Publish Decisions

The registry owns **all** publish decisions. It computes snapshots, diffs against a per-device `Published` cache, and only notifies adapters when something actually changed — passing a `uint16_t changes` bitmask so adapters know exactly which fields to publish.

```cpp
class OutputAdapter {
 public:
  virtual void setup(DeviceRegistry &registry) = 0;
  virtual void loop() = 0;
  virtual void on_device_added(const Device &dev) = 0;
  virtual void on_device_removed(const Device &dev) = 0;
  virtual void on_state_changed(const Device &dev, uint16_t changes) = 0;
  virtual void on_config_changed(const Device &dev) {}
  virtual void on_rf_packet(const RfPacketInfo &pkt) {}
};
```

**Change flags** (`state_change::` namespace): `POSITION`, `HA_STATE`, `OPERATION`, `TILT`, `PROBLEM`, `RSSI`, `STATE_STRING`, `COMMAND_SOURCE`, `BRIGHTNESS`, `ALL`.

Adapters are pure formatters — they read `dev.published` fields and format for their wire protocol. Zero adapter-side caching or snapshot computation.

---

## Naming Conventions

| Item | Convention | Example |
|---|---|---|
| C++ classes | PascalCase | `DeviceRegistry`, `EspCoverShell`, `MqttAdapter` |
| C++ namespaces | lowercase | `esphome::elero` |
| C++ constants | `UPPER_SNAKE_CASE` with `ELERO_` prefix | `ELERO_COMMAND_COVER_UP` |
| C++ private members | trailing underscore | `gdo0_pin_`, `scan_mode_` |
| Python config keys | `snake_case` string constants | `"blind_address"`, `"gdo0_pin"` |
| YAML keys | `snake_case` | `blind_address`, `open_duration` |

---

## ESPHome Platform Conventions

When adding a new platform sub-component:

1. Create `components/elero/<platform>/__init__.py` with `DEPENDENCIES = ["elero"]`, a `CONFIG_SCHEMA`, and `async def to_code(config)`
2. Create the corresponding `.h`/`.cpp` files in the same directory
3. Add a `register_<platform>()` method to `Elero` if the hub needs to dispatch data to it

The `CONF_ELERO_ID` pattern resolves the parent hub:
```python
cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
```

---

## Configuration & Testing

**Configuration reference:** `docs/CONFIGURATION.md` (German, complete parameter tables for all platforms and modes). See also `example.yaml`.

**Unit tests:**
```bash
cd tests/unit && cmake -B build && cmake --build build && ctest --test-dir build
```

**Compile tests:**
```bash
uv run esphome compile tests/test.esp32-minimal.yaml   # Native mode
uv run esphome compile tests/test.esp32-mqtt.yaml       # MQTT mode
uv run esphome compile tests/test.esp32-nvs.yaml        # Native+NVS mode
```

---

## Common Pitfalls

- **Wrong frequency**: Most European Elero motors use 868.35 MHz (`freq0=0x7a`). Some use 868.95 MHz (`freq0=0xc0`). If discovery finds nothing, try the alternate frequency.
- **SPI conflicts**: The CC1101 CS pin must not be shared with any other SPI device.
- **`web_server:` vs `web_server_base:`**: Use `web_server_base:` (auto-loaded by `elero_web`) to serve only `/elero`. Adding `web_server:` re-enables the default ESPHome entity UI.
- **Position tracking**: Leave `open_duration` and `close_duration` at `0s` if you only need open/close — incorrect durations cause wrong position estimates.
- **Poll interval `never`**: For blinds that reliably push state updates (avoids unnecessary RF traffic). Internally maps to `uint32_t` max.

---

## Contributing

- Follow the existing naming conventions for C++ and Python code.
- Keep output adapters thin — business logic belongs in state machines and `DeviceRegistry`, not in adapters.
- Test changes on real hardware before opening a pull request.
- Document new configuration parameters in both `README.md` and `docs/CONFIGURATION.md`.
- The primary development branch convention used by automation is `claude/<session-id>`.
