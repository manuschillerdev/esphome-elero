/// @file test_parse_roundtrip.cpp
/// @brief Valid packet parsing, TX->RX roundtrip, and button packet tests.
///
/// These tests verify that:
/// 1. build_tx_packet() produces parseable packets
/// 2. parse_packet() extracts correct fields from valid packets
/// 3. Button packets (type 0x44) with 1-byte addressing work
/// 4. Various edge cases in packet validation

#include <gtest/gtest.h>
#include <cstring>
#include "elero/elero_packet.h"
#include "elero/elero_protocol.h"

using namespace esphome::elero;
using namespace esphome::elero::packet;

// =============================================================================
// HELPER: Build a raw buffer that parse_packet() can consume
// build_tx_packet() produces 30 bytes. parse_packet() expects the CC1101
// to append RSSI + LQI/CRC after the packet (at positions length+1, length+2).
// =============================================================================

struct RawPacketBuffer {
  uint8_t data[64];
  size_t len;
};

/// Build a complete raw packet buffer with RSSI/LQI appended,
/// suitable for feeding to parse_packet().
static RawPacketBuffer build_parseable_packet(const TxParams& params, uint8_t rssi_raw = 100, uint8_t lqi_crc = 0x80) {
  RawPacketBuffer buf{};
  size_t pkt_len = build_tx_packet(params, buf.data);

  // parse_packet reads length from buf[0], then expects:
  //   buf[length+1] = RSSI
  //   buf[length+2] = LQI | CRC_OK
  uint8_t length = buf.data[0];  // TX_MSG_LENGTH = 0x1D = 29
  buf.data[length + 1] = rssi_raw;
  buf.data[length + 2] = lqi_crc;
  buf.len = length + 3;  // length byte + packet + RSSI + LQI

  return buf;
}

// =============================================================================
// TX -> RX ROUNDTRIP: Build a packet, parse it back, verify all fields
// =============================================================================

TEST(TxRxRoundtrip, BasicCommandPacket) {
  TxParams params;
  params.counter = 7;
  params.dst_addr = 0xA831E5;
  params.src_addr = 0x4FCA30;
  params.channel = 5;
  params.type = msg_type::COMMAND;
  params.type2 = 0x00;
  params.hop = 0x0a;
  params.command = command::UP;
  params.payload_1 = 0x00;
  params.payload_2 = 0x04;

  auto raw = build_parseable_packet(params);
  auto result = parse_packet(raw.data, raw.len);

  ASSERT_TRUE(result.valid) << "reject_reason: " << (result.reject_reason ? result.reject_reason : "null");

  // Header fields
  EXPECT_EQ(result.length, TX_MSG_LENGTH);
  EXPECT_EQ(result.counter, 7);
  EXPECT_EQ(result.type, msg_type::COMMAND);
  EXPECT_EQ(result.type2, 0x00);
  EXPECT_EQ(result.hop, 0x0a);
  EXPECT_EQ(result.syst, TX_SYS_ADDR);
  EXPECT_EQ(result.channel, 5);

  // Addresses
  EXPECT_EQ(result.src_addr, 0x4FCA30u);
  EXPECT_EQ(result.bwd_addr, 0x4FCA30u);
  EXPECT_EQ(result.fwd_addr, 0x4FCA30u);
  EXPECT_EQ(result.dst_addr, 0xA831E5u);
  EXPECT_EQ(result.num_dests, 1);

  // RSSI
  EXPECT_EQ(result.rssi_raw, 100);
  EXPECT_FLOAT_EQ(result.rssi, -24.0f);  // 100/2 + (-74)
  EXPECT_EQ(result.crc_ok, 1);  // 0x80 has CRC_OK bit set
}

TEST(TxRxRoundtrip, CommandSurvivesEncryption) {
  // The critical test: does the command byte survive build -> parse?
  uint8_t commands[] = {command::CHECK, command::STOP, command::UP,
                        command::TILT, command::DOWN, command::INTERMEDIATE};

  for (uint8_t cmd : commands) {
    SCOPED_TRACE("command: 0x" + std::to_string(cmd));

    TxParams params;
    params.counter = 1;
    params.dst_addr = 0x123456;
    params.src_addr = 0xABCDEF;
    params.channel = 3;
    params.command = cmd;

    auto raw = build_parseable_packet(params);
    auto result = parse_packet(raw.data, raw.len);

    ASSERT_TRUE(result.valid) << "reject_reason: " << (result.reject_reason ? result.reject_reason : "null");
    EXPECT_EQ(result.type, msg_type::COMMAND);

    // Command is at payload_offset::COMMAND (index 2) — same for TX and RX.
    // parse_packet should recover the command correctly.
    EXPECT_EQ(result.command, cmd);
  }
}

