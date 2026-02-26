#pragma once

#include <cstdint>

/// @file elero_protocol.h
/// @brief Pure functions for Elero RF protocol encoding/decoding.
///
/// These functions are extracted from the Elero class to enable unit testing
/// without hardware dependencies. They implement the encryption and packet
/// encoding used by Elero wireless blinds.
///
/// Encryption algorithm based on:
/// - https://github.com/QuadCorei8085/elero_protocol (MIT)
/// - https://github.com/stanleypa/eleropy (GPLv3)

namespace esphome {
namespace elero {
namespace protocol {

/// Lookup tables for nibble encoding/decoding (Elero protocol scrambling)
/// Using inline constexpr ensures a single copy in flash (DROM) across all translation units.
inline constexpr uint8_t ENCODE_TABLE[] = {0x08, 0x02, 0x0d, 0x01, 0x0f, 0x0e, 0x07, 0x05,
                                           0x09, 0x0c, 0x00, 0x0a, 0x03, 0x04, 0x0b, 0x06};
inline constexpr uint8_t DECODE_TABLE[] = {0x0a, 0x03, 0x01, 0x0c, 0x0d, 0x07, 0x0f, 0x06,
                                           0x00, 0x08, 0x0b, 0x0e, 0x09, 0x02, 0x05, 0x04};

/// Count number of set bits in a byte, return parity (0 or 1).
inline uint8_t count_bits(uint8_t byte) {
  uint8_t ones = 0;
  uint8_t mask = 1;
  for (uint8_t i = 0; i < 8; ++i) {
    if (mask & byte) {
      ++ones;
    }
    mask <<= 1;
  }
  return ones & 0x01;
}

/// Calculate parity byte for an 8-byte message.
/// Stores result in msg[7].
inline void calc_parity(uint8_t *msg) {
  uint8_t p = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    uint8_t a = count_bits(msg[0 + i * 2]);
    uint8_t b = count_bits(msg[1 + i * 2]);
    p |= a ^ b;
    p <<= 1;
  }
  msg[7] = (p << 3);
}

/// Add r20 value to nibbles (encoding step).
inline void add_r20_to_nibbles(uint8_t *msg, uint8_t r20, uint8_t start, uint8_t length) {
  for (uint8_t i = start; i < length; ++i) {
    uint8_t d = msg[i];
    uint8_t ln = (d + r20) & 0x0F;
    uint8_t hn = ((d & 0xF0) + (r20 & 0xF0)) & 0xFF;
    msg[i] = hn | ln;
    r20 = (r20 - 0x22) & 0xFF;
  }
}

/// Subtract r20 value from nibbles (decoding step).
inline void sub_r20_from_nibbles(uint8_t *msg, uint8_t r20, uint8_t start, uint8_t length) {
  for (uint8_t i = start; i < length; ++i) {
    uint8_t d = msg[i];
    uint8_t ln = (d - r20) & 0x0F;
    uint8_t hn = ((d & 0xF0) - (r20 & 0xF0)) & 0xFF;
    msg[i] = hn | ln;
    r20 = (r20 - 0x22) & 0xFF;
  }
}

/// XOR 2-byte pattern across array (encoding).
inline void xor_2byte_in_array_encode(uint8_t *msg, uint8_t xor0, uint8_t xor1) {
  for (uint8_t i = 1; i < 4; ++i) {
    msg[i * 2 + 0] = msg[i * 2 + 0] ^ xor0;
    msg[i * 2 + 1] = msg[i * 2 + 1] ^ xor1;
  }
}

/// XOR 2-byte pattern across array (decoding).
inline void xor_2byte_in_array_decode(uint8_t *msg, uint8_t xor0, uint8_t xor1) {
  for (uint8_t i = 0; i < 4; ++i) {
    msg[i * 2 + 0] = msg[i * 2 + 0] ^ xor0;
    msg[i * 2 + 1] = msg[i * 2 + 1] ^ xor1;
  }
}

/// Encode nibbles using lookup table.
inline void encode_nibbles(uint8_t *msg, uint8_t len = 8) {
  for (uint8_t i = 0; i < len; ++i) {
    uint8_t nh = (msg[i] >> 4) & 0x0F;
    uint8_t nl = msg[i] & 0x0F;
    uint8_t dh = ENCODE_TABLE[nh];
    uint8_t dl = ENCODE_TABLE[nl];
    msg[i] = ((dh << 4) & 0xFF) | ((dl) & 0xFF);
  }
}

/// Decode nibbles using lookup table.
inline void decode_nibbles(uint8_t *msg, uint8_t len) {
  for (uint8_t i = 0; i < len; ++i) {
    uint8_t nh = (msg[i] >> 4) & 0x0F;
    uint8_t nl = msg[i] & 0x0F;
    uint8_t dh = DECODE_TABLE[nh];
    uint8_t dl = DECODE_TABLE[nl];
    msg[i] = ((dh << 4) & 0xFF) | ((dl) & 0xFF);
  }
}

/// Decode an 8-byte Elero payload message.
/// @param msg Pointer to 8-byte buffer (modified in place)
inline void msg_decode(uint8_t *msg) {
  decode_nibbles(msg, 8);
  sub_r20_from_nibbles(msg, 0xFE, 0, 2);
  xor_2byte_in_array_decode(msg, msg[0], msg[1]);
  sub_r20_from_nibbles(msg, 0xBA, 2, 8);
}

/// Encode an 8-byte Elero payload message.
/// @param msg Pointer to 8-byte buffer (modified in place)
inline void msg_encode(uint8_t *msg) {
  uint8_t xor0 = msg[0];
  uint8_t xor1 = msg[1];
  calc_parity(msg);
  add_r20_to_nibbles(msg, 0xFE, 0, 8);
  xor_2byte_in_array_encode(msg, xor0, xor1);
  encode_nibbles(msg);
}

}  // namespace protocol
}  // namespace elero
}  // namespace esphome
