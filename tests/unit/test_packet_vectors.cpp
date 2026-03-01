/// @file test_packet_vectors.cpp
/// @brief Tests for Elero RF packet parsing using real captured data.
///
/// This file tests the complete packet parsing pipeline:
/// 1. Packet validation (length, structure)
/// 2. Header field extraction (addresses, channel, type)
/// 3. Payload decryption
/// 4. State/command byte extraction

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "elero/elero_protocol.h"
#include "test_vectors.h"

using namespace esphome::elero::protocol;
using namespace esphome::elero::test_vectors;

// ============================================================================
// Packet Parser (extracted from elero.cpp for testability)
// ============================================================================

namespace {

/// Maximum packet size per FCC documents
constexpr uint8_t MAX_PACKET_SIZE = 57;

/// CC1101 FIFO length
constexpr uint8_t FIFO_LENGTH = 64;

/// Result of packet parsing
struct ParseResult {
  bool valid{false};
  const char *reject_reason{nullptr};

  // Header fields
  uint8_t length{0};
  uint8_t counter{0};
  uint8_t type{0};
  uint8_t type2{0};
  uint8_t hop{0};
  uint8_t syst{0};
  uint8_t channel{0};
  uint32_t src_addr{0};
  uint32_t bwd_addr{0};
  uint32_t fwd_addr{0};
  uint8_t num_dests{0};
  uint32_t dst_addr{0};
  uint8_t payload1{0};
  uint8_t payload2{0};

  // RSSI/LQI
  float rssi{0.0f};
  uint8_t lqi{0};
  bool crc_ok{false};

