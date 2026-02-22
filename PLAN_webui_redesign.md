# Web UI Redesign Plan — esphome-elero

**Date:** 2026-02-22
**Branch:** `claude/plan-webui-redesign-yUriG`

---

## Overview

Redesign the `elero_web` component to deliver:

1. A **Home Assistant switch entity** to enable/disable the web UI at runtime
2. A **modern tabbed web UI** (built with a Node.js/Vite pipeline) with four tabs:
   - **Devices** — control configured covers + runtime-editable settings + Last Seen
   - **Discovery** — discovered blinds not yet in config + scan controls + runtime adoption
   - **Log** — live log viewer with log level dropdown; names matched to configured devices
   - **Configuration** — hub-level settings (frequency, packet dump, scan limits)

---

## Current State

### Web server (`elero_web_server.cpp`, ~404 lines)
REST endpoints at `/elero/api/*`:
- `POST scan/start|stop`
- `GET discovered` → `{scanning, blinds[{blind_address, remote_address, channel, rssi, last_state, times_seen, hop, payload_1, payload_2, pck_inf[0..1], already_configured, last_seen}]}`
- `GET configured` → `{covers[{blind_address, name, position, operation}]}` — **very limited**
- `GET yaml` — full YAML for all discovered blinds
- `POST dump/start|stop`, `GET packets`, `POST packets/clear`
- `GET frequency`, `POST frequency/set?freq2=&freq1=&freq0=`

### UI (`elero_web_ui.h`, ~511 lines)
Single-page German-language HTML/CSS/JS embedded as a C string.
Cards stacked vertically: RF Scan, Discovered Devices, Configured Covers, Packet Dump, CC1101 Frequency.
No device control. No tabs.

### C++ backend gaps
- `EleroBlindBase` interface only exposes: `get_blind_name()`, `get_cover_position()`, `get_operation_str()`
- Missing from `EleroCover`: `last_seen_ms_`, last known RSSI, last known status string, runtime-settable durations exposed via web API
- No log capture infrastructure
- No enable/disable flag on `EleroWebServer`
- No runtime "adopted" blinds (blinds controllable without reflashing)

---

## Requested Feature Details

### 1. HA Switch to Enable/Disable Web UI

A new ESPHome switch platform under `elero_web`:

```yaml
switch:
  - platform: elero_web
    name: "Elero Web UI"
```

When off: all `/elero` routes return HTTP 503.
State persists across reboots via ESPHome's `ESPPreferences`.

### 2. Devices Tab

Shows every configured cover with:
- Name + blind address
- Open / Stop / Close / Tilt (if `supports_tilt`) buttons — sending commands at runtime
- Position bar (0–100 %)
- Status badge (top / bottom / moving_up / moving_down / intermediate / stopped / …)
- RSSI (dBm)
- Last Seen (human-readable elapsed time: "2 min ago")
- Expandable settings section: Poll Interval, Open Duration, Close Duration — editable text fields with Save button; changes applied in-memory immediately (survive until reboot)

Also shows **runtime-adopted blinds** (from Discovery tab) with the same controls but a distinct "adopted (not in HA)" label.

### 3. Discovery Tab

- Scan Start / Scan Stop buttons with animated status indicator
- List of discovered blinds not yet in the firmware config:
  - Address, Remote Address, Channel, RSSI, Times Seen, Last Seen, Last State
  - **"Adopt"** button → calls `POST /elero/api/discovered/{addr}/adopt` → blind moves to Devices tab as a runtime-controlled entry (no HA entity, but fully commandable from the web UI)
  - **"YAML"** button → shows per-device YAML snippet in a modal (for pasting into ESPHome config for permanent HA integration)
- "Download all YAML" button (existing `/elero/api/yaml` endpoint)
- Already-configured blinds shown with a "Configured" badge (no Adopt button)

### 4. Log Tab

- Circular log buffer (last 200 entries) in `EleroWebServer`
- Populated by hooking into ESPHome's `logger::global_logger->add_on_message_callback()` filtered to tags starting with `elero`
- **Log level dropdown** (ERROR / WARN / INFO / DEBUG / VERBOSE): controls both the display filter (client-side) and optionally the minimum capture level (server-side via `POST /elero/api/log_level`)
- Client-side: any hex address found in a log message that matches a known device is annotated with the friendly name in parentheses
- **Clear** button → `POST /elero/api/logs/clear`
- Auto-scroll toggle
- Colour-coded by level: red = ERROR, yellow = WARN, blue = INFO, grey = DEBUG/VERBOSE

