# RFC-001: TX Race Condition Fix & Unified Naming

**Status:** Partially implemented
**Date:** 2026-03-02
**Authors:** Claude + User

## Implementation Status

| Phase | Description | Status | Notes |
|-------|-------------|--------|-------|
| Phase 1 | TX Race Fix | ✅ Complete | Implemented in prior session |
| Phase 2 | Config Naming | ✅ Complete | blind_address→dst_address, remote_address→src_address, pck_inf1→type, pck_inf2→type2 |
| Phase 3 | C++ Struct Naming | ✅ Complete | Struct fields renamed to match config |
| Phase 4 | JSON Logging | ❌ Deferred | Still using human-readable logs; JSON logging is optional enhancement |
| Phase 5 | WebSocket JSON | ✅ Complete | Field names unified |
| Phase 6 | Skill Documentation | ❌ Deferred | Lower priority |
| Phase 7 | Example & Docs | ✅ Complete | README, CONFIGURATION.md, example.yaml updated |

---

## Summary

This RFC addresses two related issues:

1. **TX Race Condition** - The CC1101 can return to RX mode during TX setup, causing repeated `Unexpected MARCSTATE=0x0d` errors and failed transmissions.

2. **Logging & Naming Inconsistencies** - Field names differ between config, logs, and WebSocket JSON, making debugging difficult. Human-readable and machine-readable logs are redundant.

---

## Part 1: TX Race Condition Fix

### 1.1 Root Cause

The CC1101 is configured with `MCSM1=0x3F`:
- `CCA_MODE=3`: TX only if channel clear AND not currently receiving
- `RXOFF_MODE=3`: Auto-return to RX after RX completes
- `TXOFF_MODE=3`: Auto-return to RX after TX completes

**The Race:**

```
Time    TX State Machine              CC1101 State       RF Activity
─────   ────────────────────────      ──────────────     ───────────
T+0ms   GOTO_IDLE: send SIDLE         RX → IDLE
T+1ms   GOTO_IDLE: check = IDLE ✓     IDLE
T+2ms   FLUSH_TX: send SFTX           IDLE               packet arrives!
T+3ms   FLUSH_TX: wait 1ms...         IDLE → RX          GDO0 interrupt
T+4ms   LOAD_FIFO: load data          RX (receiving)
T+5ms   LOAD_FIFO: send STX           RX (ignored!)      CCA fails
T+6ms   TRIGGER_TX: check MARCSTATE   RX (0x0d)          ← ABORT!
```

Between verifying IDLE and sending STX (~2-3ms), a packet can arrive. The CC1101 transitions to RX and stays there. The STX command is ignored because CCA detects channel busy.

### 1.2 Observed Symptoms

```
[09:16:19.841][W][elero:585]: Unexpected MARCSTATE=0x0d in TRIGGER_TX, aborting
[09:16:19.842][W][elero:411]: TX aborted in state 4, marcstate=0x0d
[09:16:19.844][D][elero.cover:169]: TX retry 2/3 for 0x333238
[09:16:19.903][D][elero:743]: rcv'd: len=29, cnt=65, typ=0x6a...
[09:16:19.940][W][elero:585]: Unexpected MARCSTATE=0x0d in TRIGGER_TX, aborting
...
[09:16:20.043][E][elero.cover:174]: Max retries for 0x333238, dropping command 0x00
```

Packets arrive faster than TX can complete, causing repeated aborts and eventual command drop.

### 1.3 Solution: Option A + E (Guard + Bounded Backoff)

#### Option A: MARCSTATE Guard Before STX

In `LOAD_FIFO`, verify IDLE before sending STX. If not IDLE, restart from `GOTO_IDLE` instead of aborting:

