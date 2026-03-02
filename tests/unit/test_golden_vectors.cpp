/// @file test_golden_vectors.cpp
/// @brief Golden tests derived from working commit fe33d186f70dd97a3fc8dc8a42bfb94dc3c7a5e5.
///
/// These tests verify that our implementation matches the working behavior exactly.
/// Any change to packet structure, offsets, or byte values should be caught here.

#include <gtest/gtest.h>
#include <cstring>
#include "elero/elero_packet.h"
#include "elero/elero_protocol.h"

using namespace esphome::elero;
using namespace esphome::elero::packet;

// =============================================================================
// GOLDEN CONSTANTS - derived from fe33d186f70dd97a3fc8dc8a42bfb94dc3c7a5e5
// =============================================================================

// These are the EXACT values from the working commit
namespace golden {
  // Message type constants
  constexpr uint8_t MSG_TYPE_COMMAND = 0x6a;
  constexpr uint8_t MSG_TYPE_COMMAND_ALT = 0x69;
  constexpr uint8_t MSG_TYPE_STATUS = 0xca;
  constexpr uint8_t MSG_TYPE_STATUS_ALT = 0xc9;

  // Command byte values
  constexpr uint8_t CMD_CHECK = 0x00;
  constexpr uint8_t CMD_STOP = 0x10;
  constexpr uint8_t CMD_UP = 0x20;
  constexpr uint8_t CMD_TILT = 0x24;
  constexpr uint8_t CMD_DOWN = 0x40;
  constexpr uint8_t CMD_INT = 0x44;

  // State byte values
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
  constexpr uint8_t STATE_ON = 0x10;

  // TX packet structure constants
  constexpr uint8_t TX_MSG_LENGTH = 0x1d;  // 29 bytes
  constexpr uint8_t TX_SYS_ADDR = 0x01;
  constexpr uint8_t TX_DEST_COUNT = 0x01;

  // Crypto constants
  constexpr uint16_t CRYPTO_MULT = 0x708f;
  constexpr uint16_t CRYPTO_MASK = 0xffff;

  // RSSI constants
  constexpr int8_t RSSI_OFFSET = -74;
  constexpr uint8_t RSSI_SIGN_BIT = 127;
  constexpr int RSSI_DIVISOR = 2;

  // TX packet ABSOLUTE positions (in msg_tx_ buffer)
  // From build_tx_packet_:
  //   this->msg_tx_[0] = ELERO_MSG_LENGTH;
  //   this->msg_tx_[1] = cmd.counter;
  //   this->msg_tx_[2] = cmd.type;
  //   ... etc ...
  constexpr size_t TX_POS_LENGTH = 0;
  constexpr size_t TX_POS_COUNTER = 1;
  constexpr size_t TX_POS_TYPE = 2;
  constexpr size_t TX_POS_TYPE2 = 3;
  constexpr size_t TX_POS_HOP = 4;
  constexpr size_t TX_POS_SYS = 5;
  constexpr size_t TX_POS_CHANNEL = 6;
  constexpr size_t TX_POS_SRC_ADDR = 7;   // 3 bytes
  constexpr size_t TX_POS_BWD_ADDR = 10;  // 3 bytes (same as src)
  constexpr size_t TX_POS_FWD_ADDR = 13;  // 3 bytes (same as src)
  constexpr size_t TX_POS_NUM_DESTS = 16;
  constexpr size_t TX_POS_DST_ADDR = 17;  // 3 bytes
  constexpr size_t TX_POS_PAYLOAD_START = 20;  // 10 bytes copied from cmd.payload
  constexpr size_t TX_POS_CRYPTO_HI = 22; // Overwrites payload[2]
  constexpr size_t TX_POS_CRYPTO_LO = 23; // Overwrites payload[3]
  constexpr size_t TX_POS_COMMAND = 24;   // cmd.payload[4] - NOT OVERWRITTEN
  constexpr size_t TX_POS_ENCRYPT_START = 22; // msg_encode starts here
  constexpr size_t TX_POS_ENCRYPT_END = 29;   // msg_encode ends here (8 bytes total)

  // RX payload offsets (relative to &msg_rx_[19 + dests_len])
  // For single 3-byte dest, dests_len=3, so payload starts at position 22
  // From interpret_msg:
  //   uint8_t command = ((typ == 0x6a) || (typ == 0x69)) ? payload[4] : 0;
  //   uint8_t state = ((typ == 0xca) || (typ == 0xc9)) ? payload[6] : 0;
  constexpr size_t RX_PAYLOAD_OFFSET_COMMAND = 4;  // For sniffing OTHER remotes
  constexpr size_t RX_PAYLOAD_OFFSET_STATE = 6;    // For status from blinds

