/// @file elero_packet.h
/// @brief Pure packet parsing functions for testability.
///
/// This module extracts packet parsing logic from Elero::interpret_msg()
/// into pure functions that can be unit tested without ESPHome dependencies.

#pragma once

#include <cstdint>
#include <cstddef>
#include "elero_protocol.h"

namespace esphome::elero::packet {

// ─── Constants ──────────────────────────────────────────────────────────────

constexpr uint8_t MAX_PACKET_SIZE = 57;       // Maximum valid packet length
constexpr uint8_t MAX_DESTINATIONS = 20;      // Maximum destination count
constexpr uint8_t FIFO_LENGTH = 64;           // CC1101 FIFO size
constexpr uint8_t MIN_PACKET_SIZE = 4;        // Minimum valid packet length
constexpr int8_t RSSI_OFFSET = -74;           // CC1101 RSSI offset (dBm)
constexpr uint8_t RSSI_SIGN_BIT = 127;        // Two's complement threshold

// ─── Parse Result ───────────────────────────────────────────────────────────

/// Result of parsing an RF packet.
/// All fields are populated if valid==true, otherwise only reject_reason is set.
struct ParseResult {
  bool valid{false};
  const char* reject_reason{nullptr};

  // Header fields
  uint8_t length{0};      ///< Packet length (from byte 0)
  uint8_t counter{0};     ///< Rolling counter
  uint8_t type{0};        ///< Message type (0x6a=command, 0xca=status)
  uint8_t type2{0};       ///< Secondary type byte
  uint8_t hop{0};         ///< Hop count
  uint8_t syst{0};        ///< System byte
  uint8_t channel{0};     ///< RF channel

  // Addresses (3 bytes each, big-endian)
  uint32_t src_addr{0};   ///< Source address
  uint32_t bwd_addr{0};   ///< Backward address
  uint32_t fwd_addr{0};   ///< Forward address
  uint32_t dst_addr{0};   ///< First destination address

  // Destination info
  uint8_t num_dests{0};   ///< Number of destinations
  uint8_t dests_len{0};   ///< Total bytes for destinations

  // RSSI/LQI (appended by CC1101)
  uint8_t rssi_raw{0};    ///< Raw RSSI byte
  float rssi{0.0f};       ///< Calculated RSSI in dBm
  uint8_t lqi{0};         ///< Link Quality Indicator
  uint8_t crc_ok{0};      ///< CRC status (1=OK)

  // Payload (after decryption)
  uint8_t payload[10]{0}; ///< Decrypted payload bytes
  uint8_t command{0};     ///< Command byte (for command packets)
  uint8_t state{0};       ///< State byte (for status packets)
};

// ─── Helper Functions ───────────────────────────────────────────────────────

/// Extract 3-byte big-endian address from buffer.
/// @param p Pointer to first byte of address
/// @return 32-bit address value
inline uint32_t extract_addr(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 16) |
         (static_cast<uint32_t>(p[1]) << 8) |
         static_cast<uint32_t>(p[2]);
}

/// Calculate RSSI in dBm from raw CC1101 value.
/// @param raw Raw RSSI byte from CC1101 (two's complement encoded)
/// @return RSSI in dBm
inline float calc_rssi(uint8_t raw) {
  if (raw > RSSI_SIGN_BIT) {
    // Negative value (two's complement)
    return static_cast<float>(static_cast<int8_t>(raw)) / 2.0f + RSSI_OFFSET;
  } else {
    // Positive value
    return static_cast<float>(raw) / 2.0f + RSSI_OFFSET;
  }
}

/// Check if packet type is a command packet.
/// @param type Message type byte
/// @return true if command packet (0x6a or 0x69)
inline bool is_command_packet(uint8_t type) {
  return type == 0x6a || type == 0x69;
}

/// Check if packet type is a status packet.
/// @param type Message type byte
/// @return true if status packet (0xca or 0xc9)
inline bool is_status_packet(uint8_t type) {
  return type == 0xca || type == 0xc9;
}

// ─── Main Parse Function ────────────────────────────────────────────────────

/// Parse a raw RF packet from the CC1101 FIFO.
///
/// This function validates the packet structure, extracts all fields,
/// and decrypts the payload. It mirrors the logic from Elero::interpret_msg()
/// but returns a structured result instead of performing side effects.
///
/// @param raw Pointer to raw packet data (from CC1101 FIFO)
/// @param raw_len Length of raw data in buffer
/// @return ParseResult with all fields populated if valid
ParseResult parse_packet(const uint8_t* raw, size_t raw_len);

}  // namespace esphome::elero::packet