### 5. Configuration Tab

Consolidates hub-level settings not related to individual covers:

- **CC1101 Frequency** (moved from current inline card): presets + manual hex registers + Apply
- **Packet Dump**: Start/Stop/Clear + live packet table (moved from current UI)
- **Scan Settings**: Max discovered count (display only or editable)
- **Hardware Info**: GDO0 pin (read-only), SPI pins (read-only), firmware uptime

---

## Architecture Changes

### New File Structure

```
components/
  elero_web/
    __init__.py              # Updated: add switch platform config option
    elero_web_server.h       # Updated: log buffer, enable flag, new handlers
    elero_web_server.cpp     # Updated: all new API endpoints
    elero_web_ui.h           # Updated: generated from frontend build
    switch/                  # NEW
      __init__.py
      elero_web_switch.h
      elero_web_switch.cpp
    frontend/                # NEW: Node.js build environment
      package.json
      vite.config.js
      index.html
      src/
        main.js              # Tab routing, state management
        tabs/
          devices.js
          discovery.js
          log.js
          config.js
        api.js               # Fetch wrappers for all endpoints
        style.css
      build_ui.py            # Post-build: embeds dist/index.html → elero_web_ui.h
  elero/
    elero.h                  # Updated: RuntimeBlind struct, new EleroBlindBase virtuals
    elero.cpp                # Updated: runtime blind support, RSSI pass-through to covers
    cover/
      EleroCover.h           # Updated: last_seen_ms_, last_rssi_, last_status_, getters
      EleroCover.cpp         # Updated: update last_seen in set_rx_state()
```

### Frontend Build Pipeline

**Stack:** Vite + vanilla JS + Alpine.js (8 KB gzipped, reactive without a framework build step overhead)

```json
// frontend/package.json (key parts)
{
  "scripts": {
    "build": "vite build && python3 build_ui.py"
  },
  "devDependencies": {
    "vite": "^5.0.0",
    "vite-plugin-singlefile": "^2.0.0"
  },
  "dependencies": {
    "alpinejs": "^3.13.0"
  }
}
```

`vite-plugin-singlefile` bundles everything (HTML + CSS + JS + inlined Alpine) into a single `dist/index.html`.

`build_ui.py` reads `dist/index.html`, minifies whitespace, and writes:

```python
# build_ui.py
with open("dist/index.html") as f:
    html = f.read()
escaped = html.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
header = f'''#pragma once
// AUTO-GENERATED — run `npm run build` in frontend/
namespace esphome {{ namespace elero {{
const char ELERO_WEB_UI_HTML[] PROGMEM = "{escaped}";
}} }}
'''
with open("elero_web_ui.h", "w") as f:
    f.write(header)
```

**Note for contributors:** The pre-built `elero_web_ui.h` is committed to the repo so end-users never need Node.js. Run `npm run build` only when modifying the frontend.

---

## C++ Backend Changes

### `EleroBlindBase` — New Virtual Methods

```cpp
// In elero.h
class EleroBlindBase {
 public:
  // ... existing virtuals ...
  virtual uint32_t get_last_seen_ms() const = 0;      // millis() of last RF packet
  virtual float    get_last_rssi()    const = 0;      // dBm, or NAN if never seen
  virtual uint8_t  get_last_state()   const = 0;      // ELERO_STATE_* constant
  virtual bool     get_supports_tilt()const = 0;
  virtual uint32_t get_poll_interval()const = 0;      // ms
  virtual uint32_t get_open_duration()const = 0;      // ms
  virtual uint32_t get_close_duration()const = 0;     // ms
  virtual uint32_t get_channel()      const = 0;
  virtual uint32_t get_remote_address()const = 0;
  // Runtime settings (survive until reboot)
  virtual void set_poll_interval_rt(uint32_t ms) = 0;
  virtual void set_open_duration_rt(uint32_t ms) = 0;
  virtual void set_close_duration_rt(uint32_t ms) = 0;
  // Command
  virtual void send_open()  = 0;
  virtual void send_close() = 0;
  virtual void send_stop()  = 0;
  virtual void send_tilt()  = 0;
  virtual void send_check() = 0;
};
```

### `EleroCover` — New Fields & Method Implementations

```cpp
// In EleroCover.h — new private members
  uint32_t last_seen_ms_{0};
  float    last_rssi_{NAN};
  uint8_t  last_state_{ELERO_STATE_UNKNOWN};

// In EleroCover::set_rx_state() — add at top
  this->last_seen_ms_ = millis();
  // RSSI is passed via the hub — need a separate setter or store it in Elero
```