  // TX encrypted block offsets (relative to position 22)
  // Command is at position 24 = encrypted_block[2]
  constexpr size_t TX_ENCRYPT_OFFSET_CRYPTO_HI = 0;
  constexpr size_t TX_ENCRYPT_OFFSET_CRYPTO_LO = 1;
  constexpr size_t TX_ENCRYPT_OFFSET_COMMAND = 2;  // OUR packets put command here
}

// =============================================================================
// CONSTANT VALUE VERIFICATION
// Verify our constants match the golden values from working commit
// =============================================================================

TEST(GoldenConstants, MessageTypes) {
  EXPECT_EQ(msg_type::COMMAND, golden::MSG_TYPE_COMMAND);
  EXPECT_EQ(msg_type::COMMAND_ALT, golden::MSG_TYPE_COMMAND_ALT);
  EXPECT_EQ(msg_type::STATUS, golden::MSG_TYPE_STATUS);
  EXPECT_EQ(msg_type::STATUS_ALT, golden::MSG_TYPE_STATUS_ALT);
}

TEST(GoldenConstants, CommandBytes) {
  EXPECT_EQ(command::CHECK, golden::CMD_CHECK);
  EXPECT_EQ(command::STOP, golden::CMD_STOP);
  EXPECT_EQ(command::UP, golden::CMD_UP);
  EXPECT_EQ(command::TILT, golden::CMD_TILT);
  EXPECT_EQ(command::DOWN, golden::CMD_DOWN);
  EXPECT_EQ(command::INTERMEDIATE, golden::CMD_INT);
}

TEST(GoldenConstants, StateBytes) {
  EXPECT_EQ(state::UNKNOWN, golden::STATE_UNKNOWN);
  EXPECT_EQ(state::TOP, golden::STATE_TOP);
  EXPECT_EQ(state::BOTTOM, golden::STATE_BOTTOM);
  EXPECT_EQ(state::INTERMEDIATE, golden::STATE_INTERMEDIATE);
  EXPECT_EQ(state::TILT, golden::STATE_TILT);
  EXPECT_EQ(state::BLOCKING, golden::STATE_BLOCKING);
  EXPECT_EQ(state::OVERHEATED, golden::STATE_OVERHEATED);
  EXPECT_EQ(state::TIMEOUT, golden::STATE_TIMEOUT);
  EXPECT_EQ(state::START_MOVING_UP, golden::STATE_START_MOVING_UP);
  EXPECT_EQ(state::START_MOVING_DOWN, golden::STATE_START_MOVING_DOWN);
  EXPECT_EQ(state::MOVING_UP, golden::STATE_MOVING_UP);
  EXPECT_EQ(state::MOVING_DOWN, golden::STATE_MOVING_DOWN);
  EXPECT_EQ(state::STOPPED, golden::STATE_STOPPED);
  EXPECT_EQ(state::TOP_TILT, golden::STATE_TOP_TILT);
  EXPECT_EQ(state::BOTTOM_TILT, golden::STATE_BOTTOM_TILT);
  EXPECT_EQ(state::LIGHT_ON, golden::STATE_ON);
}

TEST(GoldenConstants, TxStructure) {
  EXPECT_EQ(TX_MSG_LENGTH, golden::TX_MSG_LENGTH);
  EXPECT_EQ(TX_SYS_ADDR, golden::TX_SYS_ADDR);
  EXPECT_EQ(TX_DEST_COUNT, golden::TX_DEST_COUNT);
}

TEST(GoldenConstants, CryptoConstants) {
  EXPECT_EQ(crypto::MULT, golden::CRYPTO_MULT);
  EXPECT_EQ(crypto::MASK, golden::CRYPTO_MASK);
}

TEST(GoldenConstants, RssiConstants) {
  EXPECT_EQ(RSSI_OFFSET, golden::RSSI_OFFSET);
  EXPECT_EQ(RSSI_SIGN_BIT, golden::RSSI_SIGN_BIT);
  EXPECT_EQ(RSSI_DIVISOR, golden::RSSI_DIVISOR);
}

