/// @file test_vectors.h
/// @brief Real RF packet captures from Elero blinds and remotes.
///
/// Packet structure (CC1101 FIFO after read):
/// ┌─────────────────────────────────────────────────────────────────────────┐
/// │ Offset │ Field       │ Size  │ Description                             │
/// ├────────┼─────────────┼───────┼─────────────────────────────────────────┤
/// │   0    │ length      │   1   │ Packet length (excluding RSSI/LQI)      │
/// │   1    │ cnt         │   1   │ Counter/sequence number                 │
/// │   2    │ typ         │   1   │ Packet type (0x6a/0x69=cmd, 0xca/0xc9)  │
/// │   3    │ typ2        │   1   │ Secondary type byte                     │
/// │   4    │ hop         │   1   │ Hop count                               │
/// │   5    │ syst        │   1   │ System address (usually 0x01)           │
/// │   6    │ chl         │   1   │ Channel number                          │
/// │   7-9  │ src         │   3   │ Source address (big-endian)             │
/// │ 10-12  │ bwd         │   3   │ Backward address                        │
/// │ 13-15  │ fwd         │   3   │ Forward address                         │
/// │  16    │ num_dests   │   1   │ Number of destinations                  │
/// │ 17-19  │ dst         │  1-3  │ Destination (3 bytes if typ>0x60)       │
/// │ 17+N   │ payload1    │   1   │ Payload byte 1 (pck_inf[0])             │
/// │ 18+N   │ payload2    │   1   │ Payload byte 2 (pck_inf[1])             │
/// │ 19+N   │ encrypted   │   8   │ Encrypted payload                       │
/// │ 27+N   │ checksum    │   1   │ Packet checksum                         │
/// │ len+1  │ rssi_raw    │   1   │ CC1101 RSSI (appended)                  │
/// │ len+2  │ lqi_crc     │   1   │ LQI[6:0] | CRC_OK[7] (appended)         │
/// └─────────────────────────────────────────────────────────────────────────┘
///
/// After decryption of the 8-byte payload:
///   payload[4] = command byte (for 0x6a/0x69 packets)
///   payload[6] = state byte (for 0xCA/0xC9 packets)

#pragma once

#include <cstdint>
#include <cstddef>

namespace esphome::elero::test_vectors {

// ============================================================================
// Protocol Constants (mirrored from elero.h for test independence)
// ============================================================================

// Packet types
constexpr uint8_t PKT_TYPE_COMMAND = 0x6a;
constexpr uint8_t PKT_TYPE_COMMAND_ALT = 0x69;
constexpr uint8_t PKT_TYPE_STATUS = 0xca;
constexpr uint8_t PKT_TYPE_STATUS_ALT = 0xc9;

// Command bytes (in decrypted payload[4])
constexpr uint8_t CMD_CHECK = 0x00;
constexpr uint8_t CMD_STOP = 0x10;
constexpr uint8_t CMD_UP = 0x20;
constexpr uint8_t CMD_TILT = 0x24;
constexpr uint8_t CMD_DOWN = 0x40;
constexpr uint8_t CMD_INT = 0x44;

// State bytes (in decrypted payload[6])
constexpr uint8_t STATE_UNKNOWN = 0x00;
constexpr uint8_t STATE_TOP = 0x01;
constexpr uint8_t STATE_BOTTOM = 0x02;
constexpr uint8_t STATE_INTERMEDIATE = 0x03;
constexpr uint8_t STATE_TILT = 0x04;
constexpr uint8_t STATE_BLOCKING = 0x05;
constexpr uint8_t STATE_OVERHEATED = 0x06;
constexpr uint8_t STATE_TIMEOUT = 0x07;
constexpr uint8_t STATE_START_MOVING_UP = 0x08;
constexpr uint8_t STATE_START_MOVING_DOWN = 0x09;
constexpr uint8_t STATE_MOVING_UP = 0x0a;
constexpr uint8_t STATE_MOVING_DOWN = 0x0b;
constexpr uint8_t STATE_STOPPED = 0x0d;
constexpr uint8_t STATE_TOP_TILT = 0x0e;
constexpr uint8_t STATE_BOTTOM_TILT = 0x0f;

// Packet structure offsets
constexpr size_t OFF_LENGTH = 0;
constexpr size_t OFF_CNT = 1;
constexpr size_t OFF_TYPE = 2;
constexpr size_t OFF_TYPE2 = 3;
constexpr size_t OFF_HOP = 4;
constexpr size_t OFF_SYST = 5;
constexpr size_t OFF_CHANNEL = 6;
constexpr size_t OFF_SRC_ADDR = 7;      // 3 bytes
constexpr size_t OFF_BWD_ADDR = 10;     // 3 bytes
constexpr size_t OFF_FWD_ADDR = 13;     // 3 bytes
constexpr size_t OFF_NUM_DESTS = 16;
constexpr size_t OFF_DST_ADDR = 17;     // 1-3 bytes depending on typ

// Minimum valid packet sizes
constexpr size_t MIN_PACKET_LEN = 28;   // Minimum with 1 dest + payload
constexpr size_t MIN_FIFO_READ = 32;    // + RSSI + LQI + some margin

// ============================================================================
// Test Vector Structure
// ============================================================================

struct PacketVector {
  const char *name;               // Test name
  const char *description;        // What this packet represents