RSSI is currently published directly to the sensor from `Elero::interpret_msg()`. To expose it via `EleroBlindBase`, add `set_last_rssi(float rssi)` to the base class so `interpret_msg()` can call it alongside `set_rx_state()`.

### `RuntimeBlind` — Runtime-Adopted Blinds in `Elero`

```cpp
// In elero.h
struct RuntimeBlind {
  DiscoveredBlind info;     // address, channel, remote, RF params, last_seen etc.
  std::queue<uint8_t> command_queue;
  uint8_t counter{1};
};

// New methods on Elero:
bool adopt_blind(uint32_t addr);           // moves from discovered → runtime list
bool remove_runtime_blind(uint32_t addr);
bool send_runtime_command(uint32_t addr, uint8_t command);
const std::vector<RuntimeBlind>& get_runtime_blinds() const;
```

`Elero::loop()` drains `RuntimeBlind::command_queue` the same way `EleroCover::loop()` does, reusing the existing `send_command()` path.

### `EleroWebServer` — Log Buffer & Enable Flag

```cpp
// In elero_web_server.h
struct LogEntry {
  uint32_t timestamp_ms;
  int      level;       // ESPHOME_LOG_LEVEL_*
  char     tag[24];
  char     message[128];
};

class EleroWebServer : public Component, public AsyncWebHandler {
 public:
  void set_enabled(bool en) { enabled_ = en; }
  bool is_enabled() const   { return enabled_; }

  void add_log_entry(int level, const char *tag, const char *msg);

 protected:
  bool enabled_{true};
  static constexpr size_t LOG_BUFFER_SIZE = 200;
  LogEntry log_buffer_[LOG_BUFFER_SIZE];
  size_t   log_head_{0};
  size_t   log_count_{0};

  // ... all new handlers below ...
  void handle_cover_command(AsyncWebServerRequest *request, uint32_t addr);
  void handle_cover_settings(AsyncWebServerRequest *request, uint32_t addr);
  void handle_runtime_list(AsyncWebServerRequest *request);
  void handle_runtime_command(AsyncWebServerRequest *request, uint32_t addr);
  void handle_runtime_adopt(AsyncWebServerRequest *request, uint32_t addr);
  void handle_runtime_remove(AsyncWebServerRequest *request, uint32_t addr);
  void handle_get_logs(AsyncWebServerRequest *request);
  void handle_clear_logs(AsyncWebServerRequest *request);
  void handle_set_log_level(AsyncWebServerRequest *request);
  void handle_webui_enable(AsyncWebServerRequest *request);
};
```

In `EleroWebServer::setup()` hook into ESPHome logger:
```cpp
#ifdef USE_LOGGER
  if (logger::global_logger != nullptr) {
    logger::global_logger->add_on_message_callback(
      [this](int level, const char *tag, const char *msg) {
        if (strncmp(tag, "elero", 5) == 0)
          this->add_log_entry(level, tag, msg);
      });
  }
#endif
```

`canHandle()` must first check `enabled_`:
```cpp
bool EleroWebServer::canHandle(AsyncWebServerRequest *request) {
  if (!enabled_) return false;   // 503 handled elsewhere or just 404
  const std::string &url = request->url();
  return url.size() >= 6 && url.substr(0, 6) == "/elero";
}
```

### HA Switch — `elero_web/switch/`

```cpp
// elero_web_switch.h
class EleroWebSwitch : public switch_::Switch, public Component {
 public:
  void set_web_server(EleroWebServer *ws) { ws_ = ws; }
  void setup() override;

 protected:
  void write_state(bool state) override;
  EleroWebServer *ws_{nullptr};
};

// elero_web_switch.cpp
void EleroWebSwitch::setup() {
  // Restore previous state from flash
  auto restore = this->get_initial_state_with_restore_mode();
  if (restore.has_value())
    this->write_state(restore.value());
}
void EleroWebSwitch::write_state(bool state) {
  this->ws_->set_enabled(state);
  this->publish_state(state);
}
```

```python
# switch/__init__.py
CONFIG_SCHEMA = switch.switch_schema(EleroWebSwitch).extend({
    cv.GenerateID(CONF_ELERO_WEB_ID): cv.use_id(EleroWebServer),
}).extend(cv.COMPONENT_SCHEMA)
```

---

## New REST API Endpoints

### Devices (configured covers)