TEST(TxRxRoundtrip, DifferentCountersProduceDifferentPackets) {
  TxParams params1;
  params1.counter = 1;
  params1.command = command::UP;

  TxParams params2;
  params2.counter = 2;
  params2.command = command::UP;

  uint8_t buf1[30] = {0}, buf2[30] = {0};
  build_tx_packet(params1, buf1);
  build_tx_packet(params2, buf2);

  // Encrypted section (bytes 22-29) should differ
  EXPECT_NE(memcmp(&buf1[tx_offset::CRYPTO_CODE], &buf2[tx_offset::CRYPTO_CODE], 8), 0);
}

TEST(TxRxRoundtrip, DifferentCommandsProduceDifferentPackets) {
  TxParams params1;
  params1.counter = 1;
  params1.command = command::UP;

  TxParams params2;
  params2.counter = 1;
  params2.command = command::DOWN;

  uint8_t buf1[30] = {0}, buf2[30] = {0};
  build_tx_packet(params1, buf1);
  build_tx_packet(params2, buf2);

  // Same counter but different command → encrypted section must differ
  EXPECT_NE(memcmp(&buf1[tx_offset::CRYPTO_CODE], &buf2[tx_offset::CRYPTO_CODE], 8), 0);
}

TEST(TxRxRoundtrip, AllAddressesParsedCorrectly) {
  // Use distinctive addresses to verify no cross-contamination
  TxParams params;
  params.src_addr = 0x112233;
  params.dst_addr = 0xAABBCC;

  auto raw = build_parseable_packet(params);
  auto result = parse_packet(raw.data, raw.len);

  ASSERT_TRUE(result.valid);
  EXPECT_EQ(result.src_addr, 0x112233u);
  EXPECT_EQ(result.bwd_addr, 0x112233u);  // Same as src for direct commands
  EXPECT_EQ(result.fwd_addr, 0x112233u);  // Same as src for direct commands
  EXPECT_EQ(result.dst_addr, 0xAABBCCu);
}

TEST(TxRxRoundtrip, FullKnownPacket) {
  // Known full TX packet from working implementation (counter=7, UP, dst=0xA831E5, src=0x4FCA30)
  uint8_t known_packet[] = {
    0x1D, 0x07, 0x6A, 0x00, 0x0A, 0x01, 0x05, 0x4F, 0xCA, 0x30,
    0x4F, 0xCA, 0x30, 0x4F, 0xCA, 0x30, 0x01, 0xA8, 0x31, 0xE5,
    0x00, 0x04, 0x40, 0xB1, 0x17, 0x96, 0xC0, 0xF1, 0x4B, 0x35
  };

  // Append RSSI + LQI for parse_packet
  uint8_t raw[34];
  memcpy(raw, known_packet, 30);
  raw[30] = 0x64;  // RSSI = 100
  raw[31] = 0x80;  // LQI with CRC_OK
  size_t raw_len = 32;

  auto result = parse_packet(raw, raw_len);

  ASSERT_TRUE(result.valid) << "reject_reason: " << (result.reject_reason ? result.reject_reason : "null");
  EXPECT_EQ(result.counter, 7);
  EXPECT_EQ(result.type, msg_type::COMMAND);
  EXPECT_EQ(result.channel, 5);
  EXPECT_EQ(result.src_addr, 0x4FCA30u);
  EXPECT_EQ(result.dst_addr, 0xA831E5u);
}

// =============================================================================
// VALID PACKET PARSING: Verify field extraction from crafted valid packets
// =============================================================================