  // Decoded payload
  uint8_t payload[8]{0};
  uint8_t command{0};   // payload[4] for command packets
  uint8_t state{0};     // payload[6] for status packets
};

/// Extract 3-byte big-endian address
inline uint32_t extract_addr(const uint8_t *p) {
  return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

/// Calculate RSSI in dBm from CC1101 raw value
inline float calc_rssi(uint8_t raw) {
  constexpr int8_t RSSI_OFFSET = -74;
  if (raw > 127) {
    return static_cast<float>(static_cast<int8_t>(raw)) / 2.0f + RSSI_OFFSET;
  }
  return static_cast<float>(raw) / 2.0f + RSSI_OFFSET;
}

/// Parse a raw packet from CC1101 FIFO
/// This mirrors the logic in Elero::interpret_msg()
ParseResult parse_packet(const uint8_t *raw, size_t raw_len) {
  ParseResult r;

  // Minimum viable packet
  if (raw_len < 4) {
    r.reject_reason = "too_short";
    return r;
  }

  r.length = raw[0];

  // Check packet length
  if (r.length > MAX_PACKET_SIZE) {
    r.reject_reason = "too_long";
    return r;
  }

  // Need enough bytes for header parsing
  if (raw_len < 17) {
    r.reject_reason = "truncated_header";
    return r;
  }

  r.counter = raw[1];
  r.type = raw[2];
  r.type2 = raw[3];
  r.hop = raw[4];
  r.syst = raw[5];
  r.channel = raw[6];
  r.src_addr = extract_addr(&raw[7]);
  r.bwd_addr = extract_addr(&raw[10]);
  r.fwd_addr = extract_addr(&raw[13]);
  r.num_dests = raw[16];

  // Validate destination count
  if (r.num_dests > 20) {
    r.reject_reason = "too_many_dests";
    return r;
  }

  // Calculate dests_len based on packet type
  uint8_t dests_len;
  if (r.type > 0x60) {
    dests_len = r.num_dests * 3;
    if (raw_len >= 20) {
      r.dst_addr = extract_addr(&raw[17]);
    }
  } else {
    dests_len = r.num_dests;
    if (raw_len >= 18) {
      r.dst_addr = raw[17];
    }
  }

  // Validate we have enough data for payload
  size_t payload_start = 19 + dests_len;
  size_t payload_end = payload_start + 8;

  if (payload_end > raw_len) {
    r.reject_reason = "truncated_payload";
    return r;
  }

  // Bounds check for RSSI/LQI
  if (r.length + 2 >= raw_len || r.length + 2 >= FIFO_LENGTH) {
    r.reject_reason = "rssi_oob";
    return r;
  }

  // Additional sanity check
  if (26 + dests_len > r.length) {
    r.reject_reason = "dests_len_too_long";
    return r;
  }

  // Extract payload bytes before decryption
  r.payload1 = raw[17 + dests_len];
  r.payload2 = raw[18 + dests_len];

  // Copy and decrypt payload
  memcpy(r.payload, &raw[payload_start], 8);
  msg_decode(r.payload);

  // Extract command/state from decrypted payload
  r.command = r.payload[4];
  r.state = r.payload[6];

  // Extract RSSI/LQI (appended by CC1101)
  uint8_t rssi_raw = raw[r.length + 1];
  uint8_t lqi_crc = raw[r.length + 2];
  r.rssi = calc_rssi(rssi_raw);
  r.crc_ok = (lqi_crc >> 7) != 0;
  r.lqi = lqi_crc & 0x7f;

  r.valid = true;
  return r;
}

/// Check if packet is a command packet (remote → blind)
inline bool is_command_packet(uint8_t type) {
  return type == PKT_TYPE_COMMAND || type == PKT_TYPE_COMMAND_ALT;
}

/// Check if packet is a status packet (blind → remote)
inline bool is_status_packet(uint8_t type) {
  return type == PKT_TYPE_STATUS || type == PKT_TYPE_STATUS_ALT;
}

/// Get human-readable state name
const char *state_name(uint8_t state) {
  switch (state) {
    case STATE_TOP: return "TOP";
    case STATE_BOTTOM: return "BOTTOM";
    case STATE_INTERMEDIATE: return "INTERMEDIATE";
    case STATE_TILT: return "TILT";
    case STATE_BLOCKING: return "BLOCKING";
    case STATE_OVERHEATED: return "OVERHEATED";
    case STATE_TIMEOUT: return "TIMEOUT";
    case STATE_START_MOVING_UP: return "START_MOVING_UP";
    case STATE_START_MOVING_DOWN: return "START_MOVING_DOWN";
    case STATE_MOVING_UP: return "MOVING_UP";
    case STATE_MOVING_DOWN: return "MOVING_DOWN";
    case STATE_STOPPED: return "STOPPED";
    case STATE_TOP_TILT: return "TOP_TILT";
    case STATE_BOTTOM_TILT: return "BOTTOM_TILT";
    default: return "UNKNOWN";
  }
}

/// Get human-readable command name
const char *command_name(uint8_t cmd) {
  switch (cmd) {
    case CMD_CHECK: return "CHECK";
    case CMD_STOP: return "STOP";
    case CMD_UP: return "UP";
    case CMD_TILT: return "TILT";
    case CMD_DOWN: return "DOWN";
    case CMD_INT: return "INT";
    default: return "UNKNOWN";
  }
}

}  // namespace

// ============================================================================
// Test Fixtures
// ============================================================================

class PacketParserTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  // Helper to run a vector test
  void RunVectorTest(const PacketVector &vec) {
    ParseResult r = parse_packet(vec.raw, vec.raw_len);

    SCOPED_TRACE(std::string("Vector: ") + vec.name);
    SCOPED_TRACE(std::string("Description: ") + vec.description);

    // Check validity
    EXPECT_EQ(r.valid, vec.expect_valid)
        << "Expected valid=" << vec.expect_valid
        << ", got valid=" << r.valid
        << ", reason=" << (r.reject_reason ? r.reject_reason : "none");

    if (!vec.expect_valid) {
      // For invalid packets, check rejection reason
      ASSERT_NE(r.reject_reason, nullptr);
      EXPECT_STREQ(r.reject_reason, vec.reject_reason);
      return;
    }

    // For valid packets, check header fields
    EXPECT_EQ(r.type, vec.exp_type);
    EXPECT_EQ(r.channel, vec.exp_channel);

    if (vec.exp_src_addr != 0) {
      EXPECT_EQ(r.src_addr, vec.exp_src_addr)
          << "Expected src_addr=0x" << std::hex << vec.exp_src_addr
          << ", got 0x" << r.src_addr;
    }

    if (vec.exp_dst_addr != 0) {
      EXPECT_EQ(r.dst_addr, vec.exp_dst_addr)
          << "Expected dst_addr=0x" << std::hex << vec.exp_dst_addr
          << ", got 0x" << r.dst_addr;
    }

    // Check decoded values based on packet type
    if (is_command_packet(r.type)) {
      EXPECT_EQ(r.command, vec.exp_command)
          << "Expected command=" << command_name(vec.exp_command)
          << ", got " << command_name(r.command);
    }

    if (is_status_packet(r.type)) {
      EXPECT_EQ(r.state, vec.exp_state)
          << "Expected state=" << state_name(vec.exp_state)
          << ", got " << state_name(r.state);
    }
  }
};

