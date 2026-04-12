/// @file test_group_packet.cpp
/// @brief Unit tests for build_group_button_packet() — multi-dest 0x44 TX.

#include <gtest/gtest.h>
#include <cstring>
#include "elero/elero_packet.h"

using namespace esphome::elero::packet;

// ============================================================================
// Validation / Rejection Tests
// ============================================================================

TEST(GroupPacketBuilding, RejectsNumDestsZero) {
  uint8_t channels[] = {1, 2};
  GroupButtonTxParams params;
  params.num_dests = 0;
  params.dest_channels = channels;

  uint8_t buf[FIFO_LENGTH] = {0};
  EXPECT_EQ(build_group_button_packet(params, buf), 0u);
}

TEST(GroupPacketBuilding, RejectsNumDestsOne) {
  uint8_t channels[] = {1};
  GroupButtonTxParams params;
  params.num_dests = 1;
  params.dest_channels = channels;

  uint8_t buf[FIFO_LENGTH] = {0};
  EXPECT_EQ(build_group_button_packet(params, buf), 0u);
}

TEST(GroupPacketBuilding, RejectsNullChannels) {
  GroupButtonTxParams params;
  params.num_dests = 2;
  params.dest_channels = nullptr;

  uint8_t buf[FIFO_LENGTH] = {0};
  EXPECT_EQ(build_group_button_packet(params, buf), 0u);
}

TEST(GroupPacketBuilding, RejectsFifoOverflow) {
  // GROUP_MAX_DESTS = 37, so 38 should fail
  uint8_t channels[38];
  memset(channels, 1, sizeof(channels));

  GroupButtonTxParams params;
  params.num_dests = 38;
  params.dest_channels = channels;

  uint8_t buf[FIFO_LENGTH] = {0};
  EXPECT_EQ(build_group_button_packet(params, buf), 0u);
}

TEST(GroupPacketBuilding, AcceptsMaxDests) {
  // GROUP_MAX_DESTS = 37: should succeed
  uint8_t channels[GROUP_MAX_DESTS];
  for (uint8_t i = 0; i < GROUP_MAX_DESTS; ++i) channels[i] = i + 1;

  GroupButtonTxParams params;
  params.num_dests = GROUP_MAX_DESTS;
  params.dest_channels = channels;
  params.src_addr = 0x4FCA30;
  params.counter = 1;
  params.command = command::CHECK;

  uint8_t buf[FIFO_LENGTH] = {0};
  size_t len = build_group_button_packet(params, buf);

  // len = 26 + 37 + 1 (length byte) = 64 = FIFO_LENGTH
  EXPECT_EQ(len, static_cast<size_t>(button::GROUP_BASE_LENGTH + GROUP_MAX_DESTS + 1));
  EXPECT_EQ(len, static_cast<size_t>(FIFO_LENGTH));
}

// ============================================================================
// Packet Structure Tests (2-dest)
// ============================================================================

TEST(GroupPacketBuilding, HeaderFields_TwoDest) {
  uint8_t channels[] = {3, 7};
  GroupButtonTxParams params;
  params.counter = 42;
  params.src_addr = 0x4FCA30;
  params.command = command::UP;
  params.num_dests = 2;
  params.dest_channels = channels;

  uint8_t buf[FIFO_LENGTH] = {0};
  size_t len = build_group_button_packet(params, buf);

  // len = 26 + 2 = 28 packet bytes + 1 length byte = 29
  EXPECT_EQ(len, 29u);
  EXPECT_EQ(buf[pkt_offset::LENGTH], 28);  // 26 + 2
  EXPECT_EQ(buf[pkt_offset::COUNTER], 42);
  EXPECT_EQ(buf[pkt_offset::TYPE], msg_type::BUTTON);  // 0x44
  EXPECT_EQ(buf[pkt_offset::TYPE2], button::TYPE2);     // 0x10
  EXPECT_EQ(buf[pkt_offset::HOP], button::HOP);         // 0x00
  EXPECT_EQ(buf[pkt_offset::SYS], TX_SYS_ADDR);
  EXPECT_EQ(buf[pkt_offset::CHANNEL], button::GROUP_CHANNEL);  // 0x00 group marker
}

TEST(GroupPacketBuilding, Addresses_TwoDest) {
  uint8_t channels[] = {3, 7};
  GroupButtonTxParams params;
  params.src_addr = 0x4FCA30;
  params.num_dests = 2;
  params.dest_channels = channels;

  uint8_t buf[FIFO_LENGTH] = {0};
  build_group_button_packet(params, buf);

  EXPECT_EQ(extract_addr(&buf[pkt_offset::SRC_ADDR]), 0x4FCA30u);
  EXPECT_EQ(extract_addr(&buf[pkt_offset::BWD_ADDR]), 0x4FCA30u);
  EXPECT_EQ(extract_addr(&buf[pkt_offset::FWD_ADDR]), 0x4FCA30u);
}

