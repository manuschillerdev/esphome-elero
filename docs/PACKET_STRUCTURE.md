# Elero RF Packet Structure

This document describes the current packet builders and parser in
`components/elero/elero_packet.cpp`. Packet type selects the addressing format;
the command byte selects the action.

## Channel addressing versus receiver addressing

| Packet type | Destination fields | Which receivers can act? |
| --- | --- | --- |
| `0x44` button | One-byte channel selector, or a list of selectors | Receivers paired to the source remote identity and selected channel(s) |
| `0x6a` targeted command | Three-byte receiver address, or a list of receiver addresses | The explicitly addressed receiver(s), using the paired source identity |

Example: remote `0x111111`, channel 2, is paired to blinds A (`0x222222`)
and B (`0x333333`). A `0x44` DOWN packet from that remote with selector 2
can operate both A and B. A `0x6a` DOWN packet addressed to `0x222222`
selects A. Both packets can carry the same DOWN command byte `0x40`.

Here, **channel** means a logical channel on a remote, not the RF frequency.
The source remote identity still matters in both formats. The receiver address
stored for a device remains necessary for status lookup and targeted commands,
even when its movement command uses channel addressing.

### What this component currently sends

| Operation | Packet type | Target |
| --- | --- | --- |
| Single-cover UP, DOWN, tilt, intermediate preset | `0x44` | Device's source remote identity and channel |
| Cover STOP and CHECK | `0x6a` | Device's receiver address |
| Compatible group movement | `0x44` selector list | Channels sharing a source remote and command mapping |
| Group STOP/CHECK, or movement with different command mappings | Separate per-device operations | Each device uses the corresponding single-device path |

