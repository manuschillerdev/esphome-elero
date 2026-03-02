# Elero RF Packet Structure

This document describes the exact byte layout for TX and RX packets, derived from working implementation `fe33d186f70dd97a3fc8dc8a42bfb94dc3c7a5e5`.

## TX Packet (Command to Blind)

Total length: 30 bytes (length byte + 29 data bytes)

| Position | Field | Bytes | Value/Source | Notes |
|----------|-------|-------|--------------|-------|
| 0 | Length | 1 | `0x1D` (29) | Fixed |
| 1 | Counter | 1 | 1-255 | Rolling, wraps to 1 |
| 2 | Type | 1 | `0x6A` | Command packet |
| 3 | Type2 | 1 | `0x00` | Default |
| 4 | Hop | 1 | `0x0A` | Default |
| 5 | System | 1 | `0x01` | Fixed |
| 6 | Channel | 1 | config | RF channel |
| 7-9 | Src Addr | 3 | config | Remote address (big-endian) |
| 10-12 | Bwd Addr | 3 | = Src | Same as source |
| 13-15 | Fwd Addr | 3 | = Src | Same as source |
| 16 | Num Dests | 1 | `0x01` | Fixed (single dest) |
| 17-19 | Dst Addr | 3 | config | Blind address (big-endian) |
| 20 | Payload 1 | 1 | `0x00` | Default |
| 21 | Payload 2 | 1 | `0x04` | Default |
| **22-29** | **Encrypted** | **8** | | **msg_encode() applied** |

### Encrypted Section (positions 22-29, before encryption)

| Offset | Position | Field | Value |
|--------|----------|-------|-------|
| 0 | 22 | Crypto Hi | `(code >> 8) & 0xFF` |
| 1 | 23 | Crypto Lo | `code & 0xFF` |
| 2 | 24 | **Command** | See command table |
| 3 | 25 | Padding | `0x00` |
| 4 | 26 | Padding | `0x00` |
| 5 | 27 | Padding | `0x00` |
| 6 | 28 | Padding | `0x00` |
| 7 | 29 | Parity | Set by `msg_encode()` |

**Crypto code formula:** `code = (0x0000 - (counter * 0x708F)) & 0xFFFF`

## RX Packet (Status from Blind)

For status packets (type `0xCA`/`0xC9`), positions 0-21 match TX structure.

### Decrypted Payload (after msg_decode)

| Offset | Position | Field | Notes |
|--------|----------|-------|-------|
| 0 | 22 | Crypto Hi | |
| 1 | 23 | Crypto Lo | |
| 2 | 24 | (unused) | |
| 3 | 25 | (unused) | |
| 4 | 26 | Command* | *For sniffing other remotes |
| 5 | 27 | (unused) | |
| 6 | 28 | **State** | See state table |
| 7 | 29 | Parity | |

**Note:** Positions 30-31 contain RSSI (raw) and LQI (with CRC bit) appended by CC1101.

## Command Bytes

| Command | Byte | Description |
|---------|------|-------------|
| CHECK | `0x00` | Request status (no movement) |
| STOP | `0x10` | Stop movement |
| UP | `0x20` | Move up / open |
| TILT | `0x24` | Tilt position |
| DOWN | `0x40` | Move down / close |
| INTERMEDIATE | `0x44` | Move to intermediate position |

## State Bytes

| State | Byte | Description |
|-------|------|-------------|
| UNKNOWN | `0x00` | Unknown state |
| TOP | `0x01` | Fully open |
| BOTTOM | `0x02` | Fully closed |
| INTERMEDIATE | `0x03` | Intermediate position |
| TILT | `0x04` | Tilted |
| BLOCKING | `0x05` | Obstacle detected |
| OVERHEATED | `0x06` | Motor overheated |
| TIMEOUT | `0x07` | Communication timeout |
| START_MOVING_UP | `0x08` | Starting upward movement |
| START_MOVING_DOWN | `0x09` | Starting downward movement |
| MOVING_UP | `0x0A` | Moving up |
| MOVING_DOWN | `0x0B` | Moving down |
| STOPPED | `0x0D` | Stopped |
| TOP_TILT | `0x0E` | Open + tilted |
| BOTTOM_TILT | `0x0F` | Closed + tilted (also LIGHT_OFF) |
| LIGHT_ON | `0x10` | Light on |

## Message Types

| Type | Byte | Direction | Description |
|------|------|-----------|-------------|
| BUTTON | `0x44` | Remote → All | Button broadcast |
| COMMAND | `0x6A` | Remote → Blind | Targeted command |
| COMMAND_ALT | `0x69` | Remote → Blind | Alternate command format |
| STATUS | `0xCA` | Blind → All | Status response |
| STATUS_ALT | `0xC9` | Blind → All | Alternate status format |

**Addressing rule:** Types > `0x60` use 3-byte addresses, others use 1-byte.

## Critical Implementation Notes

1. **Positions 25-29 MUST be zeroed** before encryption - garbage here corrupts the packet
2. **Crypto code overwrites positions 22-23** after payload copy
3. **msg_encode() operates on 8 bytes** starting at position 22
4. **TX command at offset 2** (position 24) differs from **RX sniffing at offset 4** (position 26)