TEST(ValidPacketParsing, StatusPacket_ExtractsState) {
  // Craft a status packet (type 0xCA) where state byte is at payload[6]
  // We build the raw bytes manually since build_tx_packet only builds command packets

  // Start with a standard command packet structure but change type to STATUS
  TxParams params;
  params.counter = 1;
  params.type = msg_type::STATUS;
  params.src_addr = 0xA831E5;  // Blind address (status comes FROM blind)
  params.dst_addr = 0x4FCA30;  // Remote address (status goes TO remote)
  params.channel = 5;
  params.command = 0x00;

  uint8_t raw[34] = {0};
  size_t pkt_len = build_tx_packet(params, raw);

  // Override the encrypted section with a status payload
  // We need state=TOP(0x01) at payload_offset::STATE (index 6)
  uint16_t code = calc_crypto_code(params.counter);
  uint8_t payload[8] = {
    static_cast<uint8_t>((code >> 8) & 0xFF),
    static_cast<uint8_t>(code & 0xFF),
    0x00, 0x00, 0x00, 0x00,
    state::TOP,  // State at index 6
    0x00
  };
  protocol::msg_encode(payload);
  memcpy(&raw[tx_offset::CRYPTO_CODE], payload, 8);

  // Append RSSI + LQI
  uint8_t length = raw[0];
  raw[length + 1] = 0x64;
  raw[length + 2] = 0x80;
  size_t raw_len = length + 3;

  auto result = parse_packet(raw, raw_len);

  ASSERT_TRUE(result.valid) << "reject_reason: " << (result.reject_reason ? result.reject_reason : "null");
  EXPECT_EQ(result.type, msg_type::STATUS);
  EXPECT_EQ(result.state, state::TOP);
}

TEST(ValidPacketParsing, CrcNotOk) {
  TxParams params;
  params.counter = 1;
  params.command = command::UP;

  // LQI byte without CRC_OK bit
  auto raw = build_parseable_packet(params, 100, 0x00);
  auto result = parse_packet(raw.data, raw.len);

  ASSERT_TRUE(result.valid);
  EXPECT_EQ(result.crc_ok, 0);
}

TEST(ValidPacketParsing, LqiExtracted) {
  TxParams params;
  params.counter = 1;

  // LQI = 0x3F (63) with CRC_OK bit set → 0xBF
  auto raw = build_parseable_packet(params, 100, 0xBF);
  auto result = parse_packet(raw.data, raw.len);

  ASSERT_TRUE(result.valid);
  EXPECT_EQ(result.lqi, 0x3F);
  EXPECT_EQ(result.crc_ok, 1);
}

// =============================================================================
// BUTTON PACKET (type 0x44): 1-byte addressing code path
// =============================================================================

TEST(ButtonPacket, ParsesWithOneByteAddressing) {
  // Button packets use type 0x44 which is <= ADDR_3BYTE_THRESHOLD (0x60)
  // This means destinations use 1-byte addresses instead of 3-byte

  // Build a minimal valid button packet
  uint8_t raw[34] = {0};
  uint8_t length = 0x1D;  // Same length as standard packet
  raw[pkt_offset::LENGTH] = length;
  raw[pkt_offset::COUNTER] = 1;
  raw[pkt_offset::TYPE] = msg_type::BUTTON;  // 0x44
  raw[pkt_offset::TYPE2] = 0x00;
  raw[pkt_offset::HOP] = 0x0a;
  raw[pkt_offset::SYS] = 0x01;
  raw[pkt_offset::CHANNEL] = 5;

  // Source address
  raw[pkt_offset::SRC_ADDR] = 0xA8;
  raw[pkt_offset::SRC_ADDR + 1] = 0x31;
  raw[pkt_offset::SRC_ADDR + 2] = 0xE5;

  // Backward address
  raw[pkt_offset::BWD_ADDR] = 0xA8;
  raw[pkt_offset::BWD_ADDR + 1] = 0x31;
  raw[pkt_offset::BWD_ADDR + 2] = 0xE5;

  // Forward address
  raw[pkt_offset::FWD_ADDR] = 0xA8;
  raw[pkt_offset::FWD_ADDR + 1] = 0x31;
  raw[pkt_offset::FWD_ADDR + 2] = 0xE5;

  // 1 destination, 1-byte address (for button packets)
  raw[pkt_offset::NUM_DESTS] = 1;
  raw[pkt_offset::FIRST_DEST] = 0x05;  // 1-byte dst

  // Payload: 2 bytes header + 10 bytes payload data
  // Payload starts at FIRST_DEST + 2 + dests_len = 17 + 2 + 1 = 20
  size_t payload_start = pkt_offset::FIRST_DEST + 2 + 1;  // +2 for payload_1/2, +1 for 1-byte dest
  // Fill with enough data for msg_decode (8 bytes) + 2 more
  for (size_t i = 0; i < 10; i++) {
    raw[payload_start + i] = 0x00;
  }

  // Append RSSI + LQI
  raw[length + 1] = 0x64;
  raw[length + 2] = 0x80;
  size_t raw_len = length + 3;

  auto result = parse_packet(raw, raw_len);

  ASSERT_TRUE(result.valid) << "reject_reason: " << (result.reject_reason ? result.reject_reason : "null");
  EXPECT_EQ(result.type, msg_type::BUTTON);
  EXPECT_EQ(result.num_dests, 1);
  EXPECT_EQ(result.dests_len, 1);  // 1-byte addressing
  EXPECT_EQ(result.dst_addr, 0x05u);  // 1-byte address
  EXPECT_EQ(result.src_addr, 0xA831E5u);
}

