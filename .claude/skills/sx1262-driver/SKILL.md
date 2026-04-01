---
name: sx1262-driver
description: SX1262 radio driver reference for Heltec WiFi LoRa 32 V4 — register config, GFSK TX/RX, errata fixes, CC1101 interop, FEM PA control. Use when debugging SX1262 TX/RX, modifying radio config, or comparing against reference implementations.
---

# SX1262 Radio Driver Reference

Use this skill when working on `sx1262_driver.{h,cpp}` or debugging SX1262 radio behavior. Covers:
- SX1262 GFSK configuration for CC1101 interoperability
- Errata fixes and their purpose
- TX/RX sequences and timing
- FEM PA control for Heltec V4
- Reference implementation comparison (RadioLib, Heltec, StuartsProjects)

**Source files:**
- `components/elero/sx1262_driver.{h,cpp}` — our driver
- `components/elero/radio_driver.h` — abstract interface
- `config.heltec-lora-v4.yaml` — board config

**Reference implementations:**
- [RadioLib SX126x](https://github.com/jgromes/RadioLib/tree/master/src/modules/SX126x) — best GFSK reference
- [Heltec ESP32](https://github.com/HelTecAutomation/Heltec_ESP32/tree/master/src/driver) — board-specific HAL
- [StuartsProjects SX12XX-LoRa](https://github.com/StuartsProjects/SX12XX-LoRa) — LoRa-only, no GFSK packet mode

---

## Hardware: Heltec WiFi LoRa 32 V4

### Pin Mapping

| Signal | GPIO | Notes |
|--------|------|-------|
| SPI CLK | 9 | |
| SPI MOSI | 10 | |
| SPI MISO | 11 | |
| SPI CS (NSS) | 8 | |
| DIO1 (IRQ) | 14 | Rising edge for RX_DONE/TX_DONE |
| BUSY | 13 | HIGH when radio is processing |
| RST | 12 | Active LOW reset |
| VEXT_ENABLE | 36 | Active LOW — powers OLED + LoRa PA. Must be LOW for TX. |
| FEM PA TX_EN | 46 | GC1109 variant TX enable (HIGH = TX, LOW = RX) |

### FEM PA Variants (Heltec V4)

The V4 has three hardware variants selected at compile time:

| Variant | Define | PA Pins | Notes |
|---------|--------|---------|-------|
| No FEM | `USE_NONE_PA` | None | Bare SX1262, no external PA |
| GC1109 | `USE_GC1109_PA` | GPIO7 (power), GPIO2 (enable), GPIO46 (TX) | Our config uses GPIO46 only |
| KCT8103L | `USE_KCT8103L_PA` | GPIO7 (power), GPIO2 (CSD), GPIO5 (CTX) | Different TX enable pin |

**Current approach:** GPIO36 (VEXT) enables PA power on boot. GPIO46 toggled per TX. This works for both CW and GFSK on our board.

### DIO2 as RF Switch

`rf_switch: true` in config → `SetDio2AsRfSwitchCtrl(true)`. The SX1262 chip itself controls the RF switch via DIO2 — no GPIO needed from ESP32.

### TCXO

DIO3 controls the TCXO at 1.8V (`tcxo_voltage: 1.8`). Must be configured before calibration.

---

## Elero GFSK Parameters

These parameters must match the CC1101 configuration exactly for interoperability.

| Parameter | CC1101 Register | CC1101 Value | SX1262 Value | Notes |
|-----------|----------------|--------------|--------------|-------|
| **Bit rate** | MDMCFG4=0x7B, MDMCFG3=0x83 | 76,766 baud | BR=13337 (0x003419) | `32 * 32e6 / 76766` |
| **Deviation** | DEVIATN=0x43 | 34,912 Hz | FDEV=36602 (0x008EEA) | `(34912 * 2^25) / 32e6` |
| **RX BW** | MDMCFG4[7:4]=0x7 | 232 kHz | 0x0A (234.3 kHz) | Must be >= 2*dev + bitrate |
| **Modulation** | MDMCFG2[6:4]=001 | GFSK | 0x09 (BT=0.5) | CC1101 GFSK uses BT≈0.5 |
| **Preamble** | MDMCFG1[6:4]=101 | 12 bytes (96 bits) | 0x0060 | Must match for AGC/AFC lock |
| **Sync word** | SYNC1=0xD3, SYNC0=0x91 | D3 91 D3 91 (32 bits) | 32 bits at reg 0x06C0 | **CRITICAL: CC1101 SYNC_MODE=011 doubles the 16-bit sync word** |
| **Whitening** | PKTCTRL0[6]=1 | PN9 (IBM) | **OFF** (software) | SX1262 whitening incompatible |
| **CRC** | PKTCTRL0[2]=1 | CRC-16/IBM | **OFF** (software) | 0x8005, init 0xFFFF |
| **Pkt format** | PKTCTRL0[1:0]=01 | Variable length | Fixed length | SX1262 can't decode whitened length |

### Why Software Whitening + CRC

The SX1262's hardware "whitening" is **NOT IBM PN9** — it's a data-dependent scrambler that produces different output than CC1101's PN9. Proven by:
- Correction XOR between SX1262 and CC1101 output changes with packet content
- No 9-bit LFSR (any seed/taps) generates the empirical sequence
- NXP AN5070 "CCITT = reverse(IBM)" model does NOT match

**Solution:** Hardware whitening OFF, software IBM PN9 applied to the raw buffer.

Similarly, the SX1262's hardware CRC (CCITT, poly 0x1021, init 0x1D0F) doesn't match CC1101's CRC (IBM, poly 0x8005, init 0xFFFF). CRC computed in software.

### 32-Bit Sync Word (CRITICAL — Root Cause of TX Failure)

**CC1101 SYNC_MODE=011 (30/32 bits) doubles the 16-bit sync word internally.**

The CC1101 both transmits and expects a **32-bit** sync word: `D3 91 D3 91` (SYNC1+SYNC0 repeated twice). A 16-bit sync word from the SX1262 only matches 16/32 bits — far below the 30/32 threshold, causing **zero sync detection**.

**SX1262 config must use 32-bit sync:**
```cpp
// Sync word register: D3 91 repeated twice
uint8_t sync_word[8] = {0xD3, 0x91, 0xD3, 0x91, 0x00, 0x00, 0x00, 0x00};
write_register(0x06C0, sync_word, 8);

// Packet params: sync word length = 32 bits (0x20), not 16 (0x10)
pkt_params[3] = 0x20;
```

**This also explained the 2-byte "sync prefix" in RX:** With 16-bit sync, the SX1262 matched on the FIRST `D3 91` of the CC1101's 32-bit sync word. The SECOND `D3 91` copy passed through into the RX buffer as data. With 32-bit sync, the full sync word is stripped and the buffer starts directly with the whitened payload.

**Symptoms of wrong sync length:**
- CW test works (no sync word involved)
- GFSK preamble produces RSSI spike on CC1101 (RF energy visible)
- CC1101 never fires GDO0 interrupt (no sync match → no packet)
- All data encoding variations fail (whitened, raw, inverted — doesn't matter)
- SX1262 reports TX_DONE correctly (it DID transmit)

### PN9 Whitening Algorithm

```cpp
// CC1101 IBM PN9: polynomial x^9 + x^5 + 1, seed 0x1FF
// Taps at bit 0 XOR bit 5 (NOT bit 4!)
// Right-shifting LFSR, output assembled LSB-first
uint16_t key = 0x1FF;
for (size_t i = 0; i < len; ++i) {
    data[i] ^= key & 0xFF;
    for (int j = 0; j < 8; ++j) {
        uint16_t msb = ((key >> 5) ^ (key >> 0)) & 1;
        key = (key >> 1) | (msb << 8);
    }
}
```

First 8 PN9 bytes: `FF E1 1D 9A ED 85 33 24`

### CC1101 CRC-16

```cpp
// Polynomial 0x8005, init 0xFFFF, MSB-first processing
// Scope: length byte + all data bytes (before whitening)
// Byte order: MSB first in packet
uint16_t crc = 0xFFFF;
for (each byte) {
    crc ^= byte << 8;
    for (8 bits) {
        crc = (crc & 0x8000) ? (crc << 1) ^ 0x8005 : crc << 1;
    }
}
```

### Frequency Conversion

```
CC1101: freq_hz = 26e6 * FREQ[23:0] / 2^16
SX1262: freq_hz = 32e6 * RfFreq / 2^25

Combined: RfFreq = CC1101_FREQ * 26 * 2^9 / 32 = CC1101_FREQ * 416 (exact integer)

868.35 MHz: CC1101 FREQ = 0x21717A → SX1262 RfFreq = 0x21717A * 416
```

---

## SX1262 Errata Fixes (CRITICAL)

These must be applied for GFSK TX to work. RadioLib applies all of them.

### 1. PA Clamping (register 0x08D8)

**Problem:** SX1262 PA clamping circuit can reduce output power at >18 dBm.
**Fix:** RadioLib `fixPaClamping()` — set bits [4:2] based on power level.

```cpp
uint8_t clamp_cfg;
read_register(0x08D8, &clamp_cfg, 1);
if (power > 18) {
    clamp_cfg |= 0x1C;                      // bits [4:2] = 111, disable clamping
} else {
    clamp_cfg = (clamp_cfg & 0xE3) | 0x18;  // bits [4:2] = 110, default clamping
}
write_register(0x08D8, &clamp_cfg, 1);
```

**When:** During init AND before every TX.

### 2. Sensitivity/Modulation (register 0x0889, bit 2)

**Problem:** Datasheet section 15.1. Affects modulation quality for GFSK.
**Fix:** RadioLib `fixSensitivity()` — set bit 2 to 1 for all non-LoRa-500kHz modes.

```cpp
uint8_t sens_cfg;
read_register(0x0889, &sens_cfg, 1);
sens_cfg |= 0x04;  // Set bit 2
write_register(0x0889, &sens_cfg, 1);
```

**When:** During init AND before every TX.

### 3. GFSK Undocumented Registers (RadioLib fixGFSK)

Four undocumented registers must be set for correct FSK operation at non-special bitrates:

| Register | Read-modify-write | Purpose |
|----------|-------------------|---------|
| 0x06D1 | `(val & 0xE7) \| 0x08` | bits[4:3] = 01 |
| 0x089B | `val & 0xE3` | bits[4:2] = 000 |
| 0x08B8 | `(val & 0xEF) \| 0x10` | bit[4] = 1 |
| 0x06AC | `val & 0x8F` | bits[6:4] = 000 |

**When:** Once during `configure_fsk_()`.

---

## TX Sequence (load_and_transmit)

Our sequence, aligned with RadioLib's `stageMode(TX)` + `launchMode()`:

```
1. set_standby_(STDBY_XOSC)
2. Build TX data:
   a. Copy raw packet [length | data...]
   b. Compute CC1101 CRC-16 over [length + data]
   c. Append 2 CRC bytes (MSB first)
   d. PN9 whiten everything [length + data + CRC]
3. set_pa_config_()          — PA duty cycle, hpMax, deviceSel
4. fix_pa_clamping_()        — errata: reg 0x08D8
5. SET_TX_PARAMS             — power + ramp time
6. fix_sensitivity_()        — errata: reg 0x0889
7. SET_PACKET_PARAMS         — 96-bit preamble, 16-bit sync, fixed len, CRC OFF, whitening OFF
8. SET_BUFFER_BASE_ADDRESS   — reset to (0, 0)
9. WRITE_BUFFER              — write whitened data at offset 0
10. CLR_IRQ_STATUS           — clear ALL flags (prevents stale DIO1)
11. Clear atomic IRQ flag
12. SET_DIO_IRQ_PARAMS       — TX_DONE | TIMEOUT on DIO1
13. FEM PA enable (GPIO46 HIGH)
14. SET_TX(0x000000)         — single TX, no timeout (we handle timeout in poll_tx)
```

**Key differences from RadioLib:**
- We do software CRC + whitening (RadioLib uses hardware)
- We manage FEM PA GPIO explicitly
- We use an async model (poll_tx instead of blocking wait)

### PA Configuration

```cpp
// SX1262 PA config for +22 dBm max
// paDutyCycle=0x04, hpMax=0x07, deviceSel=0x00 (SX1262), paLut=0x01
uint8_t pa_config[4] = {0x04, 0x07, 0x00, 0x01};
SET_PA_CONFIG(pa_config);

// TX power + ramp
uint8_t tx_params[2] = {power_dbm, 0x04};  // PA_RAMP_200US
SET_TX_PARAMS(tx_params);
```

RadioLib's optimized PA lookup table maps power -9..+22 dBm to optimal `{paDutyCycle, hpMax, paVal}` entries. We use a single config for max power. If power reduction is needed later, use the lookup table from RadioLib `SX1262.cpp`.

---

## RX Sequence

### RX Configuration

```
1. SET_DIO_IRQ_PARAMS — RX_DONE | TIMEOUT on DIO1
2. SET_RX(0xFFFFFF)   — continuous RX, no timeout
```

### RX Buffer Format (whitening OFF, 32-bit sync, fixed-length mode)

With 32-bit sync word (D3 91 D3 91), the SX1262 strips the full sync word. The buffer starts directly with the whitened payload — no sync prefix to skip.

```
Buffer: [whitened_length] [whitened_data...] [padding to RX_FIXED_LEN]
```

**RX_FIXED_LEN = 32** — must cover the largest Elero packet (length 0x1E = 30 data + 1 length byte = 31 bytes). Do NOT reduce to 30 — status packets (0xCA) can be up to 31 bytes and would be truncated.

Processing:
1. Apply PN9 de-whitening to the buffer
2. First de-whitened byte = Elero packet length (0x1B-0x1E)
3. Read `length` data bytes + synthesize RSSI/LQI from SX1262 packet status

### RSSI Conversion (SX1262 → CC1101 format)

```cpp
// SX1262 RSSI: pkt_status[2] = -rssi_dbm * 2
int rssi_dbm_x2 = -static_cast<int>(pkt_status[2]);
// CC1101 RSSI format: unsigned byte, offset +74 dBm, divided by 2
int cc1101_rssi = rssi_dbm_x2 + 148;
if (cc1101_rssi < 0) cc1101_rssi += 256;
```

---

## Init Sequence

Follows RadioLib's proven order:

```
 1. Hardware reset (RST pin LOW → HIGH)
 2. SET_STANDBY(STDBY_RC)
 3. SET_DIO3_AS_TCXO_CTRL(1.8V, 10ms timeout)
 4. CLR_DEVICE_ERRORS
 5. SET_BUFFER_BASE_ADDRESS(0, 0)
 6. SET_PACKET_TYPE(GFSK)
 7. SET_RX_TX_FALLBACK_MODE(STDBY_RC)
 8. CLR_IRQ_STATUS + disable all DIO routing
 9. CALIBRATE(0x7F) — all blocks
10. SET_REGULATOR_MODE(DC-DC)
11. SET_DIO2_AS_RF_SWITCH(true)
12. configure_fsk_() — modulation + packet params + sync word + fixGFSK registers
13. SET_RF_FREQUENCY
14. CALIBRATE_IMAGE(0xD7, 0xDB) — 850-900 MHz band
15. SET_PA_CONFIG + fix_pa_clamping_()
16. SET_TX_PARAMS + fix_sensitivity_()
17. OCP register (0x38 = 140mA)
18. RX gain boost (0x96 at reg 0x08AC)
19. SET_DIO_IRQ for RX
20. SET_RX(continuous)
```

---

## SPI Protocol

| Operation | Opcode | Format |
|-----------|--------|--------|
| Write opcode | varies | `[opcode] [param0] [param1] ...` |
| Read opcode | varies | `[opcode] [NOP] → [status] [data0] [data1] ...` |
| Write register | 0x0D | `[0x0D] [addr_hi] [addr_lo] [data0] [data1] ...` |
| Read register | 0x1D | `[0x1D] [addr_hi] [addr_lo] [NOP] → [status] [data0] ...` |
| Write buffer | 0x0E | `[0x0E] [offset] [data0] [data1] ...` (NO NOP after offset) |
| Read buffer | 0x1E | `[0x1E] [offset] [NOP] → [status] [data0] [data1] ...` |

**BUSY pin:** Must wait for BUSY LOW before every SPI transaction. Timeout: 20ms.

**SPI clock:** 8 MHz (SX1262 supports up to 16 MHz).

---

## IRQ Handling

| IRQ Bit | Value | Used For |
|---------|-------|----------|
| TX_DONE | 0x0001 | TX completion |
| RX_DONE | 0x0002 | Packet received |
| PREAMBLE_DETECTED | 0x0004 | NOT used (causes spurious ISR) |
| SYNCWORD_VALID | 0x0008 | NOT used (causes spurious ISR) |
| CRC_ERROR | 0x0040 | NOT used (CRC in software) |
| TIMEOUT | 0x0200 | RX/TX timeout |

**RX mode:** DIO1 mask = RX_DONE | TIMEOUT
**TX mode:** DIO1 mask = TX_DONE | TIMEOUT

**Critical:** Clear `CLR_IRQ_STATUS(0xFFFF)` before switching between RX and TX DIO masks. Stale IRQ flags can hold DIO1 HIGH, preventing rising-edge detection of new events.

---

## Health Check

The SX1262 has no CC1101-style MARCSTATE. Health is checked by reading the status byte:

| chip_mode | Meaning | Action |
|-----------|---------|--------|
| 0x02 | STBY_RC | Stuck — needs recover |
| 0x03 | STBY_XOSC | Stuck — needs recover |
| 0x04 | FS | Stuck — needs recover |
| 0x05 | RX | OK (expected) |
| 0x06 | TX | OK (transient) |

Recovery: standby → clear IRQ → set DIO for RX → enter RX.

---

## Critical Lessons: CC1101 ↔ SX1262 Interop (solved 2026-03-28)

Everything below was discovered empirically. None of it is documented in either datasheet.

### 1. Sync Word Must Be 32 Bits (ROOT CAUSE OF TX FAILURE)

CC1101 `MDMCFG2` SYNC_MODE=011 means "30/32 sync word bits." The CC1101 internally **doubles** its 16-bit sync word (SYNC1+SYNC0) to form a 32-bit pattern: `D3 91 D3 91`. It transmits 32 bits and expects 32 bits in return.

The SX1262 with `sync_length=16` only sent `D3 91` (16 bits). The CC1101 correlator matched 16/32 = 50% — far below the 30/32 (93.75%) threshold. **Zero sync detection.**

**Fix:** `sync_length=0x20` (32 bits), register 0x06C0 = `{D3, 91, D3, 91, 00, 00, 00, 00}`.

**This also explained the "sync prefix" in RX:** The SX1262's 16-bit sync detector matched the FIRST `D3 91` of the CC1101's 32-bit sync. The SECOND `D3 91` leaked into the RX buffer as data, requiring a 2-byte skip. With 32-bit sync, the full sync is stripped cleanly.

**This also explained the "1-bit LSB error":** The bit alignment error at the sync/data boundary was caused by the SX1262 starting data capture mid-sync-word. Gone with 32-bit sync.

### 2. SX1262 Hardware Whitening Is Incompatible With CC1101

The SX1262's built-in "whitening" is **NOT IBM PN9**. It's a data-dependent scrambler that produces completely different output. Proven by:
- XOR correction pattern changes with packet content (PN9 is content-independent)
- No 9-bit LFSR with any seed/taps matches the empirical sequence
- NXP AN5070 "CCITT = reverse(IBM)" model doesn't match either

**Fix:** SX1262 hardware whitening OFF (`pkt_params[8]=0x00`), software IBM PN9 applied to the buffer. Same `whiten_fix_()` function works for both TX (whiten) and RX (de-whiten) since XOR is self-inverse.

### 3. SX1262 Hardware CRC Is Incompatible With CC1101

CC1101 uses CRC-16/IBM: polynomial 0x8005, init 0xFFFF, MSB-first.
SX1262 uses CRC-16/CCITT: polynomial 0x1021, init 0x1D0F, inverted.

**Fix:** SX1262 hardware CRC OFF (`pkt_params[7]=0x01`), software `cc1101_crc16()` computes and appends CRC before whitening. CRC scope: length byte + all data bytes (per CC1101 variable-length mode). CRC bytes appended MSB first.

### 4. Fixed-Length Mode Required (SX1262 Can't Decode Whitened Length)

CC1101 variable-length mode: first byte after sync = length byte, used to determine packet size. With whitening ON, this byte is whitened. The CC1101 de-whitens it internally before using it.

The SX1262 with hardware whitening OFF receives the raw whitened byte. It can't interpret it as a length for variable-length mode.

**Fix:** SX1262 uses fixed-length mode (`pkt_params[5]=0x00`) with `RX_FIXED_LEN=32` bytes. Software de-whitens the buffer and reads the length byte manually. **Do NOT reduce RX_FIXED_LEN below 32** — status packets (0xCA) can be up to 31 bytes.

### 5. Preamble Must Match CC1101 (96 Bits = 12 Bytes)

CC1101 `MDMCFG1` NUM_PREAMBLE=101 → 12 bytes of preamble (96 bits). The CC1101 receiver uses preamble for AGC settling and AFC convergence. Short preamble (e.g., 32 bits) may prevent the CC1101 from locking onto the signal, especially with any frequency offset.

**Fix:** `pkt_params[0:1] = 0x00, 0x60` (96 bits) in all packet param configurations (init, TX, RX restore).

### 6. Three SX1262 Errata Fixes Required

All discovered by comparing against RadioLib reference implementation.

| Errata | Register | Fix | When |
|--------|----------|-----|------|
| PA clamping | 0x08D8 | bits[4:2]=111 for power >18 dBm | Init + every TX |
| Modulation quality | 0x0889 | bit 2 = 1 for GFSK | Init + every TX |
| GFSK undocumented | 0x06D1, 0x089B, 0x08B8, 0x06AC | Read-modify-write per RadioLib | Init only |

Without PA clamping fix: output power silently reduced at >18 dBm.
Without modulation quality fix: GFSK signal quality degraded.
Without GFSK undocumented fixes: FSK operation incorrect at non-standard bitrates.

### 7. IRQ Must Be Cleared Before TX

Stale IRQ flags (e.g., RX_DONE from a previous reception) can hold DIO1 HIGH. When TX completes, TX_DONE sets DIO1 HIGH — but if it's already HIGH, no rising edge occurs and the ISR never fires. Result: TX timeout despite successful transmission.

**Fix:** `CLR_IRQ_STATUS(0xFFFF)` + clear atomic flag before every `SET_TX`.

### 8. Elero Bitrate Is 76.8 kbaud (NOT 9.6)

Early documentation said 9.6 kbaud. The actual CC1101 registers (MDMCFG4=0x7B, MDMCFG3=0x83) produce 76,766 baud. Getting this wrong means the SX1262 can't demodulate CC1101 packets at all.

**SX1262 BitRate register:** `BR = 32 * 32e6 / 76766 = 13337` (0x003419).

### 9. Frequency Conversion Is Exact Integer Math

CC1101 freq = `26e6 * FREQ / 2^16`. SX1262 freq = `32e6 * RfFreq / 2^25`.

Combined: `RfFreq = CC1101_FREQ * 416` (exact integer, no floating point needed).

Both radios share the same `freq0/freq1/freq2` config values. The conversion preserves exact frequency alignment.

### 10. SetTxInfinitePreamble (0xD2) Crashes on Some SX1262 Revisions

Opcode 0xD2 causes `IllegalInstruction` exception on the Heltec V4's SX1262. Use `SetTxContinuousWave` (0xD1) for RF path testing instead, or use regular `SetTx` with large preamble count.

---

## Known Issues / Quirks

1. **~~1-bit LSB error at first data byte~~ RESOLVED** — Was caused by 16-bit sync word. The SX1262 matched on the first D3 91 of the CC1101's 32-bit sync, and the second copy leaked into the buffer with a 1-bit alignment error. Fixed by using 32-bit sync word.

2. **Status responses (0xCA) from blinds rarely received** — May be timing or sensitivity. Remote commands (0x6A, 0x44) decode fine.

3. **Device errors opcode FIXED** — Now uses `GET_DEVICE_ERRORS` (0x17) for reading, `CLR_DEVICE_ERRORS` (0x07) for clearing.

4. **SX1262 whitening seed register** — Register 0x06B8 MSB: only bit 0 is writable, bits [7:1] are reserved. Always read-modify-write: `(reg & 0xFE) | (seed_bit8)`.

5. **Heltec V4 FEM PA** — Only GPIO46 (TX enable) is controlled in our driver. GPIO7 (PA power) and GPIO2 (PA enable) may need separate control on some board revisions. The VEXT pin (GPIO36, active LOW) appears to serve as the power enable for the PA circuit.

---

## SX1262 vs CC1101: Robustness Comparison

The CC1101 driver has extensive defensive mechanisms for packet loss prevention. Most are CC1101-specific hardware workarounds that don't apply to the SX1262 — the SX1262's architecture naturally eliminates those failure modes.

### Why SX1262 Is Inherently More Robust

| CC1101 Problem | CC1101 Workaround | Why SX1262 Doesn't Need It |
|---|---|---|
| 64-byte FIFO overflow | Detect overflow bit, flush FIFOs | SX1262 has 256-byte buffer. At 32-byte fixed-length, overflow is impossible |
| RXBYTES register returns stale values | `read_status_reliable_()` double-read | SX1262 has BUSY pin that serializes all SPI access — no stale reads |
| GDO0 interrupt is ambiguous (shared for RX/TX) | MARCSTATE polling fallback, TXBYTES verification | SX1262 DIO1 has separate IRQ flags: TX_DONE (0x0001) and RX_DONE (0x0002) — unambiguous |
| SPI transaction can return overflow in status byte | Check status byte on every `write_reg()` | SX1262 BUSY pin prevents operations during processing. No equivalent overflow leak |
| Radio stuck in unexpected MARCSTATE | Detailed state machine with transient state handling | SX1262 has 5 chip modes (STDBY_RC/XOSC, FS, RX, TX). Simpler, fewer stuck states |
| Separate RX/TX FIFOs can get out of sync | `flush_and_rx()` flushes both + verifies | SX1262 uses single 256-byte buffer. `set_standby_()` + `set_rx_()` is sufficient |

### What We Carried Over

| Feature | CC1101 | SX1262 | Notes |
|---|---|---|---|
| **Core 0 RF task** | ✅ | ✅ | Same `rf_task_func_` drives both via RadioDriver interface |
| **Echo detection** | ✅ | ✅ | Shared code in `elero.cpp` — TX ring buffer checked on all RX |
| **Recovery escalation** | ✅ Soft (IDLE→SRX) then hard (reset+init) | ✅ Soft (standby→set_rx), verify chip_mode, escalate to full `reset()`+`init()` | Added 2026-03-28 |
| **Health check watchdog** | ✅ Every 5s, reads MARCSTATE | ✅ Every 5s, reads chip_mode + device errors | Device errors read with correct opcode (0x17) |
| **TX timeout** | ✅ 50ms, escalates to RECOVER state | ✅ 50ms, restores RX params | SX1262 TX_DONE IRQ is reliable, timeout is safety net only |
| **Diagnostic counters** | ✅ overflow, watchdog, tx_recover | ✅ watchdog, tx_recover (overflow always 0) | Overflow counter returns 0 — not a real failure mode |

### What We Intentionally Skipped

| CC1101 Feature | Why Skipped |
|---|---|
| `read_status_reliable_()` double-read | CC1101-specific errata (RXBYTES/TXBYTES can return previous value). SX1262 doesn't have this bug |
| SPI status byte overflow checks | CC1101 leaks RXFIFO overflow through SPI status. SX1262 uses BUSY pin — if BUSY is low, SPI is safe |
| TXBYTES verification after TX | CC1101 GDO0 can fire before FIFO fully drains (grace window needed). SX1262 TX_DONE means TX is truly done |
| Transient MARCSTATE handling | CC1101 has calibration/settling states that are normal but look like errors. SX1262 has clean state transitions |
| `flush_and_rx()` post-verification | CC1101 needs to verify MARCSTATE==RX after flush. SX1262 `set_rx_()` is atomic — verified in `recover()` escalation |

---

## Simplification Opportunities

### From Reference Implementations

1. **RadioLib is the gold standard** for GFSK — use it as primary reference for any TX/RX changes. It handles all errata, all modes, and has been battle-tested.

2. **Heltec library** — useful only for board-specific FEM PA control and pin mappings. It has FEM power adjustment curves (`0.0004*x^3 - 0.011*x^2 + 1.0866*x - 11.365` for GC1109) that could be adopted if we need precise output power control.

3. **StuartsProjects** — LoRa only, no GFSK packet mode at all. Not useful for Elero. Only demonstrates CW + frequency-shift pseudo-FSK.

4. **ESPHome built-in sx126x** — ESPHome has an `sx126x` component in `.venv/.../esphome/components/sx126x/`. Could potentially replace our custom driver if it supports raw GFSK packet mode with software whitening/CRC. Worth investigating for long-term maintenance reduction.

### Potential Driver Simplifications

- **PA config:** Currently re-applied before every TX. If RadioLib-style optimized PA lookup table is adopted, each power level gets optimal `{paDutyCycle, hpMax}` instead of always using max.
- **Errata fixes:** Currently called before every TX (matching RadioLib). Could potentially be called once during init if registers are proven stable across RX/TX transitions. But RadioLib's approach is safest.
- **Buffer base address:** Currently reset before every TX. Probably unnecessary since it persists, but costs only 1 SPI transaction (~10us).