| Method | URL | Description |
|--------|-----|-------------|
| GET | `/elero/api/configured` | **Extended** — now includes `last_seen_ms`, `rssi`, `last_state`, `poll_interval`, `open_duration`, `close_duration`, `supports_tilt`, `channel`, `remote_address` |
| POST | `/elero/api/covers/{addr}/command` | Body: `{"command":"open"\|"close"\|"stop"\|"tilt"\|"check"}` |
| POST | `/elero/api/covers/{addr}/settings` | Body: `{"poll_interval":300000,"open_duration":25000,"close_duration":22000}` |

### Runtime Adopted Blinds

| Method | URL | Description |
|--------|-----|-------------|
| GET | `/elero/api/runtime` | List runtime-adopted blinds (same schema as `discovered`) |
| POST | `/elero/api/discovered/{addr}/adopt` | Moves blind from discovered → runtime list |
| DELETE | `/elero/api/runtime/{addr}` | Remove runtime blind |
| POST | `/elero/api/runtime/{addr}/command` | Body: `{"command":"open"\|"close"\|"stop"\|"tilt"\|"check"}` |

### Logs

| Method | URL | Description |
|--------|-----|-------------|
| GET | `/elero/api/logs?since_idx=0` | Returns log entries `[{idx, ts_ms, level, tag, message}]` |
| POST | `/elero/api/logs/clear` | Clears log buffer |
| POST | `/elero/api/log_level` | Body: `{"min_level":3}` — sets minimum capture level |

### Web UI Enable/Disable (for direct REST control, switch entity is primary)

| Method | URL | Description |
|--------|-----|-------------|
| POST | `/elero/api/webui/enable` | `{"enabled":true\|false}` |

---

## UI Design Wireframes

### Overall Layout

```
┌──────────────────────────────────────────────────┐
│  ⊞ Elero Blind Manager                [● Enabled]│
├──────────┬───────────┬──────────┬────────────────┤
│ Devices  │ Discovery │   Log    │ Configuration  │
├──────────┴───────────┴──────────┴────────────────┤
│                                                  │
│  [Tab Content]                                   │
│                                                  │
└──────────────────────────────────────────────────┘
```

### Devices Tab

```
┌─────────────────────────────────────────────────┐
│ Wohnzimmer Rollo                    ● intermediate│
│ 0xa831e5 · CH 3 · RSSI -72.5 dBm               │
│ ████████████░░░░░░░░░  65 %                     │
│ Last seen: 2 min ago                            │
│  [↑ Open]  [■ Stop]  [↓ Close]                  │
│ ▼ Settings                                      │
│   Poll Interval [5min    ] Open [25s ] Close [22s]│
│                                    [Save]        │
├─────────────────────────────────────────────────┤
│ Schlafzimmer Rollo              ● top            │
│ 0xb293f1 · CH 5 · RSSI -68.0 dBm               │
│ ████████████████████  100 %                     │
│ Last seen: 8 sec ago                            │
│  [↑ Open]  [■ Stop]  [↓ Close]  [↔ Tilt]       │
├─────────────────────────────────────────────────┤
│ Adopted Blind (not in HA)         ● unknown     │
│ 0xc11f22 · CH 2 · RSSI -81.0 dBm               │
│  [↑ Open]  [■ Stop]  [↓ Close]                  │
└─────────────────────────────────────────────────┘
```

### Discovery Tab

```
┌─────────────────────────────────────────────────┐
│  [▶ Start Scan]  [■ Stop Scan]   ● Scanning...  │
│  Operate your Elero remote during scan.         │
├─────────────────────────────────────────────────┤
│ 0xc11f22                          ● bottom      │
│ CH: 2  Remote: 0xa831e5  RSSI: -81 dBm  Seen: 3x│
│ Last seen: 45 sec ago                           │
│             [YAML]  [Adopt → Devices tab]        │
├─────────────────────────────────────────────────┤
│ 0xa831e5                  ✓ Already configured  │
│ CH: 3  Remote: 0xf29a01  RSSI: -72 dBm  Seen: 8x│
│ Last seen: 2 min ago                            │
├─────────────────────────────────────────────────┤
│              [↓ Download all YAML]              │
└─────────────────────────────────────────────────┘
```

### Log Tab

