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

| Offset | Length | Field | Description |
|--------|--------|-------|-------------|
| 0 | 1 | `pck_len` | Packet length (0x1B=27, 0x1C=28, 0x1D=29, 0x1E=30) |
| 1 | 1 | `counter` | Rolling message counter (increments each TX) |
| 2 | 1 | `msg_type` | Message type: 0x44=button, 0x6A=command, 0xCA/0xC9=status |
| 3-5 | 3 | `header` | Fixed: 0x10, 0x00, 0x01 |
| 6 | 1 | `channel` | RF channel (0x11 for channel 1, else channel number) |
| 7-9 | 3 | `src_addr` | Source address (remote address, big-endian) |
| 10-12 | 3 | `bwd_addr` | Backward address (usually same as src) |
| 13-15 | 3 | `fwd_addr` | Forward address (usually same as src) |
| 16 | 1 | `dest_count` | Destination count (0x01) |
| 17+ | var | `payload` | Payload (format depends on msg_type) |

### Message Types

| Type | Hex | Direction | Description |
|------|-----|-----------|-------------|
| Button Press | 0x44 | Remote → Blind | Direct button press/release |
| Command | 0x6A | Controller → Blind | Targeted command to specific blind |
| Status Response | 0xCA | Blind → Controller | Status with blind address |
| Status Response | 0xC9 | Blind → Controller | Status (alternate format) |

---

## Payload Structure

### Button Press (0x44) - Length 0x1B (27 bytes)

```
Offset 17-18: [channel, 0x00]
Offset 19:    0x03 (fixed)
Offset 20-27: Encrypted payload (8 bytes)
```

### Command (0x6A) - Length 0x1D (29 bytes)

```
Offset 17-19: Blind address (3 bytes, big-endian)
Offset 20:    0x00
Offset 21:    0x03 (or 0x04)
Offset 22-29: Encrypted payload (8 bytes)
```

### Status Response (0xCA/0xC9) - Length 0x1C-0x1E

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
    msg[6]  = (channel == 1) ? 0x11 : channel;

    // Source, backward, forward addresses (all same for direct command)
    memcpy(&msg[7],  remote_addr, 3);
    memcpy(&msg[10], remote_addr, 3);
    memcpy(&msg[13], remote_addr, 3);

    msg[16] = 0x01;                    // Destination count
    msg[17] = (channel == 1) ? 0x11 : channel;
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
    msg[6]  = (channel == 1) ? 0x11 : channel;

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

## Notes

- The receiver accepts arbitrary rolling counter values (does not require sequential counters from a specific remote)
- Addresses can be discovered by sniffing RF traffic during normal remote operation
- Status responses include RSSI for signal strength monitoring
- Button messages (0x44) broadcast to all blinds on channel; command messages (0x6A) target specific blind addresses
