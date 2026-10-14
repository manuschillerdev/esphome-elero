# AGENTS.md — esphome-elero

## Project Overview

`esphome-elero` is a custom **ESPHome external component** that enables Home Assistant to control Elero wireless motor blinds (rollers, shutters, awnings) via a **CC1101 868 MHz (or 433 MHz) RF transceiver** connected to an ESP32 over SPI.

```yaml
external_components:
  - source: github://manuschillerdev/esphome-elero
```

## Pi Skills (use them)

Project-local pi skills live in `.pi/skills/`.

| Skill | When to use |
|---|---|
| `/skill:elero-protocol` | **Always** when modifying CC1101 TX/RX code, packet encoding/decoding, encryption |
| `/skill:sx1262-driver` | **Always** when modifying SX1262 driver code, debugging TX/RX, or radio config |
| `/skill:modern-cpp` | **Always** when writing or reviewing C++ code |
| `/skill:esp32-development` | **Always** when writing C++ code (ISRs, memory, FreeRTOS, SPI) |

> **Important:** Before writing C++ code, load `/skill:modern-cpp` and `/skill:esp32-development`.
> Before touching RF protocol code (`elero.cpp` TX/RX, packet handling), load `/skill:elero-protocol`.
> Before touching SX1262 driver code, load `/skill:sx1262-driver`.

---

## Compatibility Matrix

**Supported targets — ESP32 only:**

| Framework | Status | Notes |
|---|---|---|
| **ESP-IDF** | Supported | Primary target, recommended |
| **Arduino** | Supported | Legacy support via ESPHome |

**Not supported:** ESP8266, RP2040, LibreTiny, Host/native.

The codebase uses Mongoose for HTTP/WebSocket specifically because it provides a unified API across ESP-IDF and Arduino frameworks. Do not introduce framework-specific code paths.

---

## Architecture

Detailed diagrams and data flows live in `docs/`:
- `docs/flows/STATE_MACHINES.md`
- `docs/STATE_MACHINES.md`
- `docs/STATE_REPORTING.md`

### Core Design Principles

1. **Minimal base state, maximal derivation.** Store the smallest possible state and derive everything else from `(state, now, config)`.
2. **One path, no redundant implementations.** One `Device`, one `DeviceRegistry`, one set of state machines.
3. **Route and fail early.** RF packets decode on Core 0, dispatch on Core 1, invalid configs rejected before persistence.
4. **Keep the state space flat.** Variant-based `Device` + `OutputAdapter` observer pattern keeps changes additive.
5. **Strict unidirectional data flow.** Hardware → RF task (Core 0) → queues → main loop (Core 1) → registry → adapters.

### Two Operating Modes

Devices always live in NVS. The modes differ only in how devices surface to Home Assistant.

| | Native API Mode | MQTT Mode |
|---|---|---|
| **Devices defined in** | NVS (web UI / `import_config`) | NVS (web UI / `import_config`) |
| **Home Assistant** | ESPHome native API | MQTT HA discovery |
| **Component combo** | `elero:` + `elero_nvs:` | `elero:` + `elero_mqtt:` |
| **Output adapter** | `NvsAdapter` → `EspCoverShell` / `EspLightShell` | `MqttAdapter` |
| **CRUD apply latency** | Reboot required | Immediate |

Backup/restore (`export_config` / `import_config`) is the supported recovery path.

### Critical Guardrails

- **`EleroWebServer` is stateless** for live data. It forwards live RF/config/state data and proxies CRUD.
- **The client derives live state** from `config` + `rf` + `state_changed` events.
- **`DeviceRegistry` is the single source of truth.** All adapters route commands through registry APIs.
- **Adapters are thin formatters.** Business logic belongs in state machines and the registry.

### Observer Pattern + Centralized Publish Decisions

The registry owns all publish decisions. It computes snapshots, diffs them against per-device published caches, and only notifies adapters when something actually changed.

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

**Change flags** (`state_change::`): `POSITION`, `HA_STATE`, `OPERATION`, `TILT`, `PROBLEM`, `RSSI`, `STATE_STRING`, `BRIGHTNESS`, `REMOTE_ACTIVITY`, `ALL`.

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

Use the parent-hub pattern:

```python
cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero)
```

---

## Configuration & Testing

**Configuration reference:** `docs/CONFIGURATION.md`
**Migration guide:** `docs/MIGRATION-yaml-to-nvs.md`
**Example config:** `example.yaml`

**Unit tests**
```bash
cd tests/unit && cmake -B build && cmake --build build && ctest --test-dir build
```

**Compile tests**
```bash
uv run esphome compile tests/test.esp32-minimal.yaml
uv run esphome compile tests/test.esp32-mqtt.yaml
uv run esphome compile tests/test.esp32-nvs.yaml
```

**Python tests**
```bash
uv run pytest tests/python/
```

Additional migrated agent docs live in `.pi/reference/`.

---

## Common Pitfalls

- **Wrong frequency:** Most European Elero motors use 868.35 MHz (`freq0=0x7a`). Some use 868.95 MHz (`freq0=0xc0`).
- **SPI conflicts:** The CC1101 CS pin must not be shared with another SPI device.
- **`web_server:` vs `web_server_base:`:** Use `web_server_base:` for `/elero` only. Adding `web_server:` re-enables the default ESPHome UI.
- **Position tracking:** Leave `open_duration` and `close_duration` at `0s` if you only need open/close.

---

## Contributing

- Follow the existing naming conventions.
- Keep output adapters thin.
- Test changes on real hardware before opening a PR.
- Document new configuration parameters in both `README.md` and `docs/CONFIGURATION.md`.
- The primary development branch convention used by automation is `pi/<session-id>`.