```
┌─────────────────────────────────────────────────┐
│ Level: [INFO ▼]    [Auto-scroll ✓]    [Clear]   │
├─────────────────────────────────────────────────┤
│ 14:23:01  INFO   elero        Setup complete     │
│ 14:23:05  INFO   elero.cover  Wohnzimmer (0xa831e5)│
│                               → state: bottom   │
│ 14:24:12  WARN   elero        Timeout 0xb293f1  │
│ 14:24:18  DEBUG  elero.cover  Poll: 0xa831e5    │
│ 14:24:20  INFO   elero.cover  Schlafzimmer      │
│                               (0xb293f1) → top  │
└─────────────────────────────────────────────────┘
```

Address ↔ name resolution: the log viewer JavaScript maintains a map
`{hex_addr: friendly_name}` from the `/elero/api/configured` response and
substitutes any matching `0xXXXXXX` token in log messages.

### Configuration Tab

```
┌─────────────────────────────────────────────────┐
│ CC1101 Frequency                                │
│  Preset: [868.35 MHz (Standard) ▼]              │
│  freq2: [0x21]  freq1: [0x71]  freq0: [0x7a]   │
│                                    [Apply]       │
├─────────────────────────────────────────────────┤
│ Packet Dump                                     │
│  [▶ Start Dump]  [■ Stop]  [Clear]              │
│  ┌─────────────────────────────────────────┐   │
│  │ Time(ms) │ Len │ Status │ Reason │ Hex  │   │
│  │ 14230120 │  23 │  OK    │        │ 4a…  │   │
│  │ 14230198 │  11 │  ERR   │ short  │ 2b…  │   │
│  └─────────────────────────────────────────┘   │
├─────────────────────────────────────────────────┤
│ Hardware Info                                   │
│  GDO0 Pin: GPIO26    Uptime: 1h 24m 05s         │
│  freq2/1/0: 0x21 / 0x71 / 0x7a                 │
│  Registered covers: 2                           │
└─────────────────────────────────────────────────┘
```

---

## Naming Consistency

### State Labels
Use a shared `STATE_LABELS` map in both C++ (`elero_state_to_string()`) and JS:

```js
const STATE_LABELS = {
  top: "Top",
  bottom: "Bottom",
  intermediate: "Intermediate",
  tilt: "Tilt",
  blocking: "Blocking",
  overheated: "Overheated",
  timeout: "Timeout",
  start_moving_up: "Starting Up",
  start_moving_down: "Starting Down",
  moving_up: "Moving Up",
  moving_down: "Moving Down",
  stopped: "Stopped",
  top_tilt: "Top (Tilt)",
  bottom_tilt: "Bottom (Tilt)",
  unknown: "Unknown",
};
```

These strings must be kept in sync with `elero_state_to_string()` in `elero.cpp`.

### Language
The new UI is in **English** throughout (the current UI is German). Log messages produced by C++ code are already in English.

---

## Implementation Phases

### Phase 0 — Git Setup
- Ensure work is on branch `claude/plan-webui-redesign-yUriG`
- Commit current state as baseline

### Phase 1 — C++ Backend: Richer Cover Data
**Files:** `elero.h`, `elero.cpp`, `cover/EleroCover.h`, `cover/EleroCover.cpp`

1. Add `last_seen_ms_`, `last_rssi_`, `last_state_` fields to `EleroCover`
2. Update `set_rx_state()` to set `last_seen_ms_ = millis()` and store `last_state_`
3. Add `set_last_rssi(float rssi)` to `EleroBlindBase`; call it from `Elero::interpret_msg()` alongside `set_rx_state()`
4. Add all new virtual getters to `EleroBlindBase` and implement in `EleroCover`
5. Add command push helpers (`send_open()`, `send_close()`, etc.) to `EleroCover`
6. Extend `handle_get_configured()` to include all new fields
7. Add `handle_cover_command()` and `handle_cover_settings()` endpoints

### Phase 2 — C++ Backend: Runtime Adopted Blinds
**Files:** `elero.h`, `elero.cpp`, `elero_web_server.h`, `elero_web_server.cpp`

1. Add `RuntimeBlind` struct to `elero.h`
2. Add `runtime_blinds_` vector + `adopt_blind()`, `remove_runtime_blind()`, `send_runtime_command()`, `get_runtime_blinds()` to `Elero`
3. Drain `RuntimeBlind::command_queue` in `Elero::loop()`
4. Add `/elero/api/discovered/{addr}/adopt`, `/elero/api/runtime`, `/elero/api/runtime/{addr}/command`, `/elero/api/runtime/{addr}` (DELETE) handlers to web server

### Phase 3 — C++ Backend: Log Buffer
**Files:** `elero_web_server.h`, `elero_web_server.cpp`