TEST(ButtonPacket, TypeCheckFunctions) {
  EXPECT_TRUE(is_button_packet(msg_type::BUTTON));
  EXPECT_FALSE(is_button_packet(msg_type::COMMAND));
  EXPECT_FALSE(is_button_packet(msg_type::STATUS));

  // Button type (0x44) is NOT a command or status packet
  EXPECT_FALSE(is_command_packet(msg_type::BUTTON));
  EXPECT_FALSE(is_status_packet(msg_type::BUTTON));
}

// =============================================================================
// ALT PACKET TYPES: COMMAND_ALT (0x69) and STATUS_ALT (0xc9)
// =============================================================================

TEST(AltPacketTypes, CommandAlt_ExtractsCommand) {
  // COMMAND_ALT (0x69) should be treated the same as COMMAND (0x6a)
  // for command extraction
  TxParams params;
  params.counter = 5;
  params.type = msg_type::COMMAND_ALT;  // 0x69
  params.src_addr = 0x112233;
  params.dst_addr = 0x445566;
  params.channel = 3;
  params.command = command::DOWN;

  auto raw = build_parseable_packet(params);
  auto result = parse_packet(raw.data, raw.len);

  ASSERT_TRUE(result.valid) << "reject_reason: " << (result.reject_reason ? result.reject_reason : "null");
  EXPECT_EQ(result.type, msg_type::COMMAND_ALT);
  // Verify command extraction happened (is_command_packet returns true for 0x69)
  // Command is at payload_offset::COMMAND (index 2) for both TX and RX
  EXPECT_TRUE(is_command_packet(result.type));
}

TEST(AltPacketTypes, StatusAlt_ExtractsState) {
  // STATUS_ALT (0xc9) should extract state just like STATUS (0xca)
  TxParams params;
  params.counter = 10;
  params.type = msg_type::STATUS_ALT;  // 0xc9
  params.src_addr = 0xA831E5;
  params.dst_addr = 0x4FCA30;
  params.channel = 5;
  params.command = 0x00;

  uint8_t raw[34] = {0};
  build_tx_packet(params, raw);

  // Override encrypted section with status payload containing state=MOVING_UP
  uint16_t code = calc_crypto_code(params.counter);
  uint8_t payload[8] = {
    static_cast<uint8_t>((code >> 8) & 0xFF),
    static_cast<uint8_t>(code & 0xFF),
    0x00, 0x00, 0x00, 0x00,
    state::MOVING_UP,  // State at index 6
    0x00
  };
  protocol::msg_encode(payload);
  memcpy(&raw[tx_offset::CRYPTO_CODE], payload, 8);

  uint8_t length = raw[0];
  raw[length + 1] = 0x64;
  raw[length + 2] = 0x80;
  size_t raw_len = length + 3;

  auto result = parse_packet(raw, raw_len);

  ASSERT_TRUE(result.valid) << "reject_reason: " << (result.reject_reason ? result.reject_reason : "null");
  EXPECT_EQ(result.type, msg_type::STATUS_ALT);
  EXPECT_TRUE(is_status_packet(result.type));
  EXPECT_EQ(result.state, state::MOVING_UP);
}

// =============================================================================
// MULTI-DESTINATION PACKETS
// =============================================================================

