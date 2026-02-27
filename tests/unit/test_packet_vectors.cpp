/// @file test_packet_vectors.cpp
/// @brief Tests using real captured RF packets from Elero hardware.
///
/// These tests validate that our protocol implementation correctly handles
/// packets from actual Elero blinds and remotes.

#include <gtest/gtest.h>
#include <cstring>
#include "elero/elero_protocol.h"
#include "test_vectors.h"

using namespace esphome::elero::protocol;
using namespace esphome::elero::test_vectors;

// ============================================================================
// Helper functions for packet parsing (extracted from elero.cpp for testing)
// ============================================================================

namespace {

/// Extract 3-byte address from packet buffer (big-endian)
inline uint32_t extract_address(const uint8_t *buf) {
  return ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
}

/// Calculate RSSI in dBm from CC1101 raw value
inline float calc_rssi(uint8_t raw_rssi) {
  constexpr int8_t RSSI_OFFSET = -74;
  if (raw_rssi >= 128) {
    return ((int16_t)raw_rssi - 256) / 2.0f + RSSI_OFFSET;
  } else {
    return raw_rssi / 2.0f + RSSI_OFFSET;
  }
}

/// Packet type bytes
constexpr uint8_t PKT_TYPE_COMMAND = 0x6a;
constexpr uint8_t PKT_TYPE_COMMAND_ALT = 0x69;
constexpr uint8_t PKT_TYPE_STATUS = 0xca;
constexpr uint8_t PKT_TYPE_STATUS_ALT = 0xc9;

/// Packet structure offsets (after length byte)
constexpr size_t OFF_TYPE = 1;
constexpr size_t OFF_COUNTER = 2;
constexpr size_t OFF_BLIND_ADDR = 3;    // 3 bytes
constexpr size_t OFF_REMOTE_ADDR = 6;   // 3 bytes
constexpr size_t OFF_CHANNEL = 9;
constexpr size_t OFF_PCK_INF = 10;      // 2 bytes
constexpr size_t OFF_HOP = 12;
constexpr size_t OFF_PAYLOAD = 13;      // 8 bytes (encrypted)
constexpr size_t OFF_CHECKSUM = 21;

/// Minimum valid packet length (excluding RSSI/LQI appended by CC1101)
constexpr size_t MIN_PACKET_LEN = 22;

/// Decode payload in-place and return state byte
uint8_t decode_payload_get_state(uint8_t *payload) {
  msg_decode(payload);
  return payload[6];
}

/// Decode payload in-place and return command byte
uint8_t decode_payload_get_command(uint8_t *payload) {
  msg_decode(payload);
  return payload[4];
}

}  // namespace

// ============================================================================
// Test fixture
// ============================================================================

class PacketVectorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    memset(payload_buf_, 0, sizeof(payload_buf_));
  }

  uint8_t payload_buf_[8];
};

// ============================================================================
// Basic packet structure tests
// ============================================================================

TEST_F(PacketVectorTest, AddressExtraction_KnownValue) {
  uint8_t addr_bytes[] = {0xa8, 0x31, 0xe5};
  EXPECT_EQ(extract_address(addr_bytes), 0xa831e5u);
}

TEST_F(PacketVectorTest, AddressExtraction_Zero) {
  uint8_t addr_bytes[] = {0x00, 0x00, 0x00};
  EXPECT_EQ(extract_address(addr_bytes), 0u);
}

TEST_F(PacketVectorTest, AddressExtraction_Max) {
  uint8_t addr_bytes[] = {0xff, 0xff, 0xff};
  EXPECT_EQ(extract_address(addr_bytes), 0xffffffu);
}

TEST_F(PacketVectorTest, RssiCalculation_Positive) {
  // Raw value < 128: RSSI = raw/2 + offset
  // Raw 100 -> 100/2 + (-74) = 50 - 74 = -24 dBm
  EXPECT_NEAR(calc_rssi(100), -24.0f, 0.1f);
}

TEST_F(PacketVectorTest, RssiCalculation_Negative) {
  // Raw value >= 128: RSSI = (raw-256)/2 + offset
  // Raw 200 -> (200-256)/2 + (-74) = -28 - 74 = -102 dBm
  EXPECT_NEAR(calc_rssi(200), -102.0f, 0.1f);
}

// ============================================================================
// Placeholder tests for real packet vectors
// ============================================================================

// These tests will be enabled once you provide actual captured packets.
// For now, they demonstrate the test structure.

TEST_F(PacketVectorTest, DISABLED_DecodeStatusPacket_Top) {
  // TODO: Replace with actual captured packet
  // uint8_t packet[] = { ... };
  //
  // // Verify packet structure
  // EXPECT_GE(sizeof(packet), MIN_PACKET_LEN);
  // EXPECT_EQ(packet[OFF_TYPE], PKT_TYPE_STATUS);
  //
  // // Extract addresses
  // uint32_t blind_addr = extract_address(&packet[OFF_BLIND_ADDR]);
  // uint32_t remote_addr = extract_address(&packet[OFF_REMOTE_ADDR]);
  // uint8_t channel = packet[OFF_CHANNEL];
  //
  // EXPECT_EQ(blind_addr, EXPECTED_BLIND_ADDR);
  // EXPECT_EQ(remote_addr, EXPECTED_REMOTE_ADDR);
  // EXPECT_EQ(channel, EXPECTED_CHANNEL);
  //
  // // Decode payload and check state
  // memcpy(payload_buf_, &packet[OFF_PAYLOAD], 8);
  // uint8_t state = decode_payload_get_state(payload_buf_);
  // EXPECT_EQ(state, 0x01);  // ELERO_STATE_TOP
}

TEST_F(PacketVectorTest, DISABLED_DecodeStatusPacket_Bottom) {
  // TODO: Replace with actual captured packet
}

TEST_F(PacketVectorTest, DISABLED_DecodeStatusPacket_MovingUp) {
  // TODO: Replace with actual captured packet
}

TEST_F(PacketVectorTest, DISABLED_DecodeStatusPacket_MovingDown) {
  // TODO: Replace with actual captured packet
}

TEST_F(PacketVectorTest, DISABLED_DecodeCommandPacket_Up) {
  // TODO: Replace with actual captured packet
  //
  // // Verify packet structure
  // EXPECT_EQ(packet[OFF_TYPE], PKT_TYPE_COMMAND);
  //
  // // Decode payload and check command byte
  // memcpy(payload_buf_, &packet[OFF_PAYLOAD], 8);
  // uint8_t cmd = decode_payload_get_command(payload_buf_);
  // EXPECT_EQ(cmd, 0x20);  // ELERO_COMMAND_COVER_UP
}

TEST_F(PacketVectorTest, DISABLED_DecodeCommandPacket_Down) {
  // TODO: Replace with actual captured packet
}

TEST_F(PacketVectorTest, DISABLED_DecodeCommandPacket_Stop) {
  // TODO: Replace with actual captured packet
}

// ============================================================================
// Encode-then-compare tests (generate packets and compare to captured)
// ============================================================================

TEST_F(PacketVectorTest, DISABLED_EncodeCommand_MatchesCaptured) {
  // TODO: Once we have a captured command packet, we can:
  // 1. Build the same command using our encoder
  // 2. Compare the encrypted payload to the captured one
  //
  // This validates our encoder produces identical output to a real remote.
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