TEST(GroupPacketBuilding, DestChannels_TwoDest) {
  uint8_t channels[] = {3, 7};
  GroupButtonTxParams params;
  params.num_dests = 2;
  params.dest_channels = channels;

  uint8_t buf[FIFO_LENGTH] = {0};
  build_group_button_packet(params, buf);

  EXPECT_EQ(buf[pkt_offset::NUM_DESTS], 2);
  EXPECT_EQ(buf[pkt_offset::FIRST_DEST], 3);      // channels[0]
  EXPECT_EQ(buf[pkt_offset::FIRST_DEST + 1], 7);  // channels[1]
}

TEST(GroupPacketBuilding, PayloadBytes_TwoDest) {
  uint8_t channels[] = {3, 7};
  GroupButtonTxParams params;
  params.num_dests = 2;
  params.dest_channels = channels;

  uint8_t buf[FIFO_LENGTH] = {0};
  build_group_button_packet(params, buf);

  // payload_1 and payload_2 are at FIRST_DEST + num_dests
  size_t payload_start = pkt_offset::FIRST_DEST + 2;
  EXPECT_EQ(buf[payload_start], defaults::PAYLOAD_1);      // 0x00
  EXPECT_EQ(buf[payload_start + 1], defaults::PAYLOAD_2);  // 0x04
}

TEST(GroupPacketBuilding, EncryptedCommandRoundtrip_TwoDest) {
  uint8_t channels[] = {3, 7};
  GroupButtonTxParams params;
  params.counter = 5;
  params.command = command::DOWN;
  params.num_dests = 2;
  params.dest_channels = channels;

  uint8_t buf[FIFO_LENGTH] = {0};
  build_group_button_packet(params, buf);

  // Encrypted section starts at FIRST_DEST + num_dests + 2
  size_t enc_start = pkt_offset::FIRST_DEST + 2 + 2;
  uint8_t payload[8];
  memcpy(payload, &buf[enc_start], 8);
  esphome::elero::protocol::msg_decode(payload);

  EXPECT_EQ(payload[payload_offset::COMMAND], command::DOWN);
}

// ============================================================================
// Packet Structure Tests (5-dest — more realistic group)
// ============================================================================

TEST(GroupPacketBuilding, HeaderFields_FiveDest) {
  uint8_t channels[] = {1, 2, 3, 5, 8};
  GroupButtonTxParams params;
  params.counter = 10;
  params.src_addr = 0xA831E5;
  params.command = command::STOP;
  params.num_dests = 5;
  params.dest_channels = channels;

  uint8_t buf[FIFO_LENGTH] = {0};
  size_t len = build_group_button_packet(params, buf);

  // len = 26 + 5 + 1 = 32
  EXPECT_EQ(len, 32u);
  EXPECT_EQ(buf[pkt_offset::LENGTH], 31);  // 26 + 5
  EXPECT_EQ(buf[pkt_offset::CHANNEL], button::GROUP_CHANNEL);
}

TEST(GroupPacketBuilding, DestChannels_FiveDest) {
  uint8_t channels[] = {1, 2, 3, 5, 8};
  GroupButtonTxParams params;
  params.num_dests = 5;
  params.dest_channels = channels;

  uint8_t buf[FIFO_LENGTH] = {0};
  build_group_button_packet(params, buf);

  EXPECT_EQ(buf[pkt_offset::NUM_DESTS], 5);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(buf[pkt_offset::FIRST_DEST + i], channels[i]);
  }
}

TEST(GroupPacketBuilding, EncryptedCommandRoundtrip_FiveDest) {
  uint8_t channels[] = {1, 2, 3, 5, 8};
  GroupButtonTxParams params;
  params.counter = 10;
  params.command = command::STOP;
  params.num_dests = 5;
  params.dest_channels = channels;

  uint8_t buf[FIFO_LENGTH] = {0};
  build_group_button_packet(params, buf);

  // Encrypted section at FIRST_DEST + 5 + 2
  size_t enc_start = pkt_offset::FIRST_DEST + 5 + 2;
  uint8_t payload[8];
  memcpy(payload, &buf[enc_start], 8);
  esphome::elero::protocol::msg_decode(payload);

  EXPECT_EQ(payload[payload_offset::COMMAND], command::STOP);
}

// ============================================================================
// All Commands Roundtrip
// ============================================================================

