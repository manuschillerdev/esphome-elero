/// @file test_encryption_vectors.cpp
/// @brief Known-answer encryption tests and lookup table integrity checks.
///
/// These tests lock down the EXACT output of msg_encode/msg_decode against
/// known-good values derived from the working implementation (commit fe33d186).
/// If anyone changes the lookup tables, R20 constants, XOR step order, or
/// any other encryption detail, these tests WILL fail.

#include <gtest/gtest.h>
#include <cstring>
#include "elero/elero_protocol.h"
#include "elero/elero_packet.h"

using namespace esphome::elero;

// =============================================================================
// LOOKUP TABLE INTEGRITY
// DECODE_TABLE[ENCODE_TABLE[i]] must equal i for all 0..15
// =============================================================================

TEST(LookupTableIntegrity, EncodeDecodeAreInverses) {
  for (uint8_t i = 0; i < 16; i++) {
    SCOPED_TRACE("nibble value: " + std::to_string(i));
    EXPECT_EQ(protocol::DECODE_TABLE[protocol::ENCODE_TABLE[i]], i);
  }
}

TEST(LookupTableIntegrity, DecodeEncodeAreInverses) {
  for (uint8_t i = 0; i < 16; i++) {
    SCOPED_TRACE("nibble value: " + std::to_string(i));
    EXPECT_EQ(protocol::ENCODE_TABLE[protocol::DECODE_TABLE[i]], i);
  }
}

TEST(LookupTableIntegrity, EncodeTableIsBijection) {
  // Every output value 0..15 must appear exactly once
  bool seen[16] = {false};
  for (uint8_t i = 0; i < 16; i++) {
    EXPECT_LT(protocol::ENCODE_TABLE[i], 16u);
    EXPECT_FALSE(seen[protocol::ENCODE_TABLE[i]])
      << "Duplicate output " << (int)protocol::ENCODE_TABLE[i] << " at index " << (int)i;
    seen[protocol::ENCODE_TABLE[i]] = true;
  }
}

// =============================================================================
// KNOWN-ANSWER ENCRYPTION VECTORS
// Generated from working implementation, locked down as regression tests.
//
// Each vector tests msg_encode() against an exact expected ciphertext.
// These catch: wrong lookup table, wrong R20 value, wrong step order,
// wrong XOR range, wrong parity calculation.
// =============================================================================

struct EncryptionVector {
  const char* name;
  uint8_t counter;
  uint8_t command;
  uint8_t state_byte;  // 0 for command packets
  uint8_t plaintext[8];
  uint8_t expected_ciphertext[8];
  // After decode, positions 2-6 should match plaintext[2-6]
};

// Vectors generated from working implementation
static const EncryptionVector VECTORS[] = {
  {
    "counter=1, cmd=UP(0x20)",
    1, 0x20, 0x00,
    {0x8F, 0x71, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x54, 0xF4, 0xEE, 0xBC, 0x6C, 0xDE, 0xA4, 0x02},
  },
  {
    "counter=42, cmd=DOWN(0x40)",
    42, 0x40, 0x00,
    {0x88, 0x8A, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x57, 0xE7, 0x5D, 0x2D, 0x6B, 0x4B, 0xA0, 0xE0},
  },
  {
    "counter=255, cmd=STOP(0x10)",
    255, 0x10, 0x00,
    {0xE1, 0x8F, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x46, 0xEA, 0xDA, 0x25, 0xC5, 0x4A, 0x41, 0xE6},
  },
  {
    "counter=100, cmd=CHECK(0x00), state=TOP(0x01)",
    100, 0x00, 0x01,
    {0x08, 0x24, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00},
    {0x67, 0x68, 0xAD, 0xA3, 0x5B, 0x58, 0x1A, 0x9F},
  },
  {
    "counter=1, cmd=TILT(0x24)",
    1, 0x24, 0x00,
    {0x8F, 0x71, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x54, 0xF4, 0xE2, 0xBC, 0x6C, 0xDE, 0xA4, 0xB2},
  },
};

class EncryptionVectorTest : public ::testing::TestWithParam<size_t> {};

TEST_P(EncryptionVectorTest, EncodeProducesExactCiphertext) {
  const auto& v = VECTORS[GetParam()];
  SCOPED_TRACE(v.name);

  uint8_t buf[8];
  memcpy(buf, v.plaintext, 8);
  protocol::msg_encode(buf);

  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(buf[i], v.expected_ciphertext[i])
      << "Byte " << i << " mismatch for " << v.name;
  }
}

