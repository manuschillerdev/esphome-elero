/// @file test_vectors.h
/// @brief Real RF packet captures from Elero blinds and remotes.
///
/// These test vectors are captured from actual hardware to validate
/// protocol encoding/decoding against the real Elero implementation.
///
/// Packet format (after CC1101 FIFO read):
/// - Bytes 0-N: Raw packet as received
/// - Last 2 bytes: RSSI + LQI appended by CC1101
///
/// Expected packet structure for command (0x6a/0x69):
///   [len][type][cnt][blind_addr x3][remote_addr x3][channel][pck_inf x2][hop][payload x8][chk]
///
/// Expected packet structure for status response (0xCA/0xC9):
///   [len][type][cnt][blind_addr x3][remote_addr x3][channel][pck_inf x2][hop][payload x8][chk]
///   payload[6] contains state byte (ELERO_STATE_*)

#pragma once

#include <cstdint>
#include <cstddef>

namespace esphome {
namespace elero {
namespace test_vectors {

/// Test vector structure for captured packets
struct CapturedPacket {
  const char *description;        // Human-readable description
  const uint8_t *raw_data;        // Raw bytes from CC1101 FIFO
  size_t raw_len;                 // Length of raw_data
  // Expected decoded values (after protocol decoding)
  uint32_t expected_blind_addr;   // 0 if unknown/don't care
  uint32_t expected_remote_addr;  // 0 if unknown/don't care
  uint8_t expected_channel;       // 0 if unknown/don't care
  uint8_t expected_state;         // Expected state byte (for status packets)
  uint8_t expected_command;       // Expected command byte (for command packets)
  bool is_command_packet;         // true = 0x6a/0x69, false = 0xCA/0xC9
  bool is_valid;                  // Expected validation result
};

// ============================================================================
// PLACEHOLDER: Add your captured packets below
// ============================================================================
//
// Example format:
//
// static const uint8_t PACKET_BLIND1_STATUS_TOP[] = {
//   0x1d, 0xca, 0x01, 0xa8, 0x31, 0xe5, ...  // raw bytes from CC1101
// };
//
// static const CapturedPacket VECTOR_BLIND1_STATUS_TOP = {
//   .description = "Blind 0xa831e5 reporting TOP position",
//   .raw_data = PACKET_BLIND1_STATUS_TOP,
//   .raw_len = sizeof(PACKET_BLIND1_STATUS_TOP),
//   .expected_blind_addr = 0xa831e5,
//   .expected_remote_addr = 0xf0d008,
//   .expected_channel = 4,
//   .expected_state = 0x01,  // ELERO_STATE_TOP
//   .expected_command = 0x00,
//   .is_command_packet = false,
//   .is_valid = true,
// };

// ============================================================================
// Remote command packets (0x6a type - sent by remote to blind)
// ============================================================================

// TODO: Add captured "UP" command from remote
// static const uint8_t PACKET_REMOTE_CMD_UP[] = { ... };

// TODO: Add captured "DOWN" command from remote
// static const uint8_t PACKET_REMOTE_CMD_DOWN[] = { ... };

// TODO: Add captured "STOP" command from remote
// static const uint8_t PACKET_REMOTE_CMD_STOP[] = { ... };

// ============================================================================
// Blind status response packets (0xCA type - sent by blind)
// ============================================================================

// TODO: Add captured status response showing TOP position
// static const uint8_t PACKET_BLIND_STATUS_TOP[] = { ... };

// TODO: Add captured status response showing BOTTOM position
// static const uint8_t PACKET_BLIND_STATUS_BOTTOM[] = { ... };

// TODO: Add captured status response showing MOVING_UP
// static const uint8_t PACKET_BLIND_STATUS_MOVING_UP[] = { ... };

// TODO: Add captured status response showing MOVING_DOWN
// static const uint8_t PACKET_BLIND_STATUS_MOVING_DOWN[] = { ... };

// ============================================================================
// All test vectors array (for iteration in tests)
// ============================================================================

// Uncomment and populate when you have actual packets:
// static const CapturedPacket ALL_VECTORS[] = {
//   VECTOR_BLIND1_STATUS_TOP,
//   VECTOR_BLIND1_STATUS_BOTTOM,
//   // ... more vectors
// };
// static const size_t NUM_VECTORS = sizeof(ALL_VECTORS) / sizeof(ALL_VECTORS[0]);

}  // namespace test_vectors
}  // namespace elero
}  // namespace esphome
