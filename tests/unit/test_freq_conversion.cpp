/// @file test_freq_conversion.cpp
/// @brief Tests for CC1101 → SX1262/SX1276 frequency register conversion.
///
/// All three radios store the Elero frequency in CC1101 register format
/// (freq2/freq1/freq0). The SX1262 and SX1276 drivers convert to their
/// native format at runtime. These tests verify the conversion math.

#include <gtest/gtest.h>
#include <cstdint>

// ═══════════════════════════════════════════════════════════════════════════════
// FREQUENCY CONVERSION FUNCTIONS (extracted from drivers, pure math)
// ═══════════════════════════════════════════════════════════════════════════════

/// SX1262: RfFreq = FREQ * 26e6 * 2^25 / (2^16 * 32e6) = FREQ * 416
static uint32_t sx1262_freq_from_cc1101(uint8_t f2, uint8_t f1, uint8_t f0) {
  uint32_t cc1101_freq = (static_cast<uint32_t>(f2) << 16) |
                          (static_cast<uint32_t>(f1) << 8) |
                          static_cast<uint32_t>(f0);
  return static_cast<uint32_t>(static_cast<uint64_t>(cc1101_freq) * 416ULL);
}

/// SX1276: Frf = FREQ * 26e6 * 2^19 / (2^16 * 32e6) = FREQ * 13 / 2
static uint32_t sx1276_freq_from_cc1101(uint8_t f2, uint8_t f1, uint8_t f0) {
  uint32_t cc1101_freq = (static_cast<uint32_t>(f2) << 16) |
                          (static_cast<uint32_t>(f1) << 8) |
                          static_cast<uint32_t>(f0);
  return static_cast<uint32_t>((static_cast<uint64_t>(cc1101_freq) * 13ULL) / 2ULL);
}

/// Convert CC1101 FREQ register to Hz: freq_hz = 26e6 * FREQ / 2^16
static double cc1101_freq_to_hz(uint8_t f2, uint8_t f1, uint8_t f0) {
  uint32_t cc1101_freq = (static_cast<uint32_t>(f2) << 16) |
                          (static_cast<uint32_t>(f1) << 8) |
                          static_cast<uint32_t>(f0);
  return 26.0e6 * cc1101_freq / 65536.0;
}

/// Convert SX1262 RfFreq to Hz: freq_hz = RfFreq * 32e6 / 2^25
static double sx1262_freq_to_hz(uint32_t rf_freq) {
  return static_cast<double>(rf_freq) * 32.0e6 / 33554432.0;
}

