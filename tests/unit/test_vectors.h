/// @file test_vectors.h
/// @brief Test vectors for Elero packet parsing tests.
///
/// This file contains test packet data and expected parse results.
/// Real RF packet captures should be added here as they become available.

#pragma once

#include <cstdint>
#include <cstddef>
#include "elero/elero_packet.h"

namespace esphome::elero::test_vectors {

// ─── Test Vector Structure ──────────────────────────────────────────────────

struct PacketVector {
  const char* name;
  const char* description;
  const uint8_t* raw;
  size_t raw_len;
  uint8_t exp_type;
  uint8_t exp_channel;
  uint32_t exp_src_addr;
  uint32_t exp_dst_addr;
  uint8_t exp_command;
  uint8_t exp_state;
  bool expect_valid;
  const char* reject_reason;
};

// ─── Invalid Packet Vectors ─────────────────────────────────────────────────

// Packet with length > MAX_PACKET_SIZE (57)
constexpr uint8_t RAW_TOO_LONG[] = {
  0xFF, 0x01, 0x6A, 0x00, 0x0A, 0x01, 0x05,  // length=255 (invalid)
  0xA8, 0x31, 0xE5,  // src
  0xA8, 0x31, 0xE5,  // bwd
  0xA8, 0x31, 0xE5,  // fwd
  0x01,              // num_dests
  0x12, 0x34, 0x56,  // dst
  0x00, 0x03,        // payload header
  0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,  // payload
};

constexpr PacketVector VEC_INVALID_TOO_LONG = {
  .name = "TooLong",
  .description = "Packet with length > 57",
  .raw = RAW_TOO_LONG,
  .raw_len = sizeof(RAW_TOO_LONG),
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
constexpr uint8_t RAW_TOO_MANY_DESTS[] = {
  0x1D, 0x01, 0x6A, 0x00, 0x0A, 0x01, 0x05,  // valid header
  0xA8, 0x31, 0xE5,  // src
  0xA8, 0x31, 0xE5,  // bwd
  0xA8, 0x31, 0xE5,  // fwd
  0x99,              // num_dests=153 (invalid, >20)
  0x12, 0x34, 0x56,
  0x00, 0x03,
  0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
};

constexpr PacketVector VEC_INVALID_TOO_MANY_DESTS = {
  .name = "TooManyDests",
  .description = "Packet with num_dests > 20",
  .raw = RAW_TOO_MANY_DESTS,
  .raw_len = sizeof(RAW_TOO_MANY_DESTS),
  .exp_type = 0,
  .exp_channel = 0,
  .exp_src_addr = 0,
  .exp_dst_addr = 0,
  .exp_command = 0,
  .exp_state = 0,
  .expect_valid = false,
  .reject_reason = "too_many_dests",
};

// Packet that's too short
constexpr uint8_t RAW_TOO_SHORT[] = {0x1D, 0x01};

constexpr PacketVector VEC_INVALID_TOO_SHORT = {
  .name = "TooShort",
  .description = "Packet with only 2 bytes",
  .raw = RAW_TOO_SHORT,
  .raw_len = sizeof(RAW_TOO_SHORT),
  .exp_type = 0,
  .exp_channel = 0,
  .exp_src_addr = 0,
  .exp_dst_addr = 0,
  .exp_command = 0,
  .exp_state = 0,
  .expect_valid = false,
  .reject_reason = "too_short",
};

// Empty packet
constexpr PacketVector VEC_INVALID_EMPTY = {
  .name = "Empty",
  .description = "Empty packet (nullptr)",
  .raw = nullptr,
  .raw_len = 0,
  .exp_type = 0,
  .exp_channel = 0,
  .exp_src_addr = 0,
  .exp_dst_addr = 0,
  .exp_command = 0,
  .exp_state = 0,
  .expect_valid = false,
  .reject_reason = "too_short",
};

// ─── Placeholder for Real RF Captures ───────────────────────────────────────
//
// Add real RF packet captures here. Example format:
//
// constexpr uint8_t RAW_STATUS_TOP[] = {
//   0x1C, 0x01, 0xCA, 0x00, 0x0A, 0x01, 0x05,  // header
//   0xA8, 0x31, 0xE5,  // src (blind address)
//   0xA8, 0x31, 0xE5,  // bwd
//   0xA8, 0x31, 0xE5,  // fwd
//   0x01,              // num_dests
//   0x12, 0x34, 0x56,  // dst (remote address)
//   0x00, 0x04,        // payload header
//   // ... encrypted payload ...
//   // ... RSSI, LQI appended ...
// };
//
// constexpr PacketVector VEC_STATUS_TOP = {
//   .name = "StatusTop",
//   .description = "Blind at top position",
//   .raw = RAW_STATUS_TOP,
//   .raw_len = sizeof(RAW_STATUS_TOP),
//   .exp_type = 0xCA,
//   .exp_channel = 5,
//   .exp_src_addr = 0xa831e5,
//   .exp_dst_addr = 0x123456,
//   .exp_command = 0,
//   .exp_state = packet::state::TOP,
//   .expect_valid = true,
//   .reject_reason = nullptr,
// };

}  // namespace esphome::elero::test_vectors
