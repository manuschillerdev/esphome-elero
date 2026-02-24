# Recurring Bug Patterns — esphome-elero

## 1. payload[] index inconsistency (runtime vs. cover/light paths)
**File:** `elero.cpp` line 58 vs `EleroCover.cpp` line 97 / `EleroLight.cpp` line 141
- Runtime blind drain in `Elero::loop()` writes the command byte to `cmd.payload[8]`
- Cover and light write to `cmd.payload[4]` (which is what `send_command()` maps into `msg_tx_[24]`)
- In `send_command()`, `msg_tx_[20..29]` gets all 10 payload bytes; `msg_tx_[22]` is then
  overwritten by the encryption code. So the effective command slot is payload[4] = msg_tx_[24].
- Runtime blinds therefore send the command byte to the wrong position — the motor never receives it.

## 2. Shared constant value (state aliasing)
**File:** `elero.h` lines 54–56
- `ELERO_STATE_BOTTOM_TILT = 0x0f` and `ELERO_STATE_OFF = 0x0f` are identical
- `ELERO_STATE_ON = 0x10` sits just above the state table end
- `EleroCover::set_rx_state()` handles 0x0f as BOTTOM_TILT (correct for cover)
- `EleroLight::set_rx_state()` handles 0x0f as OFF (correct for light)
- But `elero_state_to_string()` returns "bottom_tilt" for 0x0f, even in light context — misleading in logs/text sensor
- No constant `ELERO_STATE_UNKNOWN = 0x00` case handled in `set_rx_state()` — falls to default

## 3. Blocking wait inside loop() call chain
**File:** `elero.cpp` wait_rx/wait_tx/wait_tx_done/wait_idle
- Each loops up to 200 × 200µs = 40ms
- send_command → transmit calls wait_idle + wait_tx + wait_tx_done = up to 120ms worst case
- This runs from EleroCover::loop() and EleroLight::loop() on every command send
- Starves all other ESPHome tasks during that window

## 4. wait_tx_done() polls received_ flag (wrong semantic)
- `wait_tx_done()` waits for `this->received_` to become true
- This is set by the GDO0 falling-edge ISR
- GDO0 is configured as signal 0x06 ("asserts when sync word sent; de-asserts at end of packet")
- Problem: if a packet was already received before TX started, the stale flag could satisfy the wait immediately
- The code does clear `received_ = false` just before STX — so it is protected in the normal path
- However if an ISR fires between that clear and the STX strobe, wait_tx_done returns immediately with a false positive
- This is a narrow but real race condition

## 5. JSON generation without sanitisation
**File:** elero_web_server.cpp handle_adopt_discovered, handle_get_configured, etc.
- blind name and device name are inserted into JSON strings without escaping
- A blind name containing a double-quote or backslash will produce malformed JSON
- The log entry handler does escape — but adopt/configured/runtime handlers do not