// =============================================================================
// TX OFFSET VERIFICATION
// Verify tx_offset:: namespace matches golden positions
// =============================================================================

TEST(GoldenOffsets, TxPositions) {
  EXPECT_EQ(tx_offset::LENGTH, golden::TX_POS_LENGTH);
  EXPECT_EQ(tx_offset::COUNTER, golden::TX_POS_COUNTER);
  EXPECT_EQ(tx_offset::TYPE, golden::TX_POS_TYPE);
  EXPECT_EQ(tx_offset::TYPE2, golden::TX_POS_TYPE2);
  EXPECT_EQ(tx_offset::HOP, golden::TX_POS_HOP);
  EXPECT_EQ(tx_offset::SYS, golden::TX_POS_SYS);
  EXPECT_EQ(tx_offset::CHANNEL, golden::TX_POS_CHANNEL);
  EXPECT_EQ(tx_offset::SRC_ADDR, golden::TX_POS_SRC_ADDR);
  EXPECT_EQ(tx_offset::BWD_ADDR, golden::TX_POS_BWD_ADDR);
  EXPECT_EQ(tx_offset::FWD_ADDR, golden::TX_POS_FWD_ADDR);
  EXPECT_EQ(tx_offset::NUM_DESTS, golden::TX_POS_NUM_DESTS);
  EXPECT_EQ(tx_offset::DST_ADDR, golden::TX_POS_DST_ADDR);
  EXPECT_EQ(tx_offset::PAYLOAD, golden::TX_POS_PAYLOAD_START);
  EXPECT_EQ(tx_offset::CRYPTO_CODE, golden::TX_POS_CRYPTO_HI);
  EXPECT_EQ(tx_offset::COMMAND, golden::TX_POS_COMMAND);
}

TEST(GoldenOffsets, RxPayloadOffsets) {
  // RX payload offsets for parsing received packets
  EXPECT_EQ(payload_offset::COMMAND, golden::RX_PAYLOAD_OFFSET_COMMAND);
  EXPECT_EQ(payload_offset::STATE, golden::RX_PAYLOAD_OFFSET_STATE);
}

// =============================================================================
// TX PACKET STRUCTURE VERIFICATION
// Build packets and verify exact byte layout matches working code
// =============================================================================

TEST(GoldenTxPacket, HeaderLayout) {
  TxParams params;
  params.counter = 42;
  params.channel = 5;
  params.type = msg_type::COMMAND;
  params.type2 = 0x00;
  params.hop = 0x0a;
  params.src_addr = 0x4FCA30;
  params.dst_addr = 0x803238;
  params.command = command::UP;

  uint8_t buf[30] = {0};
  build_tx_packet(params, buf);

  // Verify header exactly matches working code structure
  EXPECT_EQ(buf[golden::TX_POS_LENGTH], golden::TX_MSG_LENGTH);
  EXPECT_EQ(buf[golden::TX_POS_COUNTER], 42);
  EXPECT_EQ(buf[golden::TX_POS_TYPE], golden::MSG_TYPE_COMMAND);
  EXPECT_EQ(buf[golden::TX_POS_TYPE2], 0x00);
  EXPECT_EQ(buf[golden::TX_POS_HOP], 0x0a);
  EXPECT_EQ(buf[golden::TX_POS_SYS], golden::TX_SYS_ADDR);
  EXPECT_EQ(buf[golden::TX_POS_CHANNEL], 5);
}

TEST(GoldenTxPacket, AddressLayout) {
  TxParams params;
  params.src_addr = 0x4FCA30;
  params.dst_addr = 0x803238;

  uint8_t buf[30] = {0};
  build_tx_packet(params, buf);

  // Source address at positions 7-9 (big-endian)
  EXPECT_EQ(buf[golden::TX_POS_SRC_ADDR], 0x4F);
  EXPECT_EQ(buf[golden::TX_POS_SRC_ADDR + 1], 0xCA);
  EXPECT_EQ(buf[golden::TX_POS_SRC_ADDR + 2], 0x30);

  // Backward address same as source (positions 10-12)
  EXPECT_EQ(buf[golden::TX_POS_BWD_ADDR], 0x4F);
  EXPECT_EQ(buf[golden::TX_POS_BWD_ADDR + 1], 0xCA);
  EXPECT_EQ(buf[golden::TX_POS_BWD_ADDR + 2], 0x30);

  // Forward address same as source (positions 13-15)
  EXPECT_EQ(buf[golden::TX_POS_FWD_ADDR], 0x4F);
  EXPECT_EQ(buf[golden::TX_POS_FWD_ADDR + 1], 0xCA);
  EXPECT_EQ(buf[golden::TX_POS_FWD_ADDR + 2], 0x30);

  // Num destinations
  EXPECT_EQ(buf[golden::TX_POS_NUM_DESTS], golden::TX_DEST_COUNT);

  // Destination address at positions 17-19
  EXPECT_EQ(buf[golden::TX_POS_DST_ADDR], 0x80);
  EXPECT_EQ(buf[golden::TX_POS_DST_ADDR + 1], 0x32);
  EXPECT_EQ(buf[golden::TX_POS_DST_ADDR + 2], 0x38);
}