/// Convert SX1276 Frf to Hz: freq_hz = Frf * 32e6 / 2^19
static double sx1276_freq_to_hz(uint32_t frf) {
  return static_cast<double>(frf) * 32.0e6 / 524288.0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// TESTS
// ═══════════════════════════════════════════════════════════════════════════════

// Default Elero frequency registers: freq2=0x21, freq1=0x71, freq0=0x7A
// Actual carrier: 26e6 * 0x21717A / 2^16 = 869,524,963 Hz ≈ 869.525 MHz
// (The YAML docs say "868.35 MHz" but that's the nominal Elero band label,
//  the CC1101 registers encode the exact frequency.)
TEST(FreqConversion, DefaultFrequencyRegisters) {
  double cc1101_hz = cc1101_freq_to_hz(0x21, 0x71, 0x7A);
  EXPECT_NEAR(cc1101_hz, 869525000.0, 500.0);

  uint32_t sx1262_reg = sx1262_freq_from_cc1101(0x21, 0x71, 0x7A);
  double sx1262_hz = sx1262_freq_to_hz(sx1262_reg);
  EXPECT_NEAR(sx1262_hz, cc1101_hz, 500.0);  // SX1262 matches CC1101

  uint32_t sx1276_reg = sx1276_freq_from_cc1101(0x21, 0x71, 0x7A);
  double sx1276_hz = sx1276_freq_to_hz(sx1276_reg);
  EXPECT_NEAR(sx1276_hz, cc1101_hz, 500.0);  // SX1276 matches CC1101
}

// Alternative frequency registers: freq0=0xC0
// Actual carrier: 26e6 * 0x2171C0 / 2^16 = 869,552,734 Hz ≈ 869.553 MHz
TEST(FreqConversion, AlternativeFrequencyRegisters) {
  double cc1101_hz = cc1101_freq_to_hz(0x21, 0x71, 0xC0);
  EXPECT_NEAR(cc1101_hz, 869553000.0, 500.0);

  uint32_t sx1262_reg = sx1262_freq_from_cc1101(0x21, 0x71, 0xC0);
  double sx1262_hz = sx1262_freq_to_hz(sx1262_reg);
  EXPECT_NEAR(sx1262_hz, cc1101_hz, 500.0);

  uint32_t sx1276_reg = sx1276_freq_from_cc1101(0x21, 0x71, 0xC0);
  double sx1276_hz = sx1276_freq_to_hz(sx1276_reg);
  EXPECT_NEAR(sx1276_hz, cc1101_hz, 500.0);
}

// All three radios should agree on the target frequency (within Fstep tolerance)
TEST(FreqConversion, AllRadiosAgree) {
  uint8_t f2 = 0x21;
  uint8_t f1 = 0x71;
  uint8_t f0 = 0x7A;

  double cc1101_hz = cc1101_freq_to_hz(f2, f1, f0);
  double sx1262_hz = sx1262_freq_to_hz(sx1262_freq_from_cc1101(f2, f1, f0));
  double sx1276_hz = sx1276_freq_to_hz(sx1276_freq_from_cc1101(f2, f1, f0));

  // CC1101 Fstep = 26e6/2^16 = 396.7 Hz
  // SX1276 Fstep = 32e6/2^19 = 61.035 Hz
  // SX1262 Fstep = 32e6/2^25 = 0.954 Hz
  // Max error relative to CC1101 should be within CC1101's own Fstep
  EXPECT_NEAR(sx1262_hz, cc1101_hz, 400.0);
  EXPECT_NEAR(sx1276_hz, cc1101_hz, 400.0);
}

// Verify SX1262 conversion produces consistent register value
TEST(FreqConversion, SX1262RegisterConsistency) {
  uint32_t reg = sx1262_freq_from_cc1101(0x21, 0x71, 0x7A);
  // 0x21717A = 2,191,738 — use 64-bit to avoid overflow in the test itself
  uint32_t expected = static_cast<uint32_t>(2191738ULL * 416ULL);
  EXPECT_EQ(reg, expected);

  // Convert back to Hz and verify against CC1101
  double sx1262_hz = sx1262_freq_to_hz(reg);
  double cc1101_hz = cc1101_freq_to_hz(0x21, 0x71, 0x7A);
  EXPECT_NEAR(sx1262_hz, cc1101_hz, 400.0);
}

// Verify SX1276 register value is in expected 24-bit range
TEST(FreqConversion, SX1276RegisterRange) {
  uint32_t reg = sx1276_freq_from_cc1101(0x21, 0x71, 0x7A);
  // SX1276 Frf is 24-bit (3 bytes)
  EXPECT_LT(reg, 0x01000000U);  // Must fit in 24 bits
  EXPECT_GT(reg, 0x00D00000U);  // ~855 MHz lower bound

  // Verify conversion is close to direct calculation
  // Expected: cc1101_hz / sx1276_fstep
  double cc1101_hz = cc1101_freq_to_hz(0x21, 0x71, 0x7A);
  double frf_exact = cc1101_hz / 61.03515625;
  EXPECT_NEAR(static_cast<double>(reg), frf_exact, 10.0);
}

// Edge case: minimum CC1101 frequency (all zeros = 0 Hz)
TEST(FreqConversion, ZeroFrequency) {
  EXPECT_EQ(sx1262_freq_from_cc1101(0, 0, 0), 0U);
  EXPECT_EQ(sx1276_freq_from_cc1101(0, 0, 0), 0U);
}

// Edge case: maximum CC1101 FREQ register value
// Note: 0xFFFFFF * 416 overflows uint32_t (6.6 GHz is physically impossible).
// The SX1276 conversion (FREQ * 13 / 2) does NOT overflow since max is ~108M.
// We only test SX1276 here; SX1262 overflow is expected and irrelevant.
TEST(FreqConversion, MaxFreqRegister) {
  uint32_t sx1276_reg = sx1276_freq_from_cc1101(0xFF, 0xFF, 0xFF);
  EXPECT_GT(sx1276_reg, 0U);

  // CC1101 max FREQ: 26e6 * 0xFFFFFF / 2^16 = ~6,655.99 MHz (theoretical)
  double hz = cc1101_freq_to_hz(0xFF, 0xFF, 0xFF);
  EXPECT_NEAR(hz, 6656.0e6, 1e6);

  // SX1276 should still agree (no overflow: 0xFFFFFF * 13 / 2 = 108,789,757)
  double sx1276_hz = sx1276_freq_to_hz(sx1276_reg);
  EXPECT_NEAR(sx1276_hz, hz, 400.0);
}
