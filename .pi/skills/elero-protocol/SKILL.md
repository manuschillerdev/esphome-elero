---
name: elero-protocol
description: Complete RF protocol reference for Elero wireless blinds - packet structure, encryption, commands, state constants. Use when working with RF packets, encryption/decryption, command bytes, CC1101 configuration, or protocol debugging.
---

# Elero RF Protocol Reference

Use this skill when working on the esphome-elero codebase and you need information about:
- RF packet structure and encoding
- Encryption/decryption algorithms
- Command bytes and state constants
- CC1101 transceiver configuration

When invoked, use this reference to answer protocol questions accurately.

**Source:** [QuadCorei8085/elero_protocol](https://github.com/QuadCorei8085/elero_protocol) (MIT License)

---

## RF Parameters

| Parameter | Value |
|-----------|-------|
| Frequency | 868.35 MHz (EU) or 868.95 MHz (variant) |
| Modulation | FSK via CC1101 transceiver |
| Data Rate | ~9.6 kbaud |
| Sync Word | Configured in CC1101 registers |

### CC1101 Frequency Registers

```
868.35 MHz: FREQ2=0x21, FREQ1=0x71, FREQ0=0x7A
868.95 MHz: FREQ2=0x21, FREQ1=0x71, FREQ0=0xC0
```

---

## Message Structure

### Packet Format (27-30 bytes)

| Offset | Length | Field | C++ Offset Constant | Description |
|--------|--------|-------|---------------------|-------------|
| 0 | 1 | `pck_len` | `pkt_offset::LENGTH` | Packet length (0x1B=27..0x1E=30) |
| 1 | 1 | `counter` | `pkt_offset::COUNTER` | Rolling counter (increments each TX) |
| 2 | 1 | `msg_type` | `pkt_offset::TYPE` | Message type (0x44/0x6A/0xCA/0xC9) |
| 3 | 1 | `type2` | `pkt_offset::TYPE2` | Secondary type (usually 0x00) |
| 4 | 1 | `hop` | `pkt_offset::HOP` | Hop count (usually 0x0a) |
| 5 | 1 | `syst` | `pkt_offset::SYS` | System address (fixed: 0x01) |
| 6 | 1 | `channel` | `pkt_offset::CHANNEL` | RF channel number |
| 7-9 | 3 | `src_addr` | `pkt_offset::SRC_ADDR` | Source address (big-endian) |
| 10-12 | 3 | `bwd_addr` | `pkt_offset::BWD_ADDR` | Backward address (same as src) |
| 13-15 | 3 | `fwd_addr` | `pkt_offset::FWD_ADDR` | Forward address (same as src) |
| 16 | 1 | `dest_count` | `pkt_offset::NUM_DESTS` | Destination count (0x01) |
| 17-19 | 3 | `dst_addr` | `pkt_offset::FIRST_DEST` / `tx_offset::DST_ADDR` | Destination address (big-endian) |
| 20+ | var | `payload` | `tx_offset::PAYLOAD` | Encrypted payload (type-dependent) |

**Additional constants in `packet::` namespace:**

| Constant | Value | Description |
|----------|-------|-------------|
| `ADDR_SIZE` | 3 | Bytes per 24-bit address |
| `CC1101_APPEND_SIZE` | 2 | RSSI + LQI bytes appended by CC1101 |
| `PACKET_TOTAL_OVERHEAD` | 3 | Length byte + RSSI + LQI (for buffer validation) |

### Field Naming Consistency

The codebase uses consistent naming across all layers:

| Protocol | YAML Config | C++ Struct | JSON Log | Description |
|----------|-------------|------------|----------|-------------|
| dst_addr | `dst_address` | `dst_addr` | `"dst"` | Target blind/light address |
| src_addr | `src_address` | `src_addr` | `"src"` | Remote control address |
| msg_type | `pck_inf1` | `type` | `"type"` | Message type (0x6a, 0xca, etc.) |
| type2 | `pck_inf2` | `type2` | `"type2"` | Secondary type (usually 0x00) |
| hop | `hop` | `hop` | `"hop"` | Hop count (usually 0x0a) |

### Message Types

| Type | Hex | Direction | Description |
|------|-----|-----------|-------------|
| Button / selector | 0x44 | Remote → Blind | Button press/release with one-byte channel selector |
| Group/list selector | 0x45 | Remote → Blind | Group/central selector-list envelope |
| Command | 0x6A | Controller/Remote → Blind | Targeted command with 3-byte destination address |
| Command Alt | 0x69 | Controller/Remote → Blind | Alternate targeted command envelope |
| Program selector | 0x70 | Remote → Blind | Single-channel programming/P envelope with one-byte selector |
| Status Response | 0xCA | Blind → Controller | Status with 3-byte address |
| Status Response Alt | 0xC9 | Blind → Controller | Alternate status format |

**Envelope rule:** classify packet shape explicitly. Selector envelopes (`0x44`, `0x45`, `0x70`) use one-byte destinations even though `0x70 > 0x60`. Address envelopes (`0x6A`, `0x69`, `0xCA`, `0xC9`) use 3-byte addresses. Do not rely on a blanket `type > 0x60` rule.

---

## Payload Structure

### Selector packets (`0x44`, `0x45`, `0x70`)

```
Offset 16:            num_dests
Offset 17..17+N-1:    N one-byte selectors/channels
Offset 17+N:          payload prefix byte 1
Offset 18+N:          payload prefix byte 2
Offset 19+N..26+N:    encrypted payload (8 bytes)
```

Canonical single-channel packets use `N=1`, selector equal to `channel`, prefix bytes `00 03`, and length `0x1B`.

### Command packets (`0x6A`, `0x69`) - Length 0x1D (29 bytes)

```
Offset 17-19: Blind address (3 bytes, big-endian)
Offset 20:    Payload prefix byte 1 (usually 0x00)
Offset 21:    Payload prefix byte 2 (observed 0x03/0x04)
Offset 22-29: Encrypted payload (8 bytes)
```

### Status Response (`0xCA`, `0xC9`) - Length 0x1C-0x1E

```
Payload contains:
- Blind address
- State byte (see State Constants below)
- Encrypted portion
```

---

## Encryption/Decryption

The payload uses a simple obfuscation scheme: **nibble substitution + XOR + arithmetic addition**.

### Lookup Tables

```c
// Encoding table (plaintext nibble → encoded nibble)
const uint8_t flash_table_encode[] = {
    0x08, 0x02, 0x0d, 0x01, 0x0f, 0x0e, 0x07, 0x05,
    0x09, 0x0c, 0x00, 0x0a, 0x03, 0x04, 0x0b, 0x06
};

// Decoding table (encoded nibble → plaintext nibble)
const uint8_t flash_table_decode[] = {
    0x0a, 0x03, 0x01, 0x0c, 0x0d, 0x07, 0x0f, 0x06,
    0x00, 0x08, 0x0b, 0x0e, 0x09, 0x02, 0x05, 0x04
};
```

### Encryption Algorithm (encode_msg)

```c
void msg_encode(uint8_t* msg, uint8_t xor0, uint8_t xor1) {
    // 1. Calculate parity bits
    calc_parity(msg);  // Sets msg[7] based on bit parity of pairs

    // 2. Add r20 to nibbles (starting at 0xFE, decrement by 0x22 each byte)
    add_r20_to_nibbles(msg, 0xFE, 0, 8);

    // 3. XOR bytes 2-7 with the code bytes (skip first pair)
    xor_2byte_in_array_encode(msg, xor0, xor1);

    // 4. Substitute nibbles through encoding table
    encode_nibbles(msg);
}
```

### Decryption Algorithm (decode_msg)

```c
void msg_decode(uint8_t* msg) {
    // 1. Decode nibbles through lookup table
    decode_nibbles(msg, 8);

    // 2. Subtract r20 from first 2 bytes (start 0xFE)
    sub_r20_from_nibbles(msg, 0xFE, 0, 2);

    // 3. XOR all 8 bytes with decoded bytes 0,1
    xor_2byte_in_array_decode(msg, msg[0], msg[1]);

    // 4. Subtract r20 from bytes 2-7 (start 0xBA)
    sub_r20_from_nibbles(msg, 0xBA, 2, 8);
}
```

### Rolling Code Calculation

The XOR key is derived from the message counter:

```c
uint16_t code = (0x0000 - (counter * 0x708F)) & 0xFFFF;
uint8_t xor0 = (code >> 8) & 0xFF;  // High byte
uint8_t xor1 = code & 0xFF;         // Low byte
```

The first two bytes of the plaintext payload contain this code.

### Parity Calculation

```c
void calc_parity(uint8_t* msg) {
    uint8_t p = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t a = count_bits(msg[i*2]);      // Bit count of even byte
        uint8_t b = count_bits(msg[i*2 + 1]);  // Bit count of odd byte
        p |= (a ^ b);
        p <<= 1;
    }
    msg[7] = (p << 3);  // Parity stored in byte 7, upper nibble
}
```

---

## Command Bytes

### Cover Commands (payload byte after XOR code)

| Command | Hex | Description |
|---------|-----|-------------|
| CHECK | 0x00 | Request status |
| STOP | 0x10 | Stop movement |
| UP / OPEN | 0x20 | Move up / open |
| TILT | 0x24 | Tilt position |
| DOWN / CLOSE | 0x40 | Move down / close |
| INTERMEDIATE | 0x44 | Move to intermediate position |
| PROGRAM / P | 0x80 | Programming/P, single-channel selector envelope |
| PROGRAM / P GROUP | 0x81 | Programming/P, group/list selector envelope (RX/sniffing only) |
| PROGRAM / P TARGETED | 0x84 | Programming/P, targeted 3-byte-address envelope (RX/sniffing only) |

### Light Commands (if applicable)

| Command | Hex | Description |
|---------|-----|-------------|
| ON | 0x20 | Turn on |
| OFF | 0x40 | Turn off |
| DIM_UP | 0x20 | Start dimming up |
| DIM_DOWN | 0x40 | Start dimming down |
| STOP | 0x10 | Stop dimming |

---

## State Constants (Status Response)

| State | Hex | Description |
|-------|-----|-------------|
| UNKNOWN | 0x00 | Unknown state |
| TOP | 0x01 | Fully open (top position) |
| BOTTOM | 0x02 | Fully closed (bottom position) |
| INTERMEDIATE | 0x03 | Intermediate position |
| TILT | 0x04 | Tilted position |
| BLOCKING | 0x05 | Blocked / obstacle detected |
| OVERHEATED | 0x06 | Motor overheated |
| TIMEOUT | 0x07 | Communication timeout |
| START_MOVING_UP | 0x08 | Starting upward movement |
| START_MOVING_DOWN | 0x09 | Starting downward movement |
| MOVING_UP | 0x0A | Currently moving up |
| MOVING_DOWN | 0x0B | Currently moving down |
| STOPPED | 0x0D | Stopped (after movement) |
| TOP_TILT | 0x0E | Top position + tilted |
| BOTTOM_TILT / OFF | 0x0F | Bottom + tilt / Light off |
| ON | 0x10 | Light on |

---

## Address Format

Addresses are 3 bytes, transmitted big-endian:

```c
// Example: address 0xA831E5
uint8_t addr[3] = {0xA8, 0x31, 0xE5};

// In message at offset 7-9:
msg[7] = 0xA8;
msg[8] = 0x31;
msg[9] = 0xE5;
```

### Address Fields in Message

- **src_addr** (7-9): Source/sender address (remote)
- **bwd_addr** (10-12): Backward routing address (usually = src)
- **fwd_addr** (13-15): Forward routing address (usually = src)

For direct remote commands, all three addresses are identical. They may differ in relay/hopping scenarios.

---

## Example: Generate DOWN Command

```c
void generate_msg_down(uint8_t* msg, uint8_t* remote_addr,
                       uint8_t counter, uint8_t channel,
                       uint8_t button_pressed) {
    uint16_t code = (0x0000 - (counter * 0x708F)) & 0xFFFF;

    memset(msg, 0, 28);

    msg[0]  = 0x1B;                    // Packet length (27)
    msg[1]  = counter;                 // Rolling counter
    msg[2]  = 0x44;                    // Button press message type
    msg[3]  = 0x10;                    // Header byte 1
    msg[4]  = 0x00;                    // Header byte 2
    msg[5]  = 0x01;                    // Header byte 3
    msg[6]  = channel;                  // Channel (no remapping)

    // Source, backward, forward addresses (all same for direct command)
    memcpy(&msg[7],  remote_addr, 3);
    memcpy(&msg[10], remote_addr, 3);
    memcpy(&msg[13], remote_addr, 3);

    msg[16] = 0x01;                    // Destination count
    msg[17] = channel;                 // Channel destination (same as msg[6])
    msg[18] = 0x00;
    msg[19] = 0x03;

    // Encrypted payload
    msg[20] = (code >> 8) & 0xFF;      // XOR key high
    msg[21] = code & 0xFF;             // XOR key low
    msg[22] = button_pressed ? 0x40 : 0x00;  // DOWN=0x40, release=0x00

    msg_encode(&msg[20], msg[20], msg[21]);
}
```

---

## Example: Generate Targeted STOP Command

```c
void generate_msg_stop(uint8_t* msg, uint8_t* remote_addr,
                       uint8_t counter, uint8_t channel,
                       uint8_t* blind_addr) {
    uint16_t code = (0x0000 - (counter * 0x708F)) & 0xFFFF;

    memset(msg, 0, 30);

    msg[0]  = 0x1D;                    // Packet length (29)
    msg[1]  = counter;                 // Rolling counter
    msg[2]  = 0x6A;                    // Command message type
    msg[3]  = 0x10;
    msg[4]  = 0x00;
    msg[5]  = 0x01;
    msg[6]  = channel;                  // Channel (no remapping)

    memcpy(&msg[7],  remote_addr, 3);
    memcpy(&msg[10], remote_addr, 3);
    memcpy(&msg[13], remote_addr, 3);

    msg[16] = 0x01;                    // Destination count
    memcpy(&msg[17], blind_addr, 3);   // Target blind address
    msg[20] = 0x00;
    msg[21] = 0x03;

    // Encrypted payload
    msg[22] = (code >> 8) & 0xFF;
    msg[23] = code & 0xFF;
    msg[24] = 0x10;                    // STOP command

    msg_encode(&msg[22], msg[22], msg[23]);
}
```

---

## Transmission Protocol

1. **Button press**: Send packet 3 times with 10ms delay between transmissions
2. **Button release**: After ~100ms, send release packet (command byte = 0x00) 3 times
3. **Stop command**: Single transmission is usually sufficient
4. **Counter increment**: Increment counter after each logical command (press + release = 2 increments)

---

## Official Learn-In / Programming Procedure (elero manuals)

The power-cycle learn-in flow is documented in official elero handheld transmitter manuals and should be treated as a product requirement, not folklore.

### Affected transmitters

Local reference PDFs:
- `docs/manuals/monotel2.pdf`
- `docs/manuals/tempotel2.pdf`
- `docs/manuals/variotel2.pdf`

Product references:
- **TempoTel 2** — https://www.elero.com/en/products/control-systems/tempotel-2/
- **VarioTel 2** — https://www.elero.com/en/products/control-systems/variotel-2/
- **MonoTel 2** — https://www.elero.com/en/products/control-systems/monotel-2/

### Manual-backed programming flow

The manuals describe essentially the same learn-in sequence:

1. **Power-cycle the installed receiver/motor**
   - TempoTel 2: "switch the circuit breaker off and on again after a few seconds."
   - VarioTel 2: "switch the circuit breaker off and on again after a few seconds."
   - MonoTel 2: "switch the circuit breaker off and on again after a few seconds."
2. **Receiver enters learn readiness for about 5 minutes**
3. **Press transmitter programming button `P` for about 1 second**
4. The blind/curtain moves up and down for about 2 minutes
5. Confirm learn-in with **UP** within 1 second of upward movement starting
6. Confirm learn-in with **DOWN** within 1 second of downward movement starting
7. If the blind does not stop after DOWN confirmation, repeat learn-in

Manual safety/UX implications:
- Receivers power-cycled together can all enter learn readiness and react to `P`.
- Guide users to put only the intended receiver into learn readiness where possible.
- If more than 10 bidirectional receivers are learned into one channel simultaneously, transmitter group mode can be entered.

### Observed P / learn-in RF envelopes

The manuals confirm the user-visible procedure but do not publish RF internals. These are empirical captures from physical remotes:

| Remote/context | Header/envelope | Addressing | Command | Decrypted payload | TX status |
|----------------|-----------------|------------|---------|-------------------|-----------|
| MonoTel single channel | `type=0x70 type2=0x10 hop=0x00` | `channel=selector`, `num_dests=1`, one-byte dest = selector | `0x80` | `00 00 80 00 00 00 00 40/C0` | Canonical hub TX |
| TempoTel single channel | `type=0x70 type2=0x10 hop=0x00` | Same single-channel selector envelope | `0x80` | `00 00 80 00 00 00 00 40` | Canonical hub TX |
| VarioTel selector capture | `type=0x70 type2=0x10 hop=0x00` | Same single-channel selector envelope | `0x80` | `00 00 80 00 00 00 00 C0` | Canonical hub TX |
| VarioTel targeted single channel | `type=0x6A` followed by `type=0x69` | 3-byte destination = known receiver address | `0x84` | `00 00 84 00 00 00 00 80` | RX/sniffing only |
| TempoTel learned group/central | `type=0x45 type2=0x10/0x11 hop=0x15/0x25` | `channel=0`, selector list observed `01 03 07` | `0x81` | `00 00 81 00 00 00 00 00` | RX/sniffing only |

Practical interpretation:
- `PROGRAM/P` is a semantic command family, not a single packet type.
- `0x80`, `0x81`, and `0x84` are observed `PROGRAM/P` command bytes.
- `0x70`, `0x45`, `0x6A`, and `0x69` are envelope/addressing choices around the same semantic action.
- For hub-side learn-in TX, implement only the simple single-channel selector envelope: `len=0x1B`, `type=0x70`, `type2=0x10`, `hop=0x00`, `channel=<selected channel>`, `src=bwd=fwd=<hub source address>`, `num_dests=1`, `dest[0]=<selected channel>`, encrypted command `0x80`.
- Keep `0x45/0x81` group P and `0x6A/0x69/0x84` targeted P as RX-only sniffing support unless real hardware validation says otherwise.
- Do not expose RF command bytes in user-facing UI.

### Recommended backend/frontend split

**Backend (C++ core):**
- Own RF/domain logic for learn-in.
- Keep learn-in separate from normal configured-device command dispatch.
- PROGRAMMING sends canonical `0x70/0x80` P.
- CONFIRMING_UP sends existing channel/button UP (`0x44/0x20`).
- CONFIRMING_DOWN sends existing channel/button DOWN (`0x44/0x40`).
- Radio drivers transmit opaque packet bytes only; no chip driver should know P semantics.

**Frontend (Web UI / Home Assistant):**
- Guide power cycling and safety timing.
- Warn that multiple receivers may react if they enter learn readiness together.
- Trigger backend learn-in primitives; do not expose RF bytes.

### Example channel-button repeat sequence

```c
// Example transmission sequence for DOWN
for (int i = 0; i < 3; i++) {
    cc1100_tx(msg_buffer, 28);
    delay(10);
}
delay(100);
// Generate and send release packet
generate_msg_down(msg_buffer, remote_addr, counter++, channel, 0);
for (int i = 0; i < 3; i++) {
    cc1100_tx(msg_buffer, 28);
    delay(10);
}
```

---

## CC1101 Register Configuration

Key registers for Elero protocol:

```c
// Frequency (868.35 MHz)
spi_write_reg(0x0D, 0x21);  // FREQ2
spi_write_reg(0x0E, 0x71);  // FREQ1
spi_write_reg(0x0F, 0x7A);  // FREQ0

// Modulation
spi_write_reg(0x10, 0x7B);  // MDMCFG4
spi_write_reg(0x11, 0x83);  // MDMCFG3
spi_write_reg(0x12, 0x13);  // MDMCFG2
spi_write_reg(0x13, 0x52);  // MDMCFG1
spi_write_reg(0x14, 0xF8);  // MDMCFG0

// Sync word
spi_write_reg(0x04, 0xD3);  // SYNC1
spi_write_reg(0x05, 0x91);  // SYNC0

// Packet control
spi_write_reg(0x06, 0x3C);  // PKTLEN
spi_write_reg(0x07, 0x8C);  // PKTCTRL1
spi_write_reg(0x08, 0x45);  // PKTCTRL0
```

---

## Protocol Ambiguities

### type2 Field (Offset 3)

The secondary type byte varies by envelope/context:

| Value | Observed In | Notes |
|-------|-------------|-------|
| 0x00 | Normal targeted commands | Default in older command references |
| 0x10 | Button/program envelopes, targeted VarioTel P | Common remote-originated packet info |
| 0x11 | TempoTel group/list P | Alternate repeated group/list packet info |
| 0x12 | VarioTel targeted P alternate (`type=0x69`) | Follow-up/alternate targeted program envelope |
| 0x01 | Legacy devices | Alternate format |

**Recommendation:** preserve captured `type2` when replaying/sniffing a known envelope. For generated backend learn-in P use canonical `type2=0x10`; for normal targeted commands use project defaults unless hardware evidence says otherwise.

### hop Field (Offset 4)

For targeted command/status packets, hop typically starts at `0x0a` and may decrement through relays. Button/program envelopes also use this byte, but observed values are envelope/context-specific.

| Value | Observed In | Notes |
|-------|-------------|-------|
| 0x0a | Targeted commands/status, VarioTel targeted P | Direct targeted communication in existing references |
| 0x00 | Single-channel `type=0x70` P | Canonical program selector hop, not “max relay depth” in this context |
| 0x15 | TempoTel group/list P | Group/list context |
| 0x25 | TempoTel learned group P | Group/list context variant |

**Recommendation:** treat hop as envelope-specific. Preserve captured values when analyzing physical remotes; generate `hop=0x00` for canonical single-channel P and `hop=0x0a` for normal targeted commands.

---

## Communication Flows

### TX→RX Sequence: Send Command and Receive Status

Typical flow when sending a command to a blind:

```
Controller                          Blind
    |                                 |
    |  TX: type=0x6a (command)        |
    |  dst=0xa831e5, cmd=0x20 (UP)    |
    |-------------------------------->|
    |                                 |
    |  RX: type=0xca (status)         |
    |  src=0xa831e5, state=0x08       |
    |<--------------------------------|
    |       (START_MOVING_UP)         |
    |                                 |
    |  RX: type=0xca (status)         |
    |  src=0xa831e5, state=0x0a       |
    |<--------------------------------|
    |       (MOVING_UP)               |
    |                                 |
    |  RX: type=0xca (status)         |
    |  src=0xa831e5, state=0x01       |
    |<--------------------------------|
    |       (TOP - fully open)        |
```

### JSON Log Format

Both TX and RX packets are logged in JSON format for machine parsing:

**TX Log:**
```json
{"dir":"tx","len":29,"cnt":42,"type":"0x6a","type2":"0x00","hop":"0x0a",
 "channel":5,"src":"0xf0d008","dst":"0xa831e5","command":"0x20"}
```

**RX Log:**
```json
{"dir":"rx","len":28,"cnt":12,"type":"0xca","type2":"0x00","hop":"0x0a",
 "channel":5,"src":"0xa831e5","dst":"0xf0d008","command":"0x00","state":"0x01",
 "rssi":-45.5,"lqi":48,"crc":1}
```

---

## Notes

- The receiver accepts arbitrary rolling counter values (does not require sequential counters from a specific remote)
- Addresses can be discovered by sniffing RF traffic during normal remote operation
- Status responses include RSSI for signal strength monitoring
- Selector messages (`0x44`, `0x45`, `0x70`) use channel/list selectors; command messages (`0x6A`, `0x69`) target 3-byte blind addresses
- Implement one TX path per semantic need; keep group/synchronized/targeted P variants RX-only until separately validated