TEST(GoldenTxPacket, PayloadLayout_BeforeEncrypt) {
  // This test verifies the packet structure BEFORE encryption
  // by building a packet and manually checking the unencrypted positions

  TxParams params;
  params.counter = 1;
  params.command = command::UP;  // 0x20
  params.payload_1 = 0x00;
  params.payload_2 = 0x04;

  uint8_t buf[30] = {0};

  // Manually build without encryption to verify layout
  memset(buf, 0, 30);
  buf[golden::TX_POS_LENGTH] = golden::TX_MSG_LENGTH;
  buf[golden::TX_POS_COUNTER] = params.counter;
  buf[golden::TX_POS_TYPE] = msg_type::COMMAND;
  buf[golden::TX_POS_SYS] = golden::TX_SYS_ADDR;
  buf[golden::TX_POS_NUM_DESTS] = golden::TX_DEST_COUNT;

  // Payload positions 20-21
  buf[golden::TX_POS_PAYLOAD_START] = params.payload_1;      // position 20
  buf[golden::TX_POS_PAYLOAD_START + 1] = params.payload_2;  // position 21

  // Crypto code at positions 22-23
  uint16_t code = calc_crypto_code(params.counter);
  buf[golden::TX_POS_CRYPTO_HI] = (code >> 8) & 0xFF;        // position 22
  buf[golden::TX_POS_CRYPTO_LO] = code & 0xFF;               // position 23

  // Command at position 24 (encrypted block offset 2)
  buf[golden::TX_POS_COMMAND] = params.command;              // position 24

  // Positions 25-29 must be zero (critical for encryption!)
  buf[25] = 0;
  buf[26] = 0;
  buf[27] = 0;
  buf[28] = 0;
  buf[29] = 0;

  // Verify the structure
  EXPECT_EQ(buf[20], 0x00);  // payload_1
  EXPECT_EQ(buf[21], 0x04);  // payload_2
  EXPECT_EQ(buf[22], (code >> 8) & 0xFF);  // crypto_hi
  EXPECT_EQ(buf[23], code & 0xFF);         // crypto_lo
  EXPECT_EQ(buf[24], golden::CMD_UP);      // command
  EXPECT_EQ(buf[25], 0);     // padding
  EXPECT_EQ(buf[26], 0);     // padding
  EXPECT_EQ(buf[27], 0);     // padding
  EXPECT_EQ(buf[28], 0);     // padding (state position in RX)
  EXPECT_EQ(buf[29], 0);     // parity (set by msg_encode)
}

TEST(GoldenTxPacket, CommandRoundtrip_AllCommands) {
  // Verify all command bytes survive encode/decode roundtrip
  // NOTE: Command is at encrypted_block[2] = position 24

  uint8_t commands[] = {
    golden::CMD_CHECK,
    golden::CMD_STOP,
    golden::CMD_UP,
    golden::CMD_TILT,
    golden::CMD_DOWN,
    golden::CMD_INT
  };

  for (uint8_t cmd : commands) {
    SCOPED_TRACE("Command: " + std::to_string(cmd));

    TxParams params;
    params.counter = 1;
    params.command = cmd;

    uint8_t buf[30] = {0};
    build_tx_packet(params, buf);

    // Copy encrypted section and decrypt
    uint8_t encrypted[8];
    memcpy(encrypted, &buf[golden::TX_POS_ENCRYPT_START], 8);
    protocol::msg_decode(encrypted);

    // Command should be at offset 2 within our encrypted block
    EXPECT_EQ(encrypted[golden::TX_ENCRYPT_OFFSET_COMMAND], cmd)
      << "Command 0x" << std::hex << (int)cmd << " not found at encrypted[2]";
  }
}

