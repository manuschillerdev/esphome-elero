/// @file test_cc1101_compat.cpp
/// @brief Tests for CC1101-compatible CRC-16 and PN9 whitening functions.
///
/// These pure functions are used by the SX1262 driver to produce packets
/// that CC1101 receivers can decode. Tests verify:
/// - Known PN9 sequence against published values
/// - Whitening round-trip (whiten then de-whiten = original)
/// - CRC-16 against known vectors
/// - Full TX encoding pipeline (CRC + whitening) matches CC1101 hardware output

#include <gtest/gtest.h>
#include "elero/cc1101_compat.h"
#include <cstring>
#include <vector>

using namespace esphome::elero;

// ═══════════════════════════════════════════════════════════════════════════════
// PN9 WHITENING
// ═══════════════════════════════════════════════════════════════════════════════

TEST(PN9Whitening, KnownSequence) {
  // The first 8 bytes of the CC1101 PN9 sequence (seed 0x1FF).
  // Input: all zeros → output = raw PN9 keystream.
  uint8_t zeros[8] = {};
  cc1101_pn9_whiten(zeros, 8);

  const uint8_t expected[] = {0xFF, 0xE1, 0x1D, 0x9A, 0xED, 0x85, 0x33, 0x24};
  EXPECT_EQ(memcmp(zeros, expected, 8), 0)
      << "PN9 keystream mismatch. Got: "
      << std::hex << (int)zeros[0] << " " << (int)zeros[1] << " "
      << (int)zeros[2] << " " << (int)zeros[3] << " "
      << (int)zeros[4] << " " << (int)zeros[5] << " "
      << (int)zeros[6] << " " << (int)zeros[7];
}

TEST(PN9Whitening, RoundTrip) {
  // XOR is self-inverse: whiten(whiten(data)) == data
  const uint8_t original[] = {0x1D, 0x08, 0x6A, 0x00, 0x0A, 0x01, 0x03, 0x4F,
                               0xCA, 0x30, 0x4F, 0xCA, 0x30, 0x4F, 0xCA, 0x30};
  uint8_t buf[16];
  memcpy(buf, original, 16);

  cc1101_pn9_whiten(buf, 16);  // whiten
  // Verify it changed
  EXPECT_NE(memcmp(buf, original, 16), 0);

  cc1101_pn9_whiten(buf, 16);  // de-whiten
  EXPECT_EQ(memcmp(buf, original, 16), 0);
}

TEST(PN9Whitening, EmptyInput) {
  // Zero-length input should not crash
  uint8_t buf[1] = {0x42};
  cc1101_pn9_whiten(buf, 0);
  EXPECT_EQ(buf[0], 0x42);  // unchanged
}

TEST(PN9Whitening, SingleByte) {
  // First PN9 byte is 0xFF, so any byte XOR 0xFF = complement
  uint8_t buf[1] = {0x1D};
  cc1101_pn9_whiten(buf, 1);
  EXPECT_EQ(buf[0], 0x1D ^ 0xFF);  // 0xE2
}