// ============================================================================
// Helper Function Tests
// ============================================================================

TEST_F(PacketParserTest, ExtractAddress_BigEndian) {
  uint8_t bytes[] = {0xa8, 0x31, 0xe5};
  EXPECT_EQ(extract_addr(bytes), 0xa831e5u);
}

TEST_F(PacketParserTest, ExtractAddress_Zero) {
  uint8_t bytes[] = {0x00, 0x00, 0x00};
  EXPECT_EQ(extract_addr(bytes), 0u);
}

TEST_F(PacketParserTest, ExtractAddress_Max) {
  uint8_t bytes[] = {0xff, 0xff, 0xff};
  EXPECT_EQ(extract_addr(bytes), 0xffffffu);
}

TEST_F(PacketParserTest, CalcRssi_PositiveRaw) {
  // raw=100: 100/2 + (-74) = 50 - 74 = -24 dBm
  EXPECT_NEAR(calc_rssi(100), -24.0f, 0.1f);
}

TEST_F(PacketParserTest, CalcRssi_NegativeRaw) {
  // raw=200 (signed = -56): -56/2 + (-74) = -28 - 74 = -102 dBm
  EXPECT_NEAR(calc_rssi(200), -102.0f, 0.1f);
}

TEST_F(PacketParserTest, CalcRssi_Boundary127) {
  // raw=127: 127/2 + (-74) = 63.5 - 74 = -10.5 dBm
  EXPECT_NEAR(calc_rssi(127), -10.5f, 0.1f);
}

TEST_F(PacketParserTest, CalcRssi_Boundary128) {
  // raw=128 (signed = -128): -128/2 + (-74) = -64 - 74 = -138 dBm
  EXPECT_NEAR(calc_rssi(128), -138.0f, 0.1f);
}

TEST_F(PacketParserTest, IsCommandPacket) {
  EXPECT_TRUE(is_command_packet(0x6a));
  EXPECT_TRUE(is_command_packet(0x69));
  EXPECT_FALSE(is_command_packet(0xca));
  EXPECT_FALSE(is_command_packet(0xc9));
  EXPECT_FALSE(is_command_packet(0x00));
}

TEST_F(PacketParserTest, IsStatusPacket) {
  EXPECT_TRUE(is_status_packet(0xca));
  EXPECT_TRUE(is_status_packet(0xc9));
  EXPECT_FALSE(is_status_packet(0x6a));
  EXPECT_FALSE(is_status_packet(0x69));
  EXPECT_FALSE(is_status_packet(0x00));
}

// ============================================================================
// Invalid Packet Tests (always run - no real data needed)
// ============================================================================

TEST_F(PacketParserTest, InvalidPacket_TooLong) {
  RunVectorTest(VEC_INVALID_TOO_LONG);
}

TEST_F(PacketParserTest, InvalidPacket_TooManyDests) {
  RunVectorTest(VEC_INVALID_TOO_MANY_DESTS);
}

TEST_F(PacketParserTest, InvalidPacket_TooShort) {
  const uint8_t raw[] = {0x1d, 0x01};  // Only 2 bytes
  ParseResult r = parse_packet(raw, sizeof(raw));
  EXPECT_FALSE(r.valid);
  EXPECT_STREQ(r.reject_reason, "too_short");
}

TEST_F(PacketParserTest, InvalidPacket_Empty) {
  ParseResult r = parse_packet(nullptr, 0);
  EXPECT_FALSE(r.valid);
  EXPECT_STREQ(r.reject_reason, "too_short");
}

// ============================================================================
// Real Packet Vector Tests (enable by adding data to test_vectors.h)
// ============================================================================

// STATUS PACKETS (blind → remote)

TEST_F(PacketParserTest, DISABLED_Vector_StatusTop) {
  // Uncomment VEC_STATUS_TOP in test_vectors.h and enable this test
  // RunVectorTest(VEC_STATUS_TOP);
}

TEST_F(PacketParserTest, DISABLED_Vector_StatusBottom) {
  // RunVectorTest(VEC_STATUS_BOTTOM);
}

TEST_F(PacketParserTest, DISABLED_Vector_StatusIntermediate) {
  // RunVectorTest(VEC_STATUS_INTERMEDIATE);
}

TEST_F(PacketParserTest, DISABLED_Vector_StatusMovingUp) {
  // RunVectorTest(VEC_STATUS_MOVING_UP);
}