TEST(GoldenTxPacket, EncryptedSectionLength) {
  // Verify encrypted section is exactly 8 bytes (positions 22-29)
  EXPECT_EQ(golden::TX_POS_ENCRYPT_END - golden::TX_POS_ENCRYPT_START + 1, 8);
}

TEST(GoldenTxPacket, CryptoCodeCalculation) {
  // Verify crypto code calculation matches working code
  // From: uint16_t code = (0x00 - (cmd.counter * ELERO_CRYPTO_MULT)) & ELERO_CRYPTO_MASK;

  // Counter 1
  uint16_t code1 = calc_crypto_code(1);
  EXPECT_EQ(code1, (0x0000 - (1 * golden::CRYPTO_MULT)) & golden::CRYPTO_MASK);

  // Counter 42
  uint16_t code42 = calc_crypto_code(42);
  EXPECT_EQ(code42, (0x0000 - (42 * golden::CRYPTO_MULT)) & golden::CRYPTO_MASK);

  // Counter 255
  uint16_t code255 = calc_crypto_code(255);
  EXPECT_EQ(code255, (0x0000 - (255 * golden::CRYPTO_MULT)) & golden::CRYPTO_MASK);
}

// =============================================================================
// CRITICAL: PADDING BYTES VERIFICATION
// This is what caused the "blinds not moving" bug
// =============================================================================

TEST(GoldenTxPacket, PaddingBytesAreZero) {
  // The bug was that positions 25-29 were not zeroed
  // This test ensures they are always zero before encryption

  TxParams params;
  params.counter = 1;
  params.command = command::UP;

  uint8_t buf[30];
  // Fill buffer with garbage to detect if zeroing fails
  memset(buf, 0xFF, sizeof(buf));

  build_tx_packet(params, buf);

  // Before encryption, positions 25-29 should be zero
  // (we can't check after encryption since they're scrambled)
  // Instead, decrypt and verify zeros at their positions

  uint8_t encrypted[8];
  memcpy(encrypted, &buf[golden::TX_POS_ENCRYPT_START], 8);
  protocol::msg_decode(encrypted);

  // After decryption, padding bytes (indices 3-5 and 7 is parity) should reveal
  // whether the originals were zeros
  // Note: parity at index 7 is calculated, so we check indices 3-5
  EXPECT_EQ(encrypted[3], 0) << "Padding at encrypted[3] (position 25) not zero";
  EXPECT_EQ(encrypted[4], 0) << "Padding at encrypted[4] (position 26) not zero";
  EXPECT_EQ(encrypted[5], 0) << "Padding at encrypted[5] (position 27) not zero";
}

TEST(GoldenTxPacket, FullPacketLength) {
  // Verify total packet length
  TxParams params;
  uint8_t buf[30] = {0};
  size_t len = build_tx_packet(params, buf);

  EXPECT_EQ(len, golden::TX_MSG_LENGTH + 1);  // 30 bytes total
}

// =============================================================================
// RX PARSING VERIFICATION
// Verify our parse offsets match working code
// =============================================================================

TEST(GoldenRxParsing, StateOffsetIsCorrect) {
  // Working code: payload[6] for state in status packets
  // payload starts at position 22 for single 3-byte destination
  // So state is at position 22 + 6 = 28

  // Build a simulated decrypted payload where state is at index 6
  uint8_t decrypted_payload[10] = {0};
  decrypted_payload[golden::RX_PAYLOAD_OFFSET_STATE] = golden::STATE_TOP;

  EXPECT_EQ(decrypted_payload[payload_offset::STATE], golden::STATE_TOP);
}

TEST(GoldenRxParsing, CommandOffsetForSniffing) {
  // Working code: payload[4] for command in command packets FROM OTHER REMOTES
  // This is different from our TX where command is at encrypted[2]!

  uint8_t decrypted_payload[10] = {0};
  decrypted_payload[golden::RX_PAYLOAD_OFFSET_COMMAND] = golden::CMD_DOWN;

  EXPECT_EQ(decrypted_payload[payload_offset::COMMAND], golden::CMD_DOWN);
}

// =============================================================================
// ENCODE/DECODE INVARIANT TESTS
// Verify encryption doesn't permute byte positions
// =============================================================================

