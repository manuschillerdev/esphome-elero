/// @file test_packet_vectors.cpp
/// @brief Unit tests for packet parsing functions.

#include <gtest/gtest.h>
#include <cstring>
#include "elero/elero_packet.h"
#include "test_vectors.h"

using namespace esphome::elero::packet;
using namespace esphome::elero::test_vectors;

// ============================================================================
// Helper Function Tests
// ============================================================================

TEST(PacketHelpers, ExtractAddress_BigEndian) {
  uint8_t data[] = {0xA8, 0x31, 0xE5};
  EXPECT_EQ(extract_addr(data), 0xA831E5u);
}

TEST(PacketHelpers, ExtractAddress_Zero) {
  uint8_t data[] = {0x00, 0x00, 0x00};
  EXPECT_EQ(extract_addr(data), 0x000000u);
}

TEST(PacketHelpers, ExtractAddress_Max) {
  uint8_t data[] = {0xFF, 0xFF, 0xFF};
  EXPECT_EQ(extract_addr(data), 0xFFFFFFu);
}

TEST(PacketHelpers, CalcRssi_PositiveRaw) {
  // Raw value 100 (positive)
  // RSSI = 100/2 + (-74) = 50 - 74 = -24 dBm
  EXPECT_FLOAT_EQ(calc_rssi(100), -24.0f);
}

TEST(PacketHelpers, CalcRssi_NegativeRaw) {
  // Raw value 200 = -56 in signed (two's complement)
  // RSSI = -56/2 + (-74) = -28 - 74 = -102 dBm
  EXPECT_FLOAT_EQ(calc_rssi(200), -102.0f);
}

TEST(PacketHelpers, CalcRssi_Boundary127) {
  // Max positive value
  // RSSI = 127/2 + (-74) = 63.5 - 74 = -10.5 dBm
  EXPECT_FLOAT_EQ(calc_rssi(127), -10.5f);
}

TEST(PacketHelpers, CalcRssi_Boundary128) {
  // Min negative value (128 = -128 signed)
  // RSSI = -128/2 + (-74) = -64 - 74 = -138 dBm
  EXPECT_FLOAT_EQ(calc_rssi(128), -138.0f);
}

TEST(PacketHelpers, IsCommandPacket) {
  EXPECT_TRUE(is_command_packet(0x6a));
  EXPECT_TRUE(is_command_packet(0x69));
  EXPECT_FALSE(is_command_packet(0xca));
  EXPECT_FALSE(is_command_packet(0xc9));
  EXPECT_FALSE(is_command_packet(0x00));
  EXPECT_FALSE(is_command_packet(0x44));
}

TEST(PacketHelpers, IsStatusPacket) {
  EXPECT_TRUE(is_status_packet(0xca));
  EXPECT_TRUE(is_status_packet(0xc9));
  EXPECT_FALSE(is_status_packet(0x6a));
  EXPECT_FALSE(is_status_packet(0x69));
  EXPECT_FALSE(is_status_packet(0x00));
  EXPECT_FALSE(is_status_packet(0x44));
}

// ============================================================================
// Invalid Packet Tests
// ============================================================================

TEST(PacketParsing, InvalidPacket_TooLong) {
  auto result = parse_packet(VEC_INVALID_TOO_LONG.raw, VEC_INVALID_TOO_LONG.raw_len);
  EXPECT_FALSE(result.valid);
  EXPECT_STREQ(result.reject_reason, "too_long");
}

TEST(PacketParsing, InvalidPacket_TooManyDests) {
  auto result = parse_packet(VEC_INVALID_TOO_MANY_DESTS.raw, VEC_INVALID_TOO_MANY_DESTS.raw_len);
  EXPECT_FALSE(result.valid);
  EXPECT_STREQ(result.reject_reason, "too_many_dests");
}

