#pragma once

/// @file cc1101_compat.h
/// @brief CC1101-compatible CRC-16 and PN9 whitening — pure functions, no hardware deps.
///
/// Used by the Semtech drivers to preserve the tested CC1101 wire format.
/// CC1101 applies these operations in hardware; our Semtech paths use software.

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
/// in hardware; our Semtech paths apply it in software.
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

/// Validate a de-whitened frame before replacing its wire CRC with hub metadata.
/// Extra bytes from fixed-length capture are ignored; both CRC bytes must exist.
inline bool cc1101_frame_valid(const uint8_t *data, size_t len) {
  if (data == nullptr || len < 3 || data[0] == 0) return false;
  const size_t crc_offset = 1 + data[0];
  if (crc_offset + 2 > len) return false;
  const uint16_t received = (static_cast<uint16_t>(data[crc_offset]) << 8) |
                            data[crc_offset + 1];
  return cc1101_crc16(data, crc_offset) == received;
}

}  // namespace elero
}  // namespace esphome