TEST(GroupPacketBuilding, AllCommandsRoundtrip) {
  uint8_t channels[] = {1, 3};
  uint8_t commands[] = {
    command::CHECK, command::STOP, command::UP,
    command::TILT, command::DOWN, command::INTERMEDIATE,
    button::RELEASE
  };

  for (uint8_t cmd : commands) {
    SCOPED_TRACE("Command: 0x" + std::to_string(cmd));

    GroupButtonTxParams params;
    params.counter = 1;
    params.command = cmd;
    params.num_dests = 2;
    params.dest_channels = channels;

    uint8_t buf[FIFO_LENGTH] = {0};
    size_t len = build_group_button_packet(params, buf);
    EXPECT_GT(len, 0u);

    // Decrypt and verify
    size_t enc_start = pkt_offset::FIRST_DEST + 2 + 2;
    uint8_t payload[8];
    memcpy(payload, &buf[enc_start], 8);
    esphome::elero::protocol::msg_decode(payload);

    EXPECT_EQ(payload[payload_offset::COMMAND], cmd);
  }
}

// ============================================================================
// Comparison with Single-Dest Button Packet
// ============================================================================

TEST(GroupPacketBuilding, LongerThanSingleDest) {
  // Group packet with 2 dests should be longer than single-dest button
  uint8_t channels[] = {3, 7};
  GroupButtonTxParams group_params;
  group_params.counter = 1;
  group_params.command = command::UP;
  group_params.num_dests = 2;
  group_params.dest_channels = channels;

  ButtonTxParams single_params;
  single_params.counter = 1;
  single_params.command = command::UP;
  single_params.channel = 3;

  uint8_t group_buf[FIFO_LENGTH] = {0};
  uint8_t single_buf[FIFO_LENGTH] = {0};

  size_t group_len = build_group_button_packet(group_params, group_buf);
  size_t single_len = build_button_packet(single_params, single_buf);

  EXPECT_GT(group_len, single_len);
  // Single: 28 (27+1), Group 2-dest: 29 (28+1)
  EXPECT_EQ(single_len, 28u);
  EXPECT_EQ(group_len, 29u);
}

TEST(GroupPacketBuilding, SharedHeaderLayout) {
  // Bytes 0-6 (header) and 7-15 (addresses) should match between
  // group and single-dest when using same counter/src/type2/hop
  uint8_t channels[] = {3, 7};
  GroupButtonTxParams group_params;
  group_params.counter = 42;
  group_params.src_addr = 0x4FCA30;
  group_params.command = command::UP;
  group_params.num_dests = 2;
  group_params.dest_channels = channels;

  ButtonTxParams single_params;
  single_params.counter = 42;
  single_params.src_addr = 0x4FCA30;
  single_params.command = command::UP;
  single_params.channel = 3;

  uint8_t group_buf[FIFO_LENGTH] = {0};
  uint8_t single_buf[FIFO_LENGTH] = {0};

  build_group_button_packet(group_params, group_buf);
  build_button_packet(single_params, single_buf);

  // Counter, type, type2, hop, sys should match
  EXPECT_EQ(group_buf[pkt_offset::COUNTER], single_buf[pkt_offset::COUNTER]);
  EXPECT_EQ(group_buf[pkt_offset::TYPE], single_buf[pkt_offset::TYPE]);
  EXPECT_EQ(group_buf[pkt_offset::TYPE2], single_buf[pkt_offset::TYPE2]);
  EXPECT_EQ(group_buf[pkt_offset::HOP], single_buf[pkt_offset::HOP]);
  EXPECT_EQ(group_buf[pkt_offset::SYS], single_buf[pkt_offset::SYS]);

  // Addresses (bytes 7-15) should match
  EXPECT_EQ(memcmp(&group_buf[pkt_offset::SRC_ADDR], &single_buf[pkt_offset::SRC_ADDR], 9), 0);

  // Channel differs: group=0x00, single=channel
  EXPECT_EQ(group_buf[pkt_offset::CHANNEL], button::GROUP_CHANNEL);
  EXPECT_EQ(single_buf[pkt_offset::CHANNEL], 3);
}

// ============================================================================
// Constants Validation
// ============================================================================

TEST(GroupConstants, MaxDests) {
  // FIFO_LENGTH = 64, TX buffer = 1 + 26 + N, so N ≤ 37
  EXPECT_EQ(GROUP_MAX_DESTS, 37);
}

TEST(GroupConstants, GroupBaseLength) {
  EXPECT_EQ(button::GROUP_BASE_LENGTH, 26);
}

TEST(GroupConstants, GroupChannel) {
  EXPECT_EQ(button::GROUP_CHANNEL, 0x00);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