TEST_F(PacketParserTest, DISABLED_Vector_StatusMovingDown) {
  // RunVectorTest(VEC_STATUS_MOVING_DOWN);
}

TEST_F(PacketParserTest, DISABLED_Vector_StatusStopped) {
  // RunVectorTest(VEC_STATUS_STOPPED);
}

// COMMAND PACKETS (remote → blind)

TEST_F(PacketParserTest, DISABLED_Vector_CommandUp) {
  // RunVectorTest(VEC_CMD_UP);
}

TEST_F(PacketParserTest, DISABLED_Vector_CommandDown) {
  // RunVectorTest(VEC_CMD_DOWN);
}

TEST_F(PacketParserTest, DISABLED_Vector_CommandStop) {
  // RunVectorTest(VEC_CMD_STOP);
}

TEST_F(PacketParserTest, DISABLED_Vector_CommandTilt) {
  // RunVectorTest(VEC_CMD_TILT);
}

TEST_F(PacketParserTest, DISABLED_Vector_CommandCheck) {
  // RunVectorTest(VEC_CMD_CHECK);
}

// ============================================================================
// Parameterized Test (runs all vectors in a loop)
// ============================================================================

// Uncomment when you have vectors defined:
// class PacketVectorParamTest : public PacketParserTest,
//                               public ::testing::WithParamInterface<const PacketVector*> {};
//
// TEST_P(PacketVectorParamTest, ParseVector) {
//   RunVectorTest(*GetParam());
// }
//
// INSTANTIATE_TEST_SUITE_P(
//     AllVectors,
//     PacketVectorParamTest,
//     ::testing::ValuesIn(ALL_VECTORS, ALL_VECTORS + NUM_VECTORS),
//     [](const ::testing::TestParamInfo<const PacketVector*>& info) {
//       return std::string(info.param->name);
//     }
// );

// ============================================================================
// Encode-Decode Roundtrip Tests
// ============================================================================

TEST_F(PacketParserTest, EncodeDecodePayload_Roundtrip) {
  // Test that encoding then decoding returns to original
  uint8_t original[8] = {0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00};
  uint8_t payload[8];
  memcpy(payload, original, 8);

  msg_encode(payload);

  // Should be different after encoding
  EXPECT_NE(memcmp(payload, original, 8), 0);

  msg_decode(payload);

  // First 6 bytes should match (parity byte may differ)
  for (int i = 0; i < 6; i++) {
    EXPECT_EQ(payload[i], original[i]) << "Mismatch at payload[" << i << "]";
  }
}

TEST_F(PacketParserTest, EncodePayload_CommandUp) {
  // Build a payload with UP command
  uint8_t payload[8] = {0x00, 0x00, 0x00, 0x00, CMD_UP, 0x00, 0x00, 0x00};
  uint8_t encoded[8];
  memcpy(encoded, payload, 8);

  msg_encode(encoded);

  // Decode and verify command is recovered
  msg_decode(encoded);
  EXPECT_EQ(encoded[4], CMD_UP);
}

TEST_F(PacketParserTest, EncodePayload_AllCommands) {
  const uint8_t commands[] = {CMD_CHECK, CMD_STOP, CMD_UP, CMD_TILT, CMD_DOWN, CMD_INT};

  for (uint8_t cmd : commands) {
    SCOPED_TRACE(std::string("Command: ") + command_name(cmd));

    uint8_t payload[8] = {0x00, 0x00, 0x00, 0x00, cmd, 0x00, 0x00, 0x00};
    msg_encode(payload);
    msg_decode(payload);
    EXPECT_EQ(payload[4], cmd);
  }
}

TEST_F(PacketParserTest, EncodePayload_AllStates) {
  const uint8_t states[] = {
    STATE_TOP, STATE_BOTTOM, STATE_INTERMEDIATE, STATE_TILT,
    STATE_BLOCKING, STATE_OVERHEATED, STATE_TIMEOUT,
    STATE_START_MOVING_UP, STATE_START_MOVING_DOWN,
    STATE_MOVING_UP, STATE_MOVING_DOWN, STATE_STOPPED,
    STATE_TOP_TILT, STATE_BOTTOM_TILT
  };

  for (uint8_t state : states) {
    SCOPED_TRACE(std::string("State: ") + state_name(state));

    uint8_t payload[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, state, 0x00};
    msg_encode(payload);
    msg_decode(payload);
    EXPECT_EQ(payload[6], state);
  }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