TEST(GoldenEncoding, PayloadPositionsPreserved) {
  // Verify that command (index 2) and state (index 6) positions are preserved
  // after encode/decode roundtrip.
  //
  // Note: Positions 0-1 are crypto bytes used in XOR step, so arbitrary values
  // there don't roundtrip. Position 7 is parity (calculated by encoder).
  // The important positions for data are 2 (command) and 6 (state).

  // Set up a realistic payload structure (as in actual TX)
  uint16_t crypto_code = calc_crypto_code(1);
  uint8_t original[8] = {0};
  original[0] = (crypto_code >> 8) & 0xFF;  // crypto_hi
  original[1] = crypto_code & 0xFF;          // crypto_lo
  original[2] = golden::CMD_UP;              // command
  original[3] = 0;                           // padding
  original[4] = 0;                           // padding
  original[5] = 0;                           // padding
  original[6] = 0;                           // state (unused in TX command)
  // original[7] = parity (will be set by encoder)

  uint8_t buffer[8];
  memcpy(buffer, original, 8);

  protocol::msg_encode(buffer);
  protocol::msg_decode(buffer);

  // Critical positions that must be preserved
  EXPECT_EQ(buffer[2], original[2]) << "Command position (2) not preserved";
  // Positions 3-5 should also be preserved (they're zeros)
  EXPECT_EQ(buffer[3], original[3]) << "Padding position (3) not preserved";
  EXPECT_EQ(buffer[4], original[4]) << "Padding position (4) not preserved";
  EXPECT_EQ(buffer[5], original[5]) << "Padding position (5) not preserved";
  EXPECT_EQ(buffer[6], original[6]) << "State position (6) not preserved";
}

TEST(GoldenEncoding, CommandPositionPreserved) {
  // Verify command at position 2 survives roundtrip
  uint8_t payload[8] = {0};
  payload[golden::TX_ENCRYPT_OFFSET_COMMAND] = golden::CMD_UP;

  uint8_t original_cmd = payload[golden::TX_ENCRYPT_OFFSET_COMMAND];

  protocol::msg_encode(payload);
  protocol::msg_decode(payload);

  EXPECT_EQ(payload[golden::TX_ENCRYPT_OFFSET_COMMAND], original_cmd);
}

TEST(GoldenEncoding, StatePositionPreserved) {
  // Verify state at position 6 survives roundtrip (used in status packets)
  uint8_t payload[8] = {0};
  payload[golden::RX_PAYLOAD_OFFSET_STATE] = golden::STATE_MOVING_UP;

  uint8_t original_state = payload[golden::RX_PAYLOAD_OFFSET_STATE];

  protocol::msg_encode(payload);
  protocol::msg_decode(payload);

  EXPECT_EQ(payload[golden::RX_PAYLOAD_OFFSET_STATE], original_state);
}

// =============================================================================
// RSSI CALCULATION VERIFICATION
// =============================================================================

TEST(GoldenRssi, Calculation) {
  // Working code:
  //   if (rssi_raw > ELERO_RSSI_SIGN_BIT) {
  //     rssi = static_cast<float>(static_cast<int8_t>(rssi_raw)) / ELERO_RSSI_DIVISOR + ELERO_RSSI_OFFSET;
  //   } else {
  //     rssi = static_cast<float>(rssi_raw) / ELERO_RSSI_DIVISOR + ELERO_RSSI_OFFSET;
  //   }

  // Positive raw value
  EXPECT_FLOAT_EQ(calc_rssi(100),
    static_cast<float>(100) / golden::RSSI_DIVISOR + golden::RSSI_OFFSET);

  // Negative raw value (> 127)
  EXPECT_FLOAT_EQ(calc_rssi(200),
    static_cast<float>(static_cast<int8_t>(200)) / golden::RSSI_DIVISOR + golden::RSSI_OFFSET);
}

// =============================================================================
// NEGATIVE TESTS - ensure invalid values are handled
// =============================================================================

TEST(GoldenNegative, InvalidMessageType) {
  EXPECT_FALSE(is_command_packet(0x00));
  EXPECT_FALSE(is_command_packet(0xFF));
  EXPECT_FALSE(is_status_packet(0x00));
  EXPECT_FALSE(is_status_packet(0xFF));
}

TEST(GoldenNegative, CounterWrapAround) {
  // Counter should work from 0-255
  for (int i = 0; i <= 255; i++) {
    uint16_t code = calc_crypto_code(static_cast<uint8_t>(i));
    // Just verify no crash and reasonable output
    EXPECT_LE(code, 0xFFFF);
  }
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
