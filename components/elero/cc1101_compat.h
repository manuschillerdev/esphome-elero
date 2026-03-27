#pragma once

/// @file cc1101_compat.h
/// @brief CC1101-compatible CRC-16 and PN9 whitening — pure functions, no hardware deps.
///
/// Used by the SX1262 driver to produce packets that CC1101 receivers can decode.
/// The CC1101 hardware applies these automatically; the SX1262 must do them in software
/// because its built-in CRC and whitening are incompatible (different polynomials).

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace elero {

/// CC1101 CRC-16: polynomial 0x8005 (x^16 + x^15 + x^2 + 1), init 0xFFFF.
/// Computed over data bytes BEFORE whitening. CC1101 auto-appends on TX;
/// SX1262 must compute in software. MSB-first bit processing.
inline uint16_t cc1101_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x8005;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

/// CC1101 IBM PN9 whitening/de-whitening (XOR is self-inverse).
/// Polynomial x^9 + x^5 + 1, seed 0x1FF, right-shifting LFSR.
/// Applied to [length + data + CRC] after sync word. The CC1101 does this
/// in hardware; the SX1262 must apply it in software since its built-in
/// whitening uses an incompatible scrambler (NOT IBM PN9).
inline void cc1101_pn9_whiten(uint8_t *data, size_t len) {
  uint16_t key = 0x1FF;
  for (size_t i = 0; i < len; ++i) {
    data[i] ^= key & 0xFF;
    for (int j = 0; j < 8; ++j) {
      uint16_t msb = ((key >> 5) ^ (key >> 0)) & 1;
      key = (key >> 1) | (msb << 8);
    }
  }
}

}  // namespace elero
}  // namespace esphome