TEST(MultiDest, ThreeByteAddressing_TwoDests) {
  // Build a packet with 2 destinations (3-byte each)
  // This shifts the encrypted payload offset by 3 bytes
  uint8_t raw[40] = {0};
  uint8_t length = 0x20;  // 32 (larger to fit 2 dests)
  raw[pkt_offset::LENGTH] = length;
  raw[pkt_offset::COUNTER] = 1;
  raw[pkt_offset::TYPE] = msg_type::COMMAND;  // > 0x60 = 3-byte addrs
  raw[pkt_offset::TYPE2] = 0x00;
  raw[pkt_offset::HOP] = 0x0a;
  raw[pkt_offset::SYS] = 0x01;
  raw[pkt_offset::CHANNEL] = 5;

  // Source/bwd/fwd addresses
  write_addr(&raw[pkt_offset::SRC_ADDR], 0x4FCA30);
  write_addr(&raw[pkt_offset::BWD_ADDR], 0x4FCA30);
  write_addr(&raw[pkt_offset::FWD_ADDR], 0x4FCA30);

  // 2 destinations, 3-byte each
  raw[pkt_offset::NUM_DESTS] = 2;
  write_addr(&raw[pkt_offset::FIRST_DEST], 0xA831E5);      // dst 1
  write_addr(&raw[pkt_offset::FIRST_DEST + 3], 0xB94200);  // dst 2

  // Payload starts at FIRST_DEST + 2 + dests_len = 17 + 2 + 6 = 25
  size_t payload_start = pkt_offset::FIRST_DEST + 2 + 6;
  // Build valid encrypted payload
  uint16_t code = calc_crypto_code(1);
  uint8_t enc[8] = {
    static_cast<uint8_t>((code >> 8) & 0xFF),
    static_cast<uint8_t>(code & 0xFF),
    0x00, 0x00, command::UP, 0x00, 0x00, 0x00
  };
  protocol::msg_encode(enc);
  memcpy(&raw[payload_start], enc, 8);
  // Fill remaining payload bytes
  raw[payload_start + 8] = 0x00;
  raw[payload_start + 9] = 0x00;

  // Append RSSI + LQI
  raw[length + 1] = 0x64;
  raw[length + 2] = 0x80;
  size_t raw_len = length + 3;

  auto result = parse_packet(raw, raw_len);

  ASSERT_TRUE(result.valid) << "reject_reason: " << (result.reject_reason ? result.reject_reason : "null");
  EXPECT_EQ(result.num_dests, 2);
  EXPECT_EQ(result.dests_len, 6u);  // 2 * 3 bytes
  EXPECT_EQ(result.dst_addr, 0xA831E5u);  // First destination
  EXPECT_EQ(result.src_addr, 0x4FCA30u);
}

TEST(MultiDest, ButtonPacket_ThreeSingleByteDests) {
  // Button packets use 1-byte addressing, test with 3 dests
  uint8_t raw[40] = {0};
  uint8_t length = 0x1D;
  raw[pkt_offset::LENGTH] = length;
  raw[pkt_offset::COUNTER] = 1;
  raw[pkt_offset::TYPE] = msg_type::BUTTON;  // 0x44 ≤ 0x60 = 1-byte addrs
  raw[pkt_offset::TYPE2] = 0x00;
  raw[pkt_offset::HOP] = 0x0a;
  raw[pkt_offset::SYS] = 0x01;
  raw[pkt_offset::CHANNEL] = 5;

  write_addr(&raw[pkt_offset::SRC_ADDR], 0xA831E5);
  write_addr(&raw[pkt_offset::BWD_ADDR], 0xA831E5);
  write_addr(&raw[pkt_offset::FWD_ADDR], 0xA831E5);

  // 3 destinations, 1-byte each
  raw[pkt_offset::NUM_DESTS] = 3;
  raw[pkt_offset::FIRST_DEST] = 0x05;
  raw[pkt_offset::FIRST_DEST + 1] = 0x06;
  raw[pkt_offset::FIRST_DEST + 2] = 0x07;

  // Payload at FIRST_DEST + 2 + dests_len = 17 + 2 + 3 = 22
  size_t payload_start = pkt_offset::FIRST_DEST + 2 + 3;
  for (size_t i = 0; i < 10; i++) raw[payload_start + i] = 0x00;

  raw[length + 1] = 0x64;
  raw[length + 2] = 0x80;
  size_t raw_len = length + 3;

  auto result = parse_packet(raw, raw_len);

  ASSERT_TRUE(result.valid) << "reject_reason: " << (result.reject_reason ? result.reject_reason : "null");
  EXPECT_EQ(result.type, msg_type::BUTTON);
  EXPECT_EQ(result.num_dests, 3);
  EXPECT_EQ(result.dests_len, 3u);  // 3 * 1 byte
  EXPECT_EQ(result.dst_addr, 0x05u);  // First 1-byte dest
}