The registry explicitly selects these types. A device's stored `msg_type` is
not currently a switch between `0x44` and `0x6a` for normal cover movement.
Changing `command_down` changes the action byte; it does not change addressing.
See [configuration behaviour](CONFIGURATION.md#rf-addressing-and-packet-type).

Physical remote captures establish that `0x44` is a real Elero format. This does
not establish that every receiver accepts both formats with the same action
semantics. Receiver-specific command values and packet addressing are separate
compatibility questions.

## Targeted TX packet (`0x6a`, one receiver)

Total length: 30 bytes (length byte + 29 data bytes)

| Position | Field | Bytes | C++ Constant | Value/Source | Notes |
|----------|-------|-------|--------------|--------------|-------|
| 0 | Length | 1 | `tx_offset::LENGTH` | `0x1D` (29) | Fixed |
| 1 | Counter | 1 | `tx_offset::COUNTER` | 1-255 | Rolling, wraps to 1 |
| 2 | Type | 1 | `tx_offset::TYPE` | `0x6A` | Command packet |
| 3 | Type2 | 1 | `tx_offset::TYPE2` | `0x00` | Default |
| 4 | Hop | 1 | `tx_offset::HOP` | `0x0A` | Default |
| 5 | System | 1 | `tx_offset::SYS` | `0x01` | Fixed |
| 6 | Channel | 1 | `tx_offset::CHANNEL` | config | RF channel |
| 7-9 | Src Addr | 3 | `tx_offset::SRC_ADDR` | config | Remote address (big-endian) |
| 10-12 | Bwd Addr | 3 | `tx_offset::BWD_ADDR` | = Src | Same as source |
| 13-15 | Fwd Addr | 3 | `tx_offset::FWD_ADDR` | = Src | Same as source |
| 16 | Num Dests | 1 | `tx_offset::NUM_DESTS` | `0x01` | Fixed (single dest) |
| 17-19 | Dst Addr | 3 | `tx_offset::DST_ADDR` | config | Blind address (big-endian) |
| 20 | Payload 1 | 1 | `tx_offset::PAYLOAD` | `0x00` | Default |
| 21 | Payload 2 | 1 | `tx_offset::PAYLOAD + 1` | `0x04` | Default |
| **22-29** | **Encrypted** | **8** | `tx_offset::CRYPTO_CODE` | | **msg_encode() applied** |

### Encrypted Section (positions 22-29)

Same layout for TX and RX. C++ constants in `payload_offset::` namespace.
Verified against eleropy, andyboeh, and pfriedrich84 implementations.

| Offset | Position | C++ Constant | Field | TX Value | RX Notes |
|--------|----------|--------------|-------|----------|----------|
| 0 | 22 | `payload_offset::CRYPTO_HIGH` | Crypto Hi | `(code >> 8) & 0xFF` | |
| 1 | 23 | `payload_offset::CRYPTO_LOW` | Crypto Lo | `code & 0xFF` | |
| 2 | 24 | `payload_offset::COMMAND` | **Command** | See command table | Command from remote |
| 3 | 25 | `payload_offset::COMMAND2` | Command 2 | `0x00` | Secondary command byte |
| 4 | 26 | | Padding | `0x00` | |
| 5 | 27 | | Padding | `0x00` | |
| 6 | 28 | `payload_offset::STATE` | **State** | `0x00` (unused) | See state table |
| 7 | 29 | `payload_offset::PARITY` | Parity | Set by `msg_encode()` | |

**Crypto code formula:** `code = (0x0000 - (counter * 0x708F)) & 0xFFFF`

**Note:** For RX packets, positions after the packet data contain RSSI (raw) and LQI (with CRC bit) appended by CC1101.

## Command Bytes

Addressed `0x6A`/`0x69` packets can carry multiple 24-bit destinations. With N
addresses, the destination list starts at offset 17 and occupies `3 * N` bytes;
the two payload prefix bytes follow the list, and the encrypted eight-byte
section starts at `19 + 3 * N`. All addresses are retained in decoded packets
and the WebSocket `destinations` array. `dst` remains the first address for
older clients. Prefix values vary between transmitters and are not assumed to
be `00 04` during receive parsing. The supported 57-byte body fits up to 10
addresses. Selector-list packets (`0x44`, `0x45`, `0x70`) use one-byte selectors
and are a separate envelope.

`msg_decode()` XORs the recovered key into the first pair as well as the other
payload pairs, so its decoded first two bytes are zero by construction. This
does not indicate failed decryption. Historical `0x45` group/all-channel
captures and remaining semantic questions are documented in
[the protocol issue investigation](research/elero-open-protocol-issues.md).

All constants in `packet::command::` namespace.

| Command | Byte | C++ Constant | Description |
|---------|------|--------------|-------------|
| CHECK | `0x00` | `command::CHECK` | Request status (no movement) |
| STOP | `0x10` | `command::STOP` | Stop movement |
| UP | `0x20` | `command::UP` | Move up / open |
| TILT | `0x24` | `command::TILT` | Tilt position |
| DOWN | `0x40` | `command::DOWN` | Move down / close |
| INTERMEDIATE | `0x44` | `command::INTERMEDIATE` | Move to intermediate position |
| INVALID | `0xFF` | `command::INVALID` | Invalid/unknown command marker |

## State Bytes

All constants in `packet::state::` namespace.

| State | Byte | C++ Constant | Description |
|-------|------|--------------|-------------|
| UNKNOWN | `0x00` | `state::UNKNOWN` | Unknown state |
| TOP | `0x01` | `state::TOP` | Fully open |
| BOTTOM | `0x02` | `state::BOTTOM` | Fully closed |
| INTERMEDIATE | `0x03` | `state::INTERMEDIATE` | Intermediate position |
| TILT | `0x04` | `state::TILT` | Tilted |
| BLOCKING | `0x05` | `state::BLOCKING` | Obstacle detected |
| OVERHEATED | `0x06` | `state::OVERHEATED` | Motor overheated |
| TIMEOUT | `0x07` | `state::TIMEOUT` | Communication timeout |
| START_MOVING_UP | `0x08` | `state::START_MOVING_UP` | Starting upward movement |
| START_MOVING_DOWN | `0x09` | `state::START_MOVING_DOWN` | Starting downward movement |
| MOVING_UP | `0x0A` | `state::MOVING_UP` | Moving up |
| MOVING_DOWN | `0x0B` | `state::MOVING_DOWN` | Moving down |
| STOPPED | `0x0D` | `state::STOPPED` | Stopped |
| TOP_TILT | `0x0E` | `state::TOP_TILT` | Open + tilted |
| BOTTOM_TILT | `0x0F` | `state::BOTTOM_TILT` | Closed + tilted |
| LIGHT_OFF | `0x10` | `state::LIGHT_OFF` | Switching receiver off |
| LIGHT_ON | `0x11` | `state::LIGHT_ON` | Light on |

## Message Types

All constants in `packet::msg_type::` namespace.

| Type | Byte | C++ Constant | Direction | Description |
|------|------|--------------|-----------|-------------|
| BUTTON | `0x44` | `msg_type::BUTTON` | Remote → paired receivers | Channel selector(s) |
| BUTTON_GROUP | `0x45` | `msg_type::BUTTON_GROUP` | Remote → paired receivers | Group/central selector-list envelope; TX semantics remain under investigation |
| COMMAND | `0x6A` | `msg_type::COMMAND` | Remote → Blind | Targeted command |
| COMMAND_ALT | `0x69` | `msg_type::COMMAND_ALT` | Remote → Blind | Alternate command format |
| PROGRAM | `0x70` | `msg_type::PROGRAM` | Remote → Receiver | Programming with a one-byte channel selector |
| STATUS | `0xCA` | `msg_type::STATUS` | Blind → All | Status response |
| STATUS_ALT | `0xC9` | `msg_type::STATUS_ALT` | Blind → All | Alternate status format |

**Addressing rule:** use the explicit envelope classifiers in `elero_packet.h`.
Types `0x44`, `0x45`, and `0x70` use one-byte selectors; `0x6a`, `0x69`,
`0xca`, and `0xc9` use three-byte addresses. A numeric threshold is incorrect:
`0x70` is greater than `0x60` but still uses a one-byte selector.

## Critical Implementation Notes

1. **Positions 25-29 MUST be zeroed** before encryption - garbage here corrupts the packet
2. **Crypto code overwrites positions 22-23** after payload copy
3. **msg_encode() operates on 8 bytes** starting at position 22
4. **TX command at offset 2** (position 24) differs from **RX sniffing at offset 4** (position 26)