```cpp
case TxState::LOAD_FIFO: {
  // Check we're still in IDLE (a packet might have arrived)
  uint8_t marcstate = this->read_status(CC1101_MARCSTATE) & 0x1F;
  if (marcstate != CC1101_MARCSTATE_IDLE) {
    ESP_LOGD(TAG, "Radio not idle before STX (0x%02x), deferring", marcstate);
    this->write_cmd(CC1101_SIDLE);
    this->tx_ctx_.state = TxState::GOTO_IDLE;
    this->tx_ctx_.state_enter_time = now;
    this->tx_ctx_.defer_count++;

    // Apply backoff if deferred multiple times
    if (this->tx_ctx_.defer_count > TX_DEFER_BACKOFF_THRESHOLD) {
      this->tx_ctx_.backoff_until = now + random_uint32() % TX_BACKOFF_MAX_MS;
    }
    return;
  }

  // Load TX FIFO
  if (!this->write_burst(CC1101_TXFIFO, this->msg_tx_, this->msg_tx_[0] + 1)) {
    this->abort_tx_();
    return;
  }

  // Clear received_ flag for TX-end detection
  this->received_.store(false);

  // Final IDLE check + STX in tight sequence (minimize race window)
  marcstate = this->read_status(CC1101_MARCSTATE) & 0x1F;
  if (marcstate != CC1101_MARCSTATE_IDLE) {
    ESP_LOGD(TAG, "Radio left idle during FIFO load (0x%02x), deferring", marcstate);
    this->write_cmd(CC1101_SIDLE);
    this->tx_ctx_.state = TxState::GOTO_IDLE;
    this->tx_ctx_.state_enter_time = now;
    this->tx_ctx_.defer_count++;
    return;
  }

  // Trigger TX
  if (!this->write_cmd(CC1101_STX)) {
    this->abort_tx_();
    return;
  }
  this->tx_ctx_.state = TxState::TRIGGER_TX;
  this->tx_ctx_.state_enter_time = now;
  break;
}
```

#### Option E: Bounded Backoff

Add defer counter and backoff timing to `TxContext`:

```cpp
struct TxContext {
  TxState state{TxState::IDLE};
  uint32_t state_enter_time{0};
  uint8_t defer_count{0};        // NEW: times deferred due to busy channel
  uint32_t backoff_until{0};     // NEW: don't attempt TX until this time

  static constexpr uint32_t STATE_TIMEOUT_MS = 100;
};

// Constants
constexpr uint8_t TX_DEFER_BACKOFF_THRESHOLD = 3;  // Start backoff after 3 defers
constexpr uint8_t TX_DEFER_MAX = 10;               // Abort after 10 defers
constexpr uint32_t TX_BACKOFF_MAX_MS = 50;         // Max random backoff
```

In `GOTO_IDLE`, check backoff:

```cpp
case TxState::GOTO_IDLE:
  // Check backoff timer
  if (now < this->tx_ctx_.backoff_until) {
    return;  // Still in backoff, wait
  }

  // Check defer limit
  if (this->tx_ctx_.defer_count >= TX_DEFER_MAX) {
    ESP_LOGW(TAG, "TX deferred %d times, aborting", this->tx_ctx_.defer_count);
    this->abort_tx_();
    return;
  }

  // ... rest of GOTO_IDLE logic
```

#### Reset Defer Count

Reset `defer_count` on successful TX and on `abort_tx_()`:

```cpp
void Elero::abort_tx_() {
  // ... existing code ...
  this->tx_ctx_.defer_count = 0;
  this->tx_ctx_.backoff_until = 0;
}

case TxState::RETURN_RX:
  this->flush_and_rx();
  this->tx_pending_success_ = true;
  this->tx_ctx_.state = TxState::IDLE;
  this->tx_ctx_.defer_count = 0;      // Reset on success
  this->tx_ctx_.backoff_until = 0;
  break;
```

### 1.4 Why This Approach

| Aspect | Benefit |
|--------|---------|
| Preserves auto-RX | Discovery and status monitoring continue working |
| Minimal code change | ~30 lines, no CC1101 config changes |
| Bounded retries | Prevents infinite retry storms |
| Graceful degradation | Commands eventually succeed or fail cleanly |
| No packet loss | CC1101 stays in RX during backoff |

### 1.5 Testing Plan

1. **Unit test**: Mock `read_status()` to return RX during LOAD_FIFO, verify defer logic
2. **Integration**: Generate incoming packets during TX setup, verify no WARN spam
3. **Busy RF**: Multiple blinds, ensure commands complete within bounded time
4. **Regression**: Normal TX path latency unchanged

---

## Part 2: Unified Logging & Naming

### 2.1 Problem

Current state has inconsistent naming:

| Concept | Config YAML | Logs | WebSocket |
|---------|-------------|------|-----------|
| Blind address | `blind_address` | `src` (RX) / `dst` (TX) | `src` |
| Remote address | `remote_address` | `dst` (RX) / `src` (TX) | `dst` |
| Channel | `channel` | `chl` | `ch` |
| Message type | `pck_inf1` | `typ` | `type` |
| Command | `command_*` | `cmd` | `cmd` |

Also: human-readable logs duplicate machine-readable JSON - wasteful and inconsistent.

### 2.2 Solution: Unified Field Names

**Use RF terminology consistently:**

| Field | Everywhere | Format | Description |
|-------|------------|--------|-------------|
| `src` | config, logs, WS | `0xa831e5` | Source address (who sent) |
| `dst` | config, logs, WS | `0xc41a2b` | Destination address (who receives) |
| `channel` | config, logs, WS | `5` | RF channel (decimal) |
| `type` | config, logs, WS | `0xca` | Message type byte |
| `type2` | config, logs, WS | `0x00` | Secondary type byte |
| `hop` | config, logs, WS | `0x0a` | Routing/relay field |
| `command` | config, logs, WS | `0x20` | Command byte |
| `state` | logs, WS | `0x01` | State byte |
| `rssi` | logs, WS | `-50.0` | Signal strength (dBm) |

### 2.3 Config Changes

**Before:**
```yaml
cover:
  - platform: elero
    blind_address: 0xa831e5
    remote_address: 0xc41a2b
    channel: 5
    pck_inf1: 0x6a
    pck_inf2: 0x00
    hop: 0x0a
```

**After:**
```yaml
cover:
  - platform: elero
    dst_address: 0xa831e5      # The blind we control (destination of our commands)
    src_address: 0xc41a2b      # Our emulated address (source of our commands)
    channel: 5
    type: 0x6a                 # Message type (default: 0x6a for commands)
    type2: 0x00                # Secondary type (default: 0x00)
    hop: 0x0a                  # Routing field (default: 0x0a)
```

**Rationale for src/dst:**
- When we TX a command: `src` = us, `dst` = blind
- When we RX a status: `src` = blind, `dst` = us
- Config defines OUR addresses for TX, so `src_address` is what we transmit as source

### 2.4 Logging Changes

**Remove:** Human-readable `rcv'd:` and `send:` logs

**Add:** Single JSON log line per TX/RX event

**Tag:** `elero.rf`

**RX format:**
```json
{"dir":"rx","ts":12345000,"len":29,"cnt":1,"type":"0xca","type2":"0x00","hop":"0x0a","channel":5,"src":"0xa831e5","dst":"0xc41a2b","command":"0x00","state":"0x01","rssi":-50.0,"raw":"1c,01,ca,...","payload_enc":"9f,c3,...","payload_dec":"00,00,..."}
```

**TX format:**
```json
{"dir":"tx","ts":12345100,"len":29,"cnt":2,"type":"0x6a","type2":"0x00","hop":"0x0a","channel":5,"src":"0xc41a2b","dst":"0xa831e5","command":"0x20"}
```

### 2.5 WebSocket Changes

**Config event:**
```json
{
  "event": "config",
  "device": "esp32-elero",
  "freq": {"freq2": "0x21", "freq1": "0x71", "freq0": "0x7a"},
  "blinds": [
    {"dst_address": "0xa831e5", "src_address": "0xc41a2b", "channel": 5, "name": "Living Room"}
  ]
}
```

**RF event:**
```json
{
  "event": "rf",
  "dir": "rx",
  "ts": 12345000,
  "type": "0xca",
  "type2": "0x00",
  "hop": "0x0a",
  "channel": 5,
  "src": "0xa831e5",
  "dst": "0xc41a2b",
  "command": "0x00",
  "state": "0x01",
  "rssi": -50.0,
  "raw": "1c 01 ca 00 0a 01 05 ..."
}
```

### 2.6 Skill Documentation Update

Update `.claude/skills/elero-protocol/skill.md`:

1. Split offset 3-5 into separate fields (not "header")
2. Add "Protocol Ambiguities" section documenting `type2` and `hop` variations
3. Add "Communication Flows" section with TX→RX examples
4. Add Config column to packet format table

---

## Part 3: Implementation Plan

### Phase 1: TX Race Fix (elero.cpp, elero.h)