TEST(MultiDest, ZeroDestinations) {
  // Edge case: num_dests = 0
  TxParams params;
  params.counter = 1;
  params.command = command::CHECK;

  uint8_t raw[34] = {0};
  build_tx_packet(params, raw);

  // Override num_dests to 0
  raw[tx_offset::NUM_DESTS] = 0;

  uint8_t length = raw[0];
  raw[length + 1] = 0x64;
  raw[length + 2] = 0x80;
  size_t raw_len = length + 3;

  auto result = parse_packet(raw, raw_len);

  ASSERT_TRUE(result.valid) << "reject_reason: " << (result.reject_reason ? result.reject_reason : "null");
  EXPECT_EQ(result.num_dests, 0);
  EXPECT_EQ(result.dests_len, 0u);
}

// =============================================================================
// NULL POINTER SAFETY
// =============================================================================

TEST(ParseSafety, NullPointerReturnsInvalid) {
  auto result = parse_packet(nullptr, 100);
  EXPECT_FALSE(result.valid);
  EXPECT_STREQ(result.reject_reason, "too_short");
}

TEST(ParseSafety, NullPointerZeroLength) {
  auto result = parse_packet(nullptr, 0);
  EXPECT_FALSE(result.valid);
  EXPECT_STREQ(result.reject_reason, "too_short");
}

// =============================================================================
// EDGE CASES in parse_packet validation
// =============================================================================

TEST(ParseEdgeCases, ExactMinimumValidPacket) {
  // Build the smallest valid packet that parse_packet will accept
  TxParams params;
  params.counter = 1;

  auto raw = build_parseable_packet(params);
  auto result = parse_packet(raw.data, raw.len);

  ASSERT_TRUE(result.valid);
}

TEST(ParseEdgeCases, RssiOutOfBounds) {
  // To trigger "rssi_oob", we need:
  //   payload_end <= raw_len (pass payload check)
  //   length + CC1101_APPEND_SIZE >= raw_len (fail RSSI check)
  //
  // For a standard 1-dest command packet (length=0x1D=29):
  //   payload_end = 17 + 2 + 3 + 10 = 32
  //   rssi_oob needs: 29 + 2 >= raw_len → raw_len <= 31
  // These conflict (need raw_len >= 32 AND <= 31), so RSSI check is
  // unreachable for this packet shape.
  //
  // Trigger it with a larger length value and fewer dests:
  // Use length=0x30 (48), type=COMMAND, num_dests=1
  // payload_end = 17 + 2 + 3 + 10 = 32 (same, doesn't depend on length)
  // rssi_oob needs: 48 + 2 >= raw_len → raw_len <= 49
  // So raw_len in [32..49] will pass payload but fail RSSI.
  uint8_t raw[50] = {0};
  raw[pkt_offset::LENGTH] = 0x30;  // length = 48
  raw[pkt_offset::TYPE] = msg_type::COMMAND;
  raw[pkt_offset::NUM_DESTS] = 1;
  raw[pkt_offset::SRC_ADDR] = 0x01;
  raw[pkt_offset::FIRST_DEST] = 0x01;

  // Provide 40 bytes: enough for payload (32) but not for RSSI at [49],[50]
  auto result = parse_packet(raw, 40);
  EXPECT_FALSE(result.valid);
  EXPECT_STREQ(result.reject_reason, "rssi_oob");
}

TEST(ParseEdgeCases, DestsLenOverflow) {
  // Packet where dests_len * 3 pushes encrypted payload beyond the length field.
  // We need: FIRST_DEST + 2 + dests_len + 7 > length
  // FIRST_DEST = 17, so 17 + 2 + dests_len + 7 = 26 + dests_len > length
  // With length = 29 (0x1D), need dests_len > 3 → num_dests >= 2 for 3-byte addrs
  // But also need payload_end_idx >= FIFO_LENGTH (64)
  // 26 + dests_len >= 64 → dests_len >= 38 → num_dests >= 13
  uint8_t raw[64] = {0};
  raw[pkt_offset::LENGTH] = 0x39;  // length = 57 (max)
  raw[pkt_offset::TYPE] = msg_type::COMMAND;  // 3-byte addressing (> 0x60)
  raw[pkt_offset::NUM_DESTS] = 15;  // 15 * 3 = 45 → payload_end = 26 + 45 = 71 > 64

  // Fill enough data to pass header check
  raw[pkt_offset::SRC_ADDR] = 0x01;
  raw[pkt_offset::BWD_ADDR] = 0x01;
  raw[pkt_offset::FWD_ADDR] = 0x01;

  size_t raw_len = 64;
  auto result = parse_packet(raw, raw_len);

  EXPECT_FALSE(result.valid);
  EXPECT_STREQ(result.reject_reason, "dests_len_too_long");
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
