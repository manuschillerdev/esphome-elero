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
  EXPECT_TRUE(is_command_packet(msg_type::COMMAND));
  EXPECT_TRUE(is_command_packet(msg_type::COMMAND_ALT));
  EXPECT_FALSE(is_command_packet(msg_type::STATUS));
  EXPECT_FALSE(is_command_packet(msg_type::STATUS_ALT));
  EXPECT_FALSE(is_command_packet(0x00));
  EXPECT_FALSE(is_command_packet(msg_type::BUTTON));
}

TEST(PacketHelpers, IsStatusPacket) {
  EXPECT_TRUE(is_status_packet(msg_type::STATUS));
  EXPECT_TRUE(is_status_packet(msg_type::STATUS_ALT));
  EXPECT_FALSE(is_status_packet(msg_type::COMMAND));
  EXPECT_FALSE(is_status_packet(msg_type::COMMAND_ALT));
  EXPECT_FALSE(is_status_packet(0x00));
  EXPECT_FALSE(is_status_packet(msg_type::BUTTON));
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

  using namespace esphome::elero::packet;
  uint8_t commands[] = {command::CHECK, command::STOP, command::UP, command::TILT, command::DOWN, command::INTERMEDIATE};

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

  using namespace esphome::elero::packet;
  uint8_t states[] = {
    state::UNKNOWN, state::TOP, state::BOTTOM, state::INTERMEDIATE,
    state::TILT, state::BLOCKING, state::OVERHEATED, state::TIMEOUT,
    state::START_MOVING_UP, state::START_MOVING_DOWN,
    state::MOVING_UP, state::MOVING_DOWN, state::STOPPED,
    state::TOP_TILT, state::BOTTOM_TILT
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
// TX Packet Building Tests
// ============================================================================

TEST(TxPacketBuilding, WriteAddr_BigEndian) {
  uint8_t buf[3] = {0};
  write_addr(buf, 0xA831E5);
  EXPECT_EQ(buf[0], 0xA8);
  EXPECT_EQ(buf[1], 0x31);
  EXPECT_EQ(buf[2], 0xE5);
}

TEST(TxPacketBuilding, CalcCryptoCode_Counter1) {
  // counter=1: code = (0 - 1*0x708f) & 0xffff = 0x8f71
  uint16_t code = calc_crypto_code(1);
  EXPECT_EQ(code, 0x8f71);
}

TEST(TxPacketBuilding, CalcCryptoCode_Counter255) {
  // counter=255: code = (0 - 255*0x708f) & 0xffff
  uint16_t code = calc_crypto_code(255);
  EXPECT_EQ(code, (0x0000 - (255 * 0x708f)) & 0xffff);
}

TEST(TxPacketBuilding, BuildTxPacket_HeaderFields) {
  TxParams params;
  params.counter = 42;
  params.dst_addr = 0x803238;
  params.src_addr = 0x4FCA30;
  params.channel = 5;
  params.type = msg_type::COMMAND;
  params.type2 = defaults::TYPE2;
  params.hop = defaults::HOP;
  params.command = command::UP;

  uint8_t buf[30] = {0};
  size_t len = build_tx_packet(params, buf);

  EXPECT_EQ(len, 30u);
  EXPECT_EQ(buf[tx_offset::LENGTH], TX_MSG_LENGTH);
  EXPECT_EQ(buf[tx_offset::COUNTER], 42);
  EXPECT_EQ(buf[tx_offset::TYPE], msg_type::COMMAND);
  EXPECT_EQ(buf[tx_offset::TYPE2], defaults::TYPE2);
  EXPECT_EQ(buf[tx_offset::HOP], defaults::HOP);
  EXPECT_EQ(buf[tx_offset::SYS], TX_SYS_ADDR);
  EXPECT_EQ(buf[tx_offset::CHANNEL], 5);
}

TEST(TxPacketBuilding, BuildTxPacket_Addresses) {
  TxParams params;
  params.dst_addr = 0x803238;
  params.src_addr = 0x4FCA30;

  uint8_t buf[30] = {0};
  build_tx_packet(params, buf);

  // Source address at offset 7
  EXPECT_EQ(extract_addr(&buf[tx_offset::SRC_ADDR]), 0x4FCA30u);
  // Backward address at offset 10 (same as src)
  EXPECT_EQ(extract_addr(&buf[tx_offset::BWD_ADDR]), 0x4FCA30u);
  // Forward address at offset 13 (same as src)
  EXPECT_EQ(extract_addr(&buf[tx_offset::FWD_ADDR]), 0x4FCA30u);
  // Destination count
  EXPECT_EQ(buf[tx_offset::NUM_DESTS], TX_DEST_COUNT);
  // Destination address at offset 17
  EXPECT_EQ(extract_addr(&buf[tx_offset::DST_ADDR]), 0x803238u);
}

TEST(TxPacketBuilding, BuildTxPacket_PayloadRoundtrip) {
  TxParams params;
  params.counter = 1;
  params.command = command::UP;

  uint8_t buf[30] = {0};
  build_tx_packet(params, buf);

  // Decrypt the payload to verify command is recoverable
  uint8_t payload[8];
  memcpy(payload, &buf[tx_offset::CRYPTO_CODE], 8);
  esphome::elero::protocol::msg_decode(payload);

  // OUR TX packets put command at encrypted_block[2] (position 24)
  // Note: This is different from payload_offset::COMMAND (4) which is
  // for parsing RX packets from OTHER remotes (sniffing).
  constexpr size_t TX_ENCRYPT_COMMAND_OFFSET = 2;
  EXPECT_EQ(payload[TX_ENCRYPT_COMMAND_OFFSET], command::UP);
}

// ============================================================================
// Cover State Mapping Tests
// ============================================================================

TEST(CoverStateMapping, MapState_Top) {
  auto result = map_cover_state(state::TOP);
  EXPECT_FLOAT_EQ(result.position, 1.0f);
  EXPECT_EQ(result.operation, CoverOp::IDLE);
  EXPECT_FALSE(result.is_warning);
}

TEST(CoverStateMapping, MapState_Bottom) {
  auto result = map_cover_state(state::BOTTOM);
  EXPECT_FLOAT_EQ(result.position, 0.0f);
  EXPECT_EQ(result.operation, CoverOp::IDLE);
  EXPECT_FALSE(result.is_warning);
}

TEST(CoverStateMapping, MapState_Intermediate) {
  auto result = map_cover_state(state::INTERMEDIATE);
  EXPECT_FLOAT_EQ(result.position, 0.5f);
  EXPECT_EQ(result.operation, CoverOp::IDLE);
  EXPECT_FALSE(result.is_warning);
}

TEST(CoverStateMapping, MapState_Tilt) {
  auto result = map_cover_state(state::TILT);
  EXPECT_FLOAT_EQ(result.position, 0.0f);
  EXPECT_FLOAT_EQ(result.tilt, 1.0f);
  EXPECT_EQ(result.operation, CoverOp::IDLE);
}

TEST(CoverStateMapping, MapState_TopTilt) {
  auto result = map_cover_state(state::TOP_TILT);
  EXPECT_FLOAT_EQ(result.position, 1.0f);
  EXPECT_FLOAT_EQ(result.tilt, 1.0f);
  EXPECT_EQ(result.operation, CoverOp::IDLE);
}

TEST(CoverStateMapping, MapState_BottomTilt) {
  auto result = map_cover_state(state::BOTTOM_TILT);
  EXPECT_FLOAT_EQ(result.position, 0.0f);
  EXPECT_FLOAT_EQ(result.tilt, 1.0f);
  EXPECT_EQ(result.operation, CoverOp::IDLE);
}

TEST(CoverStateMapping, MapState_MovingUp) {
  auto result = map_cover_state(state::MOVING_UP);
  EXPECT_FLOAT_EQ(result.position, -1.0f);  // Unchanged
  EXPECT_EQ(result.operation, CoverOp::OPENING);
  EXPECT_FALSE(result.is_warning);
}

TEST(CoverStateMapping, MapState_StartMovingUp) {
  auto result = map_cover_state(state::START_MOVING_UP);
  EXPECT_EQ(result.operation, CoverOp::OPENING);
}

TEST(CoverStateMapping, MapState_MovingDown) {
  auto result = map_cover_state(state::MOVING_DOWN);
  EXPECT_FLOAT_EQ(result.position, -1.0f);  // Unchanged
  EXPECT_EQ(result.operation, CoverOp::CLOSING);
  EXPECT_FALSE(result.is_warning);
}

TEST(CoverStateMapping, MapState_StartMovingDown) {
  auto result = map_cover_state(state::START_MOVING_DOWN);
  EXPECT_EQ(result.operation, CoverOp::CLOSING);
}

TEST(CoverStateMapping, MapState_Stopped) {
  auto result = map_cover_state(state::STOPPED);
  EXPECT_EQ(result.operation, CoverOp::IDLE);
  EXPECT_FALSE(result.is_warning);
}

TEST(CoverStateMapping, MapState_Blocking) {
  auto result = map_cover_state(state::BLOCKING);
  EXPECT_TRUE(result.is_warning);
  EXPECT_STREQ(result.warning_msg, "Obstacle detected");
  EXPECT_EQ(result.operation, CoverOp::IDLE);
}

TEST(CoverStateMapping, MapState_Overheated) {
  auto result = map_cover_state(state::OVERHEATED);
  EXPECT_TRUE(result.is_warning);
  EXPECT_STREQ(result.warning_msg, "Motor overheated");
  EXPECT_EQ(result.operation, CoverOp::IDLE);
}

TEST(CoverStateMapping, MapState_Timeout) {
  auto result = map_cover_state(state::TIMEOUT);
  EXPECT_TRUE(result.is_warning);
  EXPECT_STREQ(result.warning_msg, "Communication timeout");
  EXPECT_EQ(result.operation, CoverOp::IDLE);
}

TEST(CoverStateMapping, MapState_Unknown) {
  auto result = map_cover_state(state::UNKNOWN);
  EXPECT_FLOAT_EQ(result.position, -1.0f);  // Unchanged
  EXPECT_EQ(result.operation, CoverOp::IDLE);
  EXPECT_FALSE(result.is_warning);
}

TEST(CoverStateMapping, MapState_InvalidValue) {
  auto result = map_cover_state(0xFF);  // Unknown value
  EXPECT_FLOAT_EQ(result.position, -1.0f);  // Unchanged
  EXPECT_EQ(result.operation, CoverOp::IDLE);
  EXPECT_FALSE(result.is_warning);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