TEST(PacketParsing, InvalidPacket_TooShort) {
  auto result = parse_packet(VEC_INVALID_TOO_SHORT.raw, VEC_INVALID_TOO_SHORT.raw_len);
  EXPECT_FALSE(result.valid);
  EXPECT_STREQ(result.reject_reason, "too_short");
}

TEST(PacketParsing, InvalidPacket_Empty) {
  auto result = parse_packet(VEC_INVALID_EMPTY.raw, VEC_INVALID_EMPTY.raw_len);
  EXPECT_FALSE(result.valid);
  EXPECT_STREQ(result.reject_reason, "too_short");
}

TEST(PacketParsing, InvalidPacket_TruncatedHeader) {
  // Only 10 bytes, but need at least 17 for header
  uint8_t truncated[] = {0x1D, 0x01, 0x6A, 0x00, 0x0A, 0x01, 0x05, 0xA8, 0x31, 0xE5};
  auto result = parse_packet(truncated, sizeof(truncated));
  EXPECT_FALSE(result.valid);
  EXPECT_STREQ(result.reject_reason, "truncated_header");
}

TEST(PacketParsing, InvalidPacket_TruncatedPayload) {
  // Header complete but payload truncated
  uint8_t truncated[] = {
    0x1D, 0x01, 0x6A, 0x00, 0x0A, 0x01, 0x05,  // header
    0xA8, 0x31, 0xE5,  // src
    0xA8, 0x31, 0xE5,  // bwd
    0xA8, 0x31, 0xE5,  // fwd
    0x01,              // num_dests
    0x12, 0x34, 0x56,  // dst
    0x00, 0x03,        // payload header
    // Missing payload data
  };
  auto result = parse_packet(truncated, sizeof(truncated));
  EXPECT_FALSE(result.valid);
  EXPECT_STREQ(result.reject_reason, "truncated_payload");
}

// ============================================================================
// Payload Encoding/Decoding Tests
// ============================================================================

TEST(PayloadEncoding, EncodeDecodePayload_Roundtrip) {
  using namespace esphome::elero::protocol;

  uint8_t original[] = {0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00};
  uint8_t buffer[8];
  memcpy(buffer, original, 8);

  msg_encode(buffer);
  msg_decode(buffer);

  // First 6 bytes should match after roundtrip
  for (int i = 0; i < 6; i++) {
    EXPECT_EQ(buffer[i], original[i]) << "Mismatch at index " << i;
  }
}

TEST(PayloadEncoding, EncodePayload_AllCommands) {
  using namespace esphome::elero::protocol;

  uint8_t commands[] = {CMD_CHECK, CMD_STOP, CMD_UP, CMD_TILT, CMD_DOWN, CMD_INT};

  for (uint8_t cmd : commands) {
    SCOPED_TRACE("Command: " + std::to_string(cmd));

    uint8_t payload[8] = {0, 0, 0, 0, cmd, 0, 0, 0};
    uint8_t original_cmd = payload[4];

    msg_encode(payload);
    msg_decode(payload);

    EXPECT_EQ(payload[4], original_cmd);
  }
}

TEST(PayloadEncoding, EncodePayload_AllStates) {
  using namespace esphome::elero::protocol;

  uint8_t states[] = {
    STATE_UNKNOWN, STATE_TOP, STATE_BOTTOM, STATE_INTERMEDIATE,
    STATE_TILT, STATE_BLOCKING, STATE_OVERHEATED, STATE_TIMEOUT,
    STATE_START_MOVING_UP, STATE_START_MOVING_DOWN,
    STATE_MOVING_UP, STATE_MOVING_DOWN, STATE_STOPPED,
    STATE_TOP_TILT, STATE_BOTTOM_TILT
  };

  for (uint8_t state : states) {
    SCOPED_TRACE("State: " + std::to_string(state));

    uint8_t payload[8] = {0, 0, 0, 0, 0, 0, state, 0};
    uint8_t original_state = payload[6];

    msg_encode(payload);
    msg_decode(payload);

    EXPECT_EQ(payload[6], original_state);
  }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
