# parse_packet Command Offset Issue

## Status: FIXED

Issue was **verified and fixed** on 2026-03-02.

## Summary

There was a mismatch between where TX puts the command byte (`payload[2]`) and where RX parsing expected to find it (`payload[4]`). This caused received command packets (0x6A) from physical remotes to be parsed with incorrect command bytes.

## Evidence from Production

Captured logs from a Temptec remote sending a DOWN command:

1. **14:56:55** - Received 0x6A packet, parsed `command=0x00` (WRONG - should be 0x40)
2. **14:57:25** - Blind reported `moving_down` state

The blind correctly received the DOWN command (0x40) and started moving, but our parse extracted 0x00 from the wrong offset.

### Captured Packet
```
RAW RX: 1D.60.6A.10.00.01.04.4F.CA.30.4F.CA.30.4F.CA.30.01.80.32.38.00.04.5B.A9.F0.5F.67.A9.AD.A3.2A.AE
```
- Type: 0x6A (command)
- src: 0x4FCA30 (remote)
- dst: 0x803238 (blind)
- Encrypted payload at offset 22: `5B.A9.F0.5F.67.A9.AD.A3`
- Parsed command: 0x00 (WRONG - was reading payload[4])
- Actual command: 0x40 (DOWN - at payload[2])

## The Fix

Changed command extraction from `payload[4]` to `payload[2]` in three locations:

### 1. `elero_packet.cpp:108` (parse_packet)
```cpp
// Before:
r.command = r.payload[4];

// After:
r.command = r.payload[2];
```

### 2. `elero.cpp:863` (interpret_msg JSON logging)
```cpp
// Before:
uint8_t command = ((typ == 0x6a) || (typ == 0x69)) ? payload[4] : 0;

// After:
uint8_t command = ((typ == 0x6a) || (typ == 0x69)) ? payload[2] : 0;
```

### 3. `elero.cpp:907` (RfPacketInfo callback)
```cpp
// Before:
pkt.command = ((typ == 0x6a) || (typ == 0x69)) ? payload[4] : 0;

// After:
pkt.command = ((typ == 0x6a) || (typ == 0x69)) ? payload[2] : 0;
```

## Why It Wasn't Noticed Earlier

1. We **send** commands (0x6A) to blinds - TX worked correctly
2. Blinds **respond** with status (0xCA/0xC9) - status parsing uses `payload[6]`, unaffected
3. We rarely **receive** command packets from other remotes
4. When we did receive command echoes, they were ignored as "own command echo"

The command parsing code path was essentially dead code until we captured packets from a physical remote.

## Payload Structure (Decrypted)

```
payload[0] = crypto_code_high
payload[1] = crypto_code_low
payload[2] = command         <-- CORRECT location
payload[3..5] = padding
payload[6] = state           <-- For status packets
payload[7] = parity
```

## Verification

The fix aligns RX parsing with TX building:
- `build_tx_packet()` puts command at offset 24 = `payload[2]`
- `parse_packet()` now reads command from `payload[2]`

Existing unit test `BuildTxPacket_PayloadRoundtrip` verifies this:
```cpp
EXPECT_EQ(payload[2], 0x20);  // Command at payload[2]
```