TEST_P(EncryptionVectorTest, DecodeRecoversCriticalPositions) {
  const auto& v = VECTORS[GetParam()];
  SCOPED_TRACE(v.name);

  uint8_t buf[8];
  memcpy(buf, v.expected_ciphertext, 8);
  protocol::msg_decode(buf);

  // Positions 2 (command) through 6 (state) must survive roundtrip
  for (int i = 2; i <= 6; i++) {
    EXPECT_EQ(buf[i], v.plaintext[i])
      << "Byte " << i << " not recovered for " << v.name;
  }
}

TEST_P(EncryptionVectorTest, CryptoCodeMatchesCounter) {
  const auto& v = VECTORS[GetParam()];
  SCOPED_TRACE(v.name);

  uint16_t expected_code = packet::calc_crypto_code(v.counter);
  EXPECT_EQ(v.plaintext[0], (expected_code >> 8) & 0xFF);
  EXPECT_EQ(v.plaintext[1], expected_code & 0xFF);
}

INSTANTIATE_TEST_SUITE_P(
  KnownAnswerVectors,
  EncryptionVectorTest,
  ::testing::Range(size_t{0}, sizeof(VECTORS) / sizeof(VECTORS[0]))
);

// =============================================================================
// PARITY TESTS WITH NON-TRIVIAL INPUTS
// =============================================================================