TEST(PN9Whitening, WhitenedEleroPacket) {
  // Real Elero packet from TX log, verified by Python + CC1101 hardware
  uint8_t raw[] = {0x1B, 0x01, 0x44, 0x10, 0x00, 0x01, 0x03, 0x4F,
                   0xCA, 0x30, 0x4F, 0xCA, 0x30, 0x4F, 0xCA, 0x30,
                   0x01, 0x03, 0x00, 0x03, 0x54, 0xF4, 0x1E, 0xBC,
                   0x6C, 0xDE, 0xA4, 0xB2, 0xBE, 0x64};

  const uint8_t expected_whitened[] = {
      0xE4, 0xE0, 0x59, 0x8A, 0xED, 0x84, 0x30, 0x6B,
      0x20, 0x4A, 0x9D, 0xF3, 0x40, 0xD8, 0x9D, 0x3A,
      0x55, 0x7E, 0x2D, 0xDB, 0x39, 0xF9, 0xA4, 0x33,
      0x0B, 0x87, 0x63, 0x10, 0x01, 0x50};

  cc1101_pn9_whiten(raw, 30);
  EXPECT_EQ(memcmp(raw, expected_whitened, 30), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// CRC-16
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CRC16, InitValue) {
  // CRC of empty data should be the init value
  EXPECT_EQ(cc1101_crc16(nullptr, 0), 0xFFFF);
}

TEST(CRC16, SingleByte) {
  uint8_t data[] = {0x00};
  uint16_t crc = cc1101_crc16(data, 1);
  // CRC-16/IBM of 0x00 with init 0xFFFF
  EXPECT_NE(crc, 0xFFFF);  // should change from init
  EXPECT_NE(crc, 0x0000);  // should not be zero
}

TEST(CRC16, KnownEleroPacket) {
  // Real Elero packet: length byte + 27 data bytes (28 bytes total).
  // CRC verified against CC1101 hardware (crc_ok:true on reception).
  const uint8_t packet[] = {
      0x1B, 0x01, 0x44, 0x10, 0x00, 0x01, 0x03, 0x4F,
      0xCA, 0x30, 0x4F, 0xCA, 0x30, 0x4F, 0xCA, 0x30,
      0x01, 0x03, 0x00, 0x03, 0x54, 0xF4, 0x1E, 0xBC,
      0x6C, 0xDE, 0xA4, 0xB2};

  uint16_t crc = cc1101_crc16(packet, 28);
  EXPECT_EQ(crc, 0xBE64);
}

TEST(CRC16, KnownCommandPacket) {
  // 0x6A command packet (30 bytes: 1 length + 29 data)
  // From firmware TX log, CRC verified by CC1101 with crc_ok:true
  const uint8_t packet[] = {
      0x1D, 0x08, 0x6A, 0x00, 0x0A, 0x01, 0x03, 0x4F,
      0xCA, 0x30, 0x4F, 0xCA, 0x30, 0x4F, 0xCA, 0x30,
      0x01, 0x41, 0x32, 0x38, 0x00, 0x04, 0x7C, 0xEF,
      0x32, 0x28, 0x84, 0x43, 0xFC, 0xC9};

  uint16_t crc = cc1101_crc16(packet, 30);
  EXPECT_EQ(crc, 0x3AE6);
}

TEST(CRC16, DifferentData) {
  // Same packet but one byte changed → different CRC
  uint8_t packet_a[] = {0x1D, 0x01, 0x6A};
  uint8_t packet_b[] = {0x1D, 0x02, 0x6A};  // counter changed

  EXPECT_NE(cc1101_crc16(packet_a, 3), cc1101_crc16(packet_b, 3));
}

// ═══════════════════════════════════════════════════════════════════════════════
// FULL TX PIPELINE: CRC + WHITENING
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TxPipeline, CrcThenWhitenRoundTrip) {
  // Simulate full TX: raw → CRC → whiten.
  // Simulate full RX: de-whiten → verify CRC.
  // This is exactly what happens between SX1262 TX and CC1101 RX.
  uint8_t raw[] = {0x1D, 0x08, 0x6A, 0x00, 0x0A, 0x01, 0x03, 0x4F,
                   0xCA, 0x30, 0x4F, 0xCA, 0x30, 0x4F, 0xCA, 0x30,
                   0x01, 0x41, 0x32, 0x38, 0x00, 0x04, 0x7C, 0xEF,
                   0x32, 0x28, 0x84, 0x43, 0xFC, 0xC9, 0x00, 0x00};

  // TX side: compute CRC, append, whiten
  uint16_t crc = cc1101_crc16(raw, 30);
  raw[30] = static_cast<uint8_t>(crc >> 8);
  raw[31] = static_cast<uint8_t>(crc & 0xFF);
  EXPECT_EQ(crc, 0x3AE6);

  cc1101_pn9_whiten(raw, 32);

  // RX side: de-whiten, verify CRC
  cc1101_pn9_whiten(raw, 32);  // de-whiten (same operation)

  uint16_t rx_crc = cc1101_crc16(raw, 30);
  uint16_t received_crc = (static_cast<uint16_t>(raw[30]) << 8) | raw[31];
  EXPECT_EQ(rx_crc, received_crc);

  // Verify original data recovered
  EXPECT_EQ(raw[0], 0x1D);   // length
  EXPECT_EQ(raw[2], 0x6A);   // type
  EXPECT_EQ(raw[7], 0x4F);   // src addr byte 0
}

TEST(TxPipeline, MatchesFirmwareLog) {
  // Complete verification against actual firmware TX log output.
  // The firmware logged "TX raw [30]" BEFORE whitening, which includes CRC.
  const uint8_t firmware_raw[] = {
      0x1B, 0x01, 0x44, 0x10, 0x00, 0x01, 0x03, 0x4F,
      0xCA, 0x30, 0x4F, 0xCA, 0x30, 0x4F, 0xCA, 0x30,
      0x01, 0x03, 0x00, 0x03, 0x54, 0xF4, 0x1E, 0xBC,
      0x6C, 0xDE, 0xA4, 0xB2, 0xBE, 0x64};

  // Verify CRC matches what firmware computed
  uint16_t crc = cc1101_crc16(firmware_raw, 28);
  EXPECT_EQ(crc, 0xBE64);
  EXPECT_EQ(firmware_raw[28], 0xBE);  // CRC MSB
  EXPECT_EQ(firmware_raw[29], 0x64);  // CRC LSB

  // Whiten and verify against Python-computed result
  uint8_t buf[30];
  memcpy(buf, firmware_raw, 30);
  cc1101_pn9_whiten(buf, 30);

  // These whitened values were verified by Python AND by CC1101 reception (crc_ok:true)
  EXPECT_EQ(buf[0], 0xE4);
  EXPECT_EQ(buf[1], 0xE0);
  EXPECT_EQ(buf[2], 0x59);
  EXPECT_EQ(buf[29], 0x50);
}