  // Raw packet data (paste hex bytes here)
  const uint8_t *raw;
  size_t raw_len;

  // Expected header values
  uint8_t exp_type;               // 0x6a, 0x69, 0xca, 0xc9
  uint8_t exp_channel;
  uint32_t exp_src_addr;          // Source address (blind for status, remote for cmd)
  uint32_t exp_dst_addr;          // Destination address (remote for status, blind for cmd)

  // Expected decoded payload
  uint8_t exp_command;            // For command packets (payload[4])
  uint8_t exp_state;              // For status packets (payload[6])

  // Validation flags
  bool expect_valid;              // Should pass validation?
  const char *reject_reason;      // Expected rejection reason if !expect_valid
};

// ============================================================================
// PASTE YOUR CAPTURED PACKETS BELOW
// ============================================================================
//
// Format: Copy RAW RX hex from ESPHome logs or web UI packet dump
// Example log line:
//   [V][elero:147]: RAW RX 32 bytes: 1D.6A.01.A8.31.E5...
//
// Convert to array:
//   static const uint8_t PACKET_NAME[] = {0x1d, 0x6a, 0x01, 0xa8, 0x31, 0xe5, ...};

// ────────────────────────────────────────────────────────────────────────────
// STATUS PACKETS (0xCA/0xC9) - Blind → Remote
// ────────────────────────────────────────────────────────────────────────────

// Blind reporting TOP position
// static const uint8_t RAW_STATUS_TOP[] = {
//   // PASTE HEX BYTES HERE
// };
// static const PacketVector VEC_STATUS_TOP = {
//   .name = "StatusTop",
//   .description = "Blind reports fully open (TOP) position",
//   .raw = RAW_STATUS_TOP,
//   .raw_len = sizeof(RAW_STATUS_TOP),
//   .exp_type = PKT_TYPE_STATUS,
//   .exp_channel = 0,          // Fill in actual channel
//   .exp_src_addr = 0x000000,  // Fill in blind address
//   .exp_dst_addr = 0x000000,  // Fill in remote address
//   .exp_command = 0,
//   .exp_state = STATE_TOP,
//   .expect_valid = true,
//   .reject_reason = nullptr,
// };

// Blind reporting BOTTOM position
// static const uint8_t RAW_STATUS_BOTTOM[] = { /* PASTE */ };

// Blind reporting INTERMEDIATE position
// static const uint8_t RAW_STATUS_INTERMEDIATE[] = { /* PASTE */ };

// Blind reporting MOVING UP
// static const uint8_t RAW_STATUS_MOVING_UP[] = { /* PASTE */ };

// Blind reporting MOVING DOWN
// static const uint8_t RAW_STATUS_MOVING_DOWN[] = { /* PASTE */ };

// Blind reporting STOPPED
// static const uint8_t RAW_STATUS_STOPPED[] = { /* PASTE */ };

// ────────────────────────────────────────────────────────────────────────────
// COMMAND PACKETS (0x6A/0x69) - Remote → Blind
// ────────────────────────────────────────────────────────────────────────────

// Remote sending UP command
// static const uint8_t RAW_CMD_UP[] = {
//   // PASTE HEX BYTES HERE
// };
// static const PacketVector VEC_CMD_UP = {
//   .name = "CommandUp",
//   .description = "Remote sends UP/OPEN command",
//   .raw = RAW_CMD_UP,
//   .raw_len = sizeof(RAW_CMD_UP),
//   .exp_type = PKT_TYPE_COMMAND,
//   .exp_channel = 0,          // Fill in actual channel
//   .exp_src_addr = 0x000000,  // Fill in remote address
//   .exp_dst_addr = 0x000000,  // Fill in blind address
//   .exp_command = CMD_UP,
//   .exp_state = 0,
//   .expect_valid = true,
//   .reject_reason = nullptr,
// };

// Remote sending DOWN command
// static const uint8_t RAW_CMD_DOWN[] = { /* PASTE */ };

// Remote sending STOP command
// static const uint8_t RAW_CMD_STOP[] = { /* PASTE */ };

// Remote sending TILT command
// static const uint8_t RAW_CMD_TILT[] = { /* PASTE */ };

// Remote sending CHECK/poll command
// static const uint8_t RAW_CMD_CHECK[] = { /* PASTE */ };

// ────────────────────────────────────────────────────────────────────────────
// INVALID PACKETS (for rejection testing)
// ────────────────────────────────────────────────────────────────────────────

// Packet with invalid length
static const uint8_t RAW_INVALID_TOO_LONG[] = {
  0xff, 0x00, 0x00, 0x00  // length=255, way too long
};
static const PacketVector VEC_INVALID_TOO_LONG = {
  .name = "InvalidTooLong",
  .description = "Packet with length > MAX_PACKET_SIZE",
  .raw = RAW_INVALID_TOO_LONG,
  .raw_len = sizeof(RAW_INVALID_TOO_LONG),
  .exp_type = 0,
  .exp_channel = 0,
  .exp_src_addr = 0,
  .exp_dst_addr = 0,
  .exp_command = 0,
  .exp_state = 0,
  .expect_valid = false,
  .reject_reason = "too_long",
};

// Packet with too many destinations
static const uint8_t RAW_INVALID_TOO_MANY_DESTS[] = {
  0x1d, 0x01, 0x6a, 0x00, 0x0a, 0x01, 0x04,  // len, cnt, typ, typ2, hop, syst, chl
  0xa8, 0x31, 0xe5,  // src
  0x00, 0x00, 0x00,  // bwd
  0x00, 0x00, 0x00,  // fwd
  0x99,              // num_dests = 153 (way too many)
};
static const PacketVector VEC_INVALID_TOO_MANY_DESTS = {
  .name = "InvalidTooManyDests",
  .description = "Packet with num_dests > 20",
  .raw = RAW_INVALID_TOO_MANY_DESTS,
  .raw_len = sizeof(RAW_INVALID_TOO_MANY_DESTS),
  .exp_type = PKT_TYPE_COMMAND,
  .exp_channel = 4,
  .exp_src_addr = 0xa831e5,
  .exp_dst_addr = 0,
  .exp_command = 0,
  .exp_state = 0,
  .expect_valid = false,
  .reject_reason = "too_many_dests",
};

// ============================================================================
// Test Vector Collection
// ============================================================================

// Add your vectors here once defined:
// static const PacketVector* ALL_VECTORS[] = {
//   &VEC_STATUS_TOP,
//   &VEC_STATUS_BOTTOM,
//   &VEC_CMD_UP,
//   &VEC_CMD_DOWN,
//   &VEC_CMD_STOP,
//   &VEC_INVALID_TOO_LONG,
//   &VEC_INVALID_TOO_MANY_DESTS,
// };
// static const size_t NUM_VECTORS = sizeof(ALL_VECTORS) / sizeof(ALL_VECTORS[0]);

// Invalid packet vectors (always available for testing)
static const PacketVector* INVALID_VECTORS[] = {
  &VEC_INVALID_TOO_LONG,
  &VEC_INVALID_TOO_MANY_DESTS,
};
static const size_t NUM_INVALID_VECTORS = sizeof(INVALID_VECTORS) / sizeof(INVALID_VECTORS[0]);

}  // namespace esphome::elero::test_vectors