TEST(CalcParity, AllZeros) {
  uint8_t msg[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  protocol::calc_parity(msg);
  EXPECT_EQ(msg[7], 0x00);
}

TEST(CalcParity, SingleBitDifference) {
  // msg[0]=0x01 (1 bit, parity=1), msg[1]=0x00 (0 bits, parity=0)
  // pair 0: 1^0 = 1
  // Other pairs: 0^0 = 0
  // p after loop: shifted and accumulated
  uint8_t msg[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  protocol::calc_parity(msg);
  // Trace: i=0: p|=1, p<<=1 → p=0b10
  //        i=1: p|=0, p<<=1 → p=0b100
  //        i=2: p|=0, p<<=1 → p=0b1000
  //        i=3: p|=0, p<<=1 → p=0b10000
  // msg[7] = 0b10000 << 3 = 0b10000000 = 0x80
  EXPECT_EQ(msg[7], 0x80);
}

TEST(CalcParity, AllOddParity) {
  // All pairs have different parities
  // msg[0]=0x01 (odd), msg[1]=0x00 (even) → 1
  // msg[2]=0x01 (odd), msg[3]=0x00 (even) → 1
  // msg[4]=0x01 (odd), msg[5]=0x00 (even) → 1
  // msg[6]=0x01 (odd), msg[7] = ? (will be overwritten)
  uint8_t msg[8] = {0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00};
  protocol::calc_parity(msg);
  // i=0: p|=1, p<<=1 → 0b10
  // i=1: p|=1 → 0b11, p<<=1 → 0b110
  // i=2: p|=1 → 0b111, p<<=1 → 0b1110
  // i=3: a=count_bits(0x01)=1, b=count_bits(msg[7])=count_bits(0x00 at time of read)=0
  //      p|=1 → 0b1111, p<<=1 → 0b11110
  // msg[7] = 0b11110 << 3 = 0b11110000 = 0xF0
  EXPECT_EQ(msg[7], 0xF0);
}

TEST(CalcParity, RealWorldPayload_Counter1_CmdUp) {
  // Use the actual plaintext from vector 1 before encoding
  uint8_t msg[8] = {0x8F, 0x71, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};
  protocol::calc_parity(msg);
  // 0x8F=10001111 (5 bits, parity 1), 0x71=01110001 (4 bits, parity 0) → 1^0=1
  // 0x20=00100000 (1 bit, parity 1), 0x00 (0) → 1^0=1
  // 0x00, 0x00 → 0
  // 0x00, 0x00(was original msg[7]) → 0
  // p: i=0→p=0b10, i=1→p=0b110, i=2→p=0b1100, i=3→p=0b11000
  // msg[7] = 0b11000 << 3 = 0xC0
  EXPECT_EQ(msg[7], 0xC0);
}

TEST(CalcParity, SymmetricInput) {
  // Both bytes in each pair have same parity → XOR = 0
  uint8_t msg[8] = {0x03, 0x05, 0x03, 0x05, 0x03, 0x05, 0x03, 0x00};
  // 0x03=2 bits (even), 0x05=2 bits (even) → 0^0=0
  protocol::calc_parity(msg);
  // All pairs produce 0, so p stays 0 through all iterations
  EXPECT_EQ(msg[7], 0x00);
}

// =============================================================================
// NEGATIVE TESTS: Tampered ciphertext, double-encode, encode ≠ decode
// =============================================================================

TEST(EncryptionNegative, SingleBitFlipCorruptsDecryption) {
  // Flip one bit in valid ciphertext → decrypted command must differ
  const auto& v = VECTORS[0];  // counter=1, cmd=UP

  uint8_t tampered[8];
  memcpy(tampered, v.expected_ciphertext, 8);
  tampered[2] ^= 0x01;  // Flip LSB of byte 2 (command position after decode)

  protocol::msg_decode(tampered);

  // At least one of the critical positions (2-6) must differ from plaintext
  bool any_differ = false;
  for (int i = 2; i <= 6; i++) {
    if (tampered[i] != v.plaintext[i]) {
      any_differ = true;
      break;
    }
  }
  EXPECT_TRUE(any_differ) << "Bit flip in ciphertext had no effect on decrypted payload";
}

TEST(EncryptionNegative, DoubleEncodeIsNotIdentity) {
  // encode(encode(x)) must NOT equal x — encryption is not its own inverse
  const auto& v = VECTORS[0];

  uint8_t buf[8];
  memcpy(buf, v.plaintext, 8);
  protocol::msg_encode(buf);
  protocol::msg_encode(buf);  // Double-encode

  // Must NOT recover original plaintext
  EXPECT_NE(memcmp(buf, v.plaintext, 8), 0)
    << "Double-encode unexpectedly recovered plaintext (encode is self-inverse)";
}

TEST(EncryptionNegative, DecodeWithoutEncodeProducesGarbage) {
  // Decoding plaintext (not ciphertext) should NOT produce the same plaintext
  const auto& v = VECTORS[0];

  uint8_t buf[8];
  memcpy(buf, v.plaintext, 8);
  protocol::msg_decode(buf);  // Decode raw plaintext

  EXPECT_NE(memcmp(buf, v.plaintext, 8), 0)
    << "Decoding plaintext returned plaintext unchanged (decode is identity)";
}

TEST(EncryptionNegative, WrongCounterProducesDifferentCiphertext) {
  // Same command but different counter must produce different ciphertext
  uint8_t buf1[8] = {0x8F, 0x71, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};  // counter=1
  uint8_t buf2[8] = {0x88, 0x8A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};  // counter=42, same cmd

  protocol::msg_encode(buf1);
  protocol::msg_encode(buf2);

  EXPECT_NE(memcmp(buf1, buf2, 8), 0)
    << "Different counters produced identical ciphertext";
}

TEST(EncryptionNegative, AllZeroPlaintextEncodesToNonZero) {
  uint8_t buf[8] = {0};
  protocol::msg_encode(buf);

  uint8_t zeros[8] = {0};
  EXPECT_NE(memcmp(buf, zeros, 8), 0)
    << "Encoding all-zeros produced all-zeros (encryption is no-op)";
}

// =============================================================================
// INDIVIDUAL ENCRYPTION STEPS (P2: pin each step independently)
// =============================================================================

TEST(CountBits, DirectTests) {
  // count_bits returns parity (0=even, 1=odd number of set bits)
  EXPECT_EQ(protocol::count_bits(0x00), 0u);  // 0 bits → even
  EXPECT_EQ(protocol::count_bits(0x01), 1u);  // 1 bit → odd
  EXPECT_EQ(protocol::count_bits(0x03), 0u);  // 2 bits → even
  EXPECT_EQ(protocol::count_bits(0x07), 1u);  // 3 bits → odd
  EXPECT_EQ(protocol::count_bits(0xFF), 0u);  // 8 bits → even
  EXPECT_EQ(protocol::count_bits(0x80), 1u);  // 1 bit (MSB) → odd
  EXPECT_EQ(protocol::count_bits(0x55), 0u);  // 4 bits → even
  EXPECT_EQ(protocol::count_bits(0xAA), 0u);  // 4 bits → even
  EXPECT_EQ(protocol::count_bits(0xFE), 1u);  // 7 bits → odd
}

TEST(R20Nibbles, AddSubRoundtrip) {
  // add_r20 followed by sub_r20 with same params must recover original
  uint8_t original[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
  uint8_t buf[8];
  memcpy(buf, original, 8);

  protocol::add_r20_to_nibbles(buf, protocol::crypto_local::R20_INITIAL, 0, 8);
  protocol::sub_r20_from_nibbles(buf, protocol::crypto_local::R20_INITIAL, 0, 8);

  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(buf[i], original[i]) << "Byte " << i << " not recovered after add/sub R20";
  }
}

TEST(R20Nibbles, AddSubRoundtripPartialRange) {
  // Test with start=2, length=8 (the R20_SECOND path used in decode)
  uint8_t original[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x11, 0x22, 0x33};
  uint8_t buf[8];
  memcpy(buf, original, 8);

  protocol::add_r20_to_nibbles(buf, protocol::crypto_local::R20_SECOND, 2, 8);
  protocol::sub_r20_from_nibbles(buf, protocol::crypto_local::R20_SECOND, 2, 8);

  // Bytes 0-1 untouched, bytes 2-7 recovered
  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(buf[i], original[i]) << "Byte " << i;
  }
}

TEST(R20Nibbles, AddIsNotIdentity) {
  // Adding R20 must actually change the data
  uint8_t buf[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  protocol::add_r20_to_nibbles(buf, protocol::crypto_local::R20_INITIAL, 0, 8);

  uint8_t zeros[8] = {0};
  EXPECT_NE(memcmp(buf, zeros, 8), 0) << "add_r20 on zeros produced zeros";
}

TEST(XorArrays, EncodeDecodeRoundtrip) {
  // encode XORs bytes 2-7 (starts at i=1), decode XORs bytes 0-7 (starts at i=0)
  // This asymmetry is by design: bytes 0-1 are the XOR key, processed separately by R20.
  // So encode→decode recovers bytes 2-7 but NOT bytes 0-1.
  uint8_t original[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  uint8_t buf[8];
  memcpy(buf, original, 8);

  uint8_t xor0 = buf[0], xor1 = buf[1];
  protocol::xor_2byte_in_array_encode(buf, xor0, xor1);
  protocol::xor_2byte_in_array_decode(buf, xor0, xor1);

  // Bytes 2-7 should be recovered (XOR twice cancels)
  for (int i = 2; i < 8; i++) {
    EXPECT_EQ(buf[i], original[i]) << "Byte " << i;
  }
  // Bytes 0-1 are XORed once by decode (encode doesn't touch them)
  EXPECT_EQ(buf[0], original[0] ^ xor0);
  EXPECT_EQ(buf[1], original[1] ^ xor1);
}

TEST(XorArrays, EncodeAsymmetry) {
  // Encode starts at i=1 (bytes 2-7), decode starts at i=0 (bytes 0-7)
  // Encoding must NOT modify bytes 0-1
  uint8_t buf[8] = {0xAA, 0xBB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  protocol::xor_2byte_in_array_encode(buf, 0xAA, 0xBB);

  EXPECT_EQ(buf[0], 0xAA);  // Untouched by encode
  EXPECT_EQ(buf[1], 0xBB);  // Untouched by encode
  // Bytes 2+ are XORed with (0xAA, 0xBB)
  EXPECT_EQ(buf[2], 0xAA);  // 0x00 ^ 0xAA
  EXPECT_EQ(buf[3], 0xBB);  // 0x00 ^ 0xBB
}

TEST(NibbleCodec, EncodeDecodeRoundtrip) {
  // encode_nibbles then decode_nibbles must recover original
  uint8_t original[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
  uint8_t buf[8];
  memcpy(buf, original, 8);

  protocol::encode_nibbles(buf, 8);
  protocol::decode_nibbles(buf, 8);

  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(buf[i], original[i]) << "Byte " << i;
  }
}

TEST(NibbleCodec, EncodeChangesData) {
  uint8_t buf[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
  uint8_t original[8];
  memcpy(original, buf, 8);

  protocol::encode_nibbles(buf, 8);

  EXPECT_NE(memcmp(buf, original, 8), 0) << "encode_nibbles had no effect";
}

TEST(NibbleCodec, HighAndLowNibblesProcessedIndependently) {
  // Encode 0x00 and 0xF0 — high nibble should map independently of low
  uint8_t buf_lo[1] = {0x01};  // Low nibble = 1, high = 0
  uint8_t buf_hi[1] = {0x10};  // Low nibble = 0, high = 1

  protocol::encode_nibbles(buf_lo, 1);
  protocol::encode_nibbles(buf_hi, 1);

  // ENCODE_TABLE[0] = 0x08, ENCODE_TABLE[1] = 0x02
  // buf_lo: high=ENCODE[0]=0x08, low=ENCODE[1]=0x02 → 0x82
  EXPECT_EQ(buf_lo[0], 0x82);
  // buf_hi: high=ENCODE[1]=0x02, low=ENCODE[0]=0x08 → 0x28
  EXPECT_EQ(buf_hi[0], 0x28);
}

// =============================================================================
// RSSI EDGE CASES
// =============================================================================

TEST(RssiEdgeCases, RawZero) {
  // 0/2 + (-74) = -74
  EXPECT_FLOAT_EQ(packet::calc_rssi(0), -74.0f);
}

TEST(RssiEdgeCases, RawMax255) {
  // 255 > 127, so treated as signed: -1
  // -1/2 + (-74) = -0.5 - 74 = -74.5
  EXPECT_FLOAT_EQ(packet::calc_rssi(255), -74.5f);
}

TEST(RssiEdgeCases, RawOne) {
  // 1/2 + (-74) = -73.5
  EXPECT_FLOAT_EQ(packet::calc_rssi(1), -73.5f);
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