1. Add `LogEntry` struct and ring buffer to `EleroWebServer`
2. Hook into `logger::global_logger` in `setup()`; filter to `elero` tags
3. Add `GET /elero/api/logs`, `POST /elero/api/logs/clear`, `POST /elero/api/log_level` handlers

### Phase 4 — HA Switch Component
**Files:** `elero_web/switch/__init__.py`, `elero_web_switch.h`, `elero_web_switch.cpp`

1. Create the switch sub-platform following the ESPHome pattern
2. Add `enabled_` flag to `EleroWebServer` and check it in `canHandle()`
3. Add switch platform to `elero_web/__init__.py`
4. Update example YAML in `example.yaml`

### Phase 5 — Frontend Build Environment
**Files:** `components/elero_web/frontend/` (all new)

1. `npm init` → `package.json` with Vite + `vite-plugin-singlefile` + Alpine.js
2. `vite.config.js` configured for singlefile output
3. Skeleton `index.html` with tab chrome (four tabs, responsive)
4. `src/api.js` — all fetch wrappers mirroring new API
5. `build_ui.py` — embed `dist/index.html` into `elero_web_ui.h`
6. Verify build works: `npm run build` → `elero_web_ui.h` updated

### Phase 6 — Devices Tab Implementation (Frontend)
**Files:** `frontend/src/tabs/devices.js`

1. Fetch `/elero/api/configured` and `/elero/api/runtime` every 3 seconds
2. Render cover cards with position bar, status badge, Last Seen
3. Open / Stop / Close / Tilt buttons → `POST /elero/api/covers/{addr}/command`
4. Collapsible settings section → `POST /elero/api/covers/{addr}/settings`
5. Runtime blinds section with "adopted" label

### Phase 7 — Discovery Tab Implementation (Frontend)
**Files:** `frontend/src/tabs/discovery.js`

1. Scan start/stop with animated status dot
2. Auto-refresh discovered list every 3 seconds while scanning
3. Per-blind Adopt button → `POST /elero/api/discovered/{addr}/adopt` → refresh Devices tab
4. Per-blind YAML button → show snippet in modal
5. "Download all YAML" button

### Phase 8 — Log Tab Implementation (Frontend)
**Files:** `frontend/src/tabs/log.js`

1. Poll `/elero/api/logs?since_idx={last_idx}` every 2 seconds (incremental)
2. Log level dropdown → client-side filter; `POST /elero/api/log_level` for server-side
3. Address → name substitution in message text
4. Auto-scroll with toggle; Clear button

### Phase 9 — Configuration Tab Implementation (Frontend)
**Files:** `frontend/src/tabs/config.js`

1. Move CC1101 frequency card content here
2. Move Packet Dump card content here
3. Add Hardware Info section (`GET /elero/api/configured` header data)

### Phase 10 — Polish & Integration
1. English language throughout; consistent state label wording
2. Responsive design (mobile-friendly)
3. Toast notifications for all actions
4. Dark/light mode respect (`prefers-color-scheme`)
5. Rebuild `elero_web_ui.h` with final frontend
6. Update `README.md` and `docs/CONFIGURATION.md`
7. Update `example.yaml` with switch entity example
8. Commit & push to `claude/plan-webui-redesign-yUriG`

---

## Configuration YAML Examples (After Implementation)

```yaml
# Hub (unchanged)
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26

# Web UI server
web_server:

elero_web:
  id: elero_web_ui

# HA switch to enable/disable the web UI
switch:
  - platform: elero_web
    name: "Elero Web UI"
    id: elero_web_switch

# Covers (unchanged)
cover:
  - platform: elero
    blind_address: 0xa831e5
    channel: 3
    remote_address: 0xf29a01
    name: "Wohnzimmer Rollo"
    open_duration: 25s
    close_duration: 22s
```

---

## Open Questions / Decisions Needed

1. **Runtime adopted blinds persistence**: Currently planned to be lost on reboot. Should adopted blinds be persisted in NVS? Adds complexity but better UX.

2. **Cover command security**: No authentication on the web API. Acceptable for a local-network ESPHome device?

3. **Log buffer size**: 200 entries at ~180 bytes each = ~36 KB RAM on heap. ESP32 has ~200 KB free heap typically. Acceptable?

4. **Frontend framework**: Plan uses Alpine.js (8 KB). Alternative: plain vanilla JS (no extra dependency but more verbose). Confirm preference before Phase 5.

5. **Settings persistence**: Runtime-changed poll intervals / durations survive until next reboot. Should they be persisted in flash via `ESPPreferences`?