1. Add `defer_count` and `backoff_until` to `TxContext` struct
2. Add constants: `TX_DEFER_BACKOFF_THRESHOLD`, `TX_DEFER_MAX`, `TX_BACKOFF_MAX_MS`
3. Modify `LOAD_FIFO` state: add MARCSTATE guards before and after FIFO load
4. Modify `GOTO_IDLE` state: check backoff timer and defer limit
5. Modify `RETURN_RX` state: reset defer count on success
6. Modify `abort_tx_()`: reset defer count
7. Adjust log levels: DEBUG for expected defers, WARN for limit reached

### Phase 2: Config Naming (Python __init__.py files)

1. `components/elero/cover/__init__.py`:
   - Rename `CONF_BLIND_ADDRESS` → `CONF_DST_ADDRESS`
   - Rename `CONF_REMOTE_ADDRESS` → `CONF_SRC_ADDRESS`
   - Rename `CONF_PCKINF_1` → `CONF_TYPE`
   - Rename `CONF_PCKINF_2` → `CONF_TYPE2`
   - Update schema and `to_code()`

2. `components/elero/light/__init__.py`: Same renames

3. `components/elero/sensor/__init__.py`: Update address field name

4. `components/elero/text_sensor/__init__.py`: Update address field name

### Phase 3: C++ Struct Naming (elero.h, EleroCover.h, EleroLight.h)

1. Rename struct fields to match config:
   - `blind_addr` → `dst_addr`
   - `remote_addr` → `src_addr`
   - `pck_inf[0]` → `type`
   - `pck_inf[1]` → `type2`

2. Update all setter methods

### Phase 4: Logging (elero.cpp)

1. Remove human-readable `rcv'd:` log
2. Remove human-readable `send:` log
3. Add JSON logging helper function: `log_rf_packet_json()`
4. Add JSON log in `interpret_msg()` for RX
5. Add JSON log in TX path for TX
6. Use tag `elero.rf` for all RF packet logs

### Phase 5: WebSocket (elero_web_server.cpp)

1. Update `build_rf_json()` with new field names
2. Update `build_config_json()` with new field names
3. Rename `ch` → `channel`, `cmd` → `command`
4. Add `type2`, `hop` fields

### Phase 6: Skill Documentation

1. Update packet format table in skill.md
2. Add Protocol Ambiguities section
3. Add Communication Flows section

### Phase 7: Example & Docs

1. Update `example.yaml` with new config names
2. Update `README.md` with new config names
3. Update `docs/CONFIGURATION.md`

---

## File Change Summary

| File | Changes |
|------|---------|
| `components/elero/elero.h` | TxContext fields, constants, struct renames |
| `components/elero/elero.cpp` | TX state machine fix, JSON logging |
| `components/elero/cover/__init__.py` | Config renames |
| `components/elero/cover/EleroCover.h` | Setter renames |
| `components/elero/cover/EleroCover.cpp` | Field access renames |
| `components/elero/light/__init__.py` | Config renames |
| `components/elero/light/EleroLight.h` | Setter renames |
| `components/elero/light/EleroLight.cpp` | Field access renames |
| `components/elero/sensor/__init__.py` | Config renames |
| `components/elero/text_sensor/__init__.py` | Config renames |
| `components/elero_web/elero_web_server.cpp` | JSON field renames |
| `.claude/skills/elero-protocol/skill.md` | Documentation updates |
| `example.yaml` | Config example updates |
| `README.md` | Documentation updates |
| `docs/CONFIGURATION.md` | Documentation updates |

---

## Appendix: Tradeoff Analysis for TX Fix Options

| Aspect | Option A: Guard | Option B: Disable auto-RX | Option C: Retry in-loop | Option D: Disable CCA | Option E: Backoff |
|--------|-----------------|---------------------------|-------------------------|----------------------|-------------------|
| Code change | ~10 lines | ~5 lines, 2 locations | ~15 lines | 1 line | ~20 lines |
| CC1101 config | None | Yes | None | Yes | None |
| Race window | Yes (µs) | No | Yes (µs) | No | Yes (µs) |
| RX during TX setup | Received | **LOST** | Received | Received | Received |
| CPU starvation risk | Low | None | High | None | Low |
| Infinite retry risk | Possible | No | Possible | No | No |

**Chosen: A + E combined** - Best balance of reliability, simplicity, and no packet loss.
