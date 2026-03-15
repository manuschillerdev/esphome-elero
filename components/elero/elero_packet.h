/// @file elero_packet.h
/// @brief Pure packet parsing functions for testability.
///
/// This module extracts packet parsing logic from Elero::interpret_msg()
/// into pure functions that can be unit tested without ESPHome dependencies.
///
/// This is the **single source of truth** for all Elero RF protocol constants.
/// All magic numbers should be defined here and imported elsewhere.
///
/// @see docs/PACKET_STRUCTURE.md for complete packet layout documentation.
///
/// Quick Reference - TX Packet (30 bytes):
///   [0]=len [1]=cnt [2]=type [3-4]=type2,hop [5]=sys [6]=ch
///   [7-9]=src [10-12]=bwd [13-15]=fwd [16]=ndst [17-19]=dst
///   [20-21]=payload1,2 [22-29]=encrypted(crypto,cmd,pad,parity)
///
/// Quick Reference - Commands:
///   CHECK=0x00  STOP=0x10  UP=0x20  TILT=0x24  DOWN=0x40  INT=0x44

#pragma once

#include <cstdint>
#include <cstddef>
#include "elero_protocol.h"

namespace esphome::elero::packet {

// ═══════════════════════════════════════════════════════════════════════════════
// PACKET SIZE LIMITS
// ═══════════════════════════════════════════════════════════════════════════════

constexpr uint8_t MAX_PACKET_SIZE = 57;       ///< Maximum valid packet length (FCC spec)
constexpr uint8_t MAX_DESTINATIONS = 20;      ///< Maximum destination count
constexpr uint8_t FIFO_LENGTH = 64;           ///< CC1101 FIFO size
constexpr uint8_t MIN_PACKET_SIZE = 4;        ///< Minimum valid packet length

// ═══════════════════════════════════════════════════════════════════════════════
// RSSI CALCULATION
// ═══════════════════════════════════════════════════════════════════════════════

constexpr int8_t RSSI_OFFSET = -74;           ///< CC1101 RSSI offset (dBm)
constexpr uint8_t RSSI_SIGN_BIT = 127;        ///< Two's complement threshold
constexpr uint8_t RSSI_DIVISOR = 2;           ///< Divisor for raw RSSI value

// ═══════════════════════════════════════════════════════════════════════════════
// CC1101 STATUS BYTE MASKS (SPI read status)
// ═══════════════════════════════════════════════════════════════════════════════

namespace cc1101_status {
// ── RXBYTES / TXBYTES register value masks ──
constexpr uint8_t RXBYTES_OVERFLOW_BIT = 0x80; ///< Bit 7 of RXBYTES register: RXFIFO overflow
constexpr uint8_t BYTE_COUNT_MASK = 0x7F;     ///< RXBYTES/TXBYTES count (bits 6:0)

// ── SPI status byte layout (returned with every SPI transaction) ──
//   Bit 7:    CHIP_RDY (0 = crystal stable, 1 = not ready)
//   Bits 6:4: STATE (000=IDLE, 001=RX, 010=TX, 011=FSTXON,
//             100=CAL, 101=SETTLING, 110=RXFIFO_OVF, 111=TXFIFO_UFL)
//   Bits 3:0: FIFO_BYTES_AVAILABLE
constexpr uint8_t SPI_CHIP_RDY = 0x80;           ///< Bit 7: chip not ready when set
constexpr uint8_t SPI_STATE_MASK = 0x70;          ///< Bits 6:4: radio state
constexpr uint8_t SPI_STATE_RXFIFO_OVERFLOW = 0x60;  ///< STATE=110: RXFIFO overflow
constexpr uint8_t SPI_STATE_TXFIFO_UNDERFLOW = 0x70; ///< STATE=111: TXFIFO underflow

// ── Other register value masks ──
constexpr uint8_t MARCSTATE_MASK = 0x1F;      ///< MARCSTATE value mask (bits 4:0)
constexpr uint8_t LQI_MASK = 0x7F;            ///< LQI value mask (bits 6:0)
constexpr uint8_t CRC_OK_BIT = 0x80;          ///< CRC OK flag (bit 7, in LQI byte)

// ── VERSION register expected values ──
constexpr uint8_t VERSION_NOT_CONNECTED_LOW = 0x00;   ///< VERSION reads 0x00 when chip absent
constexpr uint8_t VERSION_NOT_CONNECTED_HIGH = 0xFF;  ///< VERSION reads 0xFF when chip absent
}  // namespace cc1101_status

// ═══════════════════════════════════════════════════════════════════════════════
// MESSAGE TYPE CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════════

namespace msg_type {
constexpr uint8_t BUTTON = 0x44;              ///< Button press/release (broadcast)
constexpr uint8_t COMMAND = 0x6a;             ///< Targeted command to blind
constexpr uint8_t COMMAND_ALT = 0x69;         ///< Alternate command format
constexpr uint8_t STATUS = 0xca;              ///< Status response from blind
constexpr uint8_t STATUS_ALT = 0xc9;          ///< Alternate status format
constexpr uint8_t ADDR_3BYTE_THRESHOLD = 0x60;  ///< Types > this use 3-byte addressing
}  // namespace msg_type

// ═══════════════════════════════════════════════════════════════════════════════
// COMMAND BYTE CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════════

namespace command {
constexpr uint8_t CHECK = 0x00;               ///< Request status (no movement)
constexpr uint8_t STOP = 0x10;                ///< Stop movement
constexpr uint8_t UP = 0x20;                  ///< Move up / open
constexpr uint8_t TILT = 0x24;                ///< Tilt position
constexpr uint8_t DOWN = 0x40;                ///< Move down / close
constexpr uint8_t INTERMEDIATE = 0x44;        ///< Move to intermediate position
constexpr uint8_t INVALID = 0xFF;             ///< Invalid/unknown command marker
}  // namespace command

// ═══════════════════════════════════════════════════════════════════════════════
// STATE BYTE CONSTANTS (from status packets)
// ═══════════════════════════════════════════════════════════════════════════════

namespace state {
constexpr uint8_t UNKNOWN = 0x00;
constexpr uint8_t TOP = 0x01;                 ///< Fully open position
constexpr uint8_t BOTTOM = 0x02;              ///< Fully closed position
constexpr uint8_t INTERMEDIATE = 0x03;        ///< Intermediate position
constexpr uint8_t TILT = 0x04;                ///< Tilted position
constexpr uint8_t BLOCKING = 0x05;            ///< Obstacle detected
constexpr uint8_t OVERHEATED = 0x06;          ///< Motor overheated
constexpr uint8_t TIMEOUT = 0x07;             ///< Communication timeout
constexpr uint8_t START_MOVING_UP = 0x08;     ///< Starting upward movement
constexpr uint8_t START_MOVING_DOWN = 0x09;   ///< Starting downward movement
constexpr uint8_t MOVING_UP = 0x0a;           ///< Currently moving up
constexpr uint8_t MOVING_DOWN = 0x0b;         ///< Currently moving down
constexpr uint8_t STOPPED = 0x0d;             ///< Stopped (after movement)
constexpr uint8_t TOP_TILT = 0x0e;            ///< Open + tilted
constexpr uint8_t BOTTOM_TILT = 0x0f;         ///< Closed + tilted / Light off
constexpr uint8_t LIGHT_OFF = 0x0f;           ///< Light off (alias for BOTTOM_TILT)
constexpr uint8_t LIGHT_ON = 0x10;            ///< Light on
}  // namespace state

// ═══════════════════════════════════════════════════════════════════════════════
// DEFAULT VALUES
// ═══════════════════════════════════════════════════════════════════════════════

namespace defaults {
constexpr uint8_t HOP = 0x0a;                 ///< Default hop count
constexpr uint8_t TYPE2 = 0x00;               ///< Default secondary type
constexpr uint8_t PAYLOAD_1 = 0x00;           ///< Default payload byte 1
constexpr uint8_t PAYLOAD_2 = 0x04;           ///< Default payload byte 2
constexpr uint8_t SYS_ADDR = 0x01;            ///< System address (fixed)
constexpr uint8_t DEST_COUNT = 0x01;          ///< Single destination
}  // namespace defaults

// ═══════════════════════════════════════════════════════════════════════════════
// TIMING CONSTANTS (milliseconds)
// ═══════════════════════════════════════════════════════════════════════════════

namespace timing {
constexpr uint32_t POLL_INTERVAL_MOVING = 2000;   ///< Poll every 2s while moving
constexpr uint32_t DELAY_SEND_PACKETS = 50;       ///< 50ms between packet repeats
constexpr uint32_t TIMEOUT_MOVEMENT = 120000;     ///< Max 2min movement timeout
constexpr uint32_t POLL_OFFSET_SPACING = 5000;    ///< 5s spacing between blind polls
constexpr uint32_t TX_PENDING_TIMEOUT = 500;      ///< TX completion timeout
constexpr uint32_t RADIO_WATCHDOG_INTERVAL = 5000; ///< Radio health check every 5s
constexpr uint32_t PUBLISH_THROTTLE_MS = 1000;    ///< Throttle state publishes during movement/dimming
constexpr uint32_t DEFAULT_POLL_INTERVAL_MS = 300000;  ///< Default poll interval (5 min)
constexpr uint32_t MAX_BACKOFF_MS = 400;          ///< Maximum TX retry backoff delay
}  // namespace timing

// ═══════════════════════════════════════════════════════════════════════════════
// QUEUE/BUFFER LIMITS
// ═══════════════════════════════════════════════════════════════════════════════

namespace limits {
constexpr uint8_t SEND_RETRIES = 3;           ///< Max TX retry attempts
constexpr uint8_t SEND_PACKETS = 2;           ///< Packets sent per command
constexpr uint8_t MAX_COMMAND_QUEUE = 10;     ///< Max commands per blind
constexpr uint8_t COUNTER_MAX = 255;          ///< Counter wrap-around value
}  // namespace limits

// ═══════════════════════════════════════════════════════════════════════════════
// ENCRYPTION CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════════

namespace crypto {
constexpr uint16_t MULT = 0x708f;             ///< Encryption multiplier
constexpr uint16_t MASK = 0xffff;             ///< 16-bit mask
constexpr uint8_t R20_INITIAL = 0xFE;         ///< Initial R20 value for encoding
constexpr uint8_t R20_SECOND = 0xBA;          ///< Second R20 value (after first 2 bytes)
constexpr uint8_t R20_DECREMENT = 0x22;       ///< R20 decrement per byte
}  // namespace crypto

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
  return type == msg_type::COMMAND || type == msg_type::COMMAND_ALT;
}

/// Check if packet type is a status packet.
/// @param type Message type byte
/// @return true if status packet (0xca or 0xc9)
inline bool is_status_packet(uint8_t type) {
  return type == msg_type::STATUS || type == msg_type::STATUS_ALT;
}

/// Check if packet type is a button packet.
/// @param type Message type byte
/// @return true if button packet (0x44)
inline bool is_button_packet(uint8_t type) {
  return type == msg_type::BUTTON;
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

// ═══════════════════════════════════════════════════════════════════════════════
// TX PACKET BUILDING
// ═══════════════════════════════════════════════════════════════════════════════

/// TX building constants
constexpr uint8_t TX_MSG_LENGTH = 0x1d;                  ///< Fixed message length (29 bytes)
constexpr uint16_t TX_CRYPTO_MULT = crypto::MULT;        ///< Encryption multiplier
constexpr uint16_t TX_CRYPTO_MASK = crypto::MASK;        ///< 16-bit mask
constexpr uint8_t TX_SYS_ADDR = defaults::SYS_ADDR;      ///< System address byte
constexpr uint8_t TX_DEST_COUNT = defaults::DEST_COUNT;  ///< Single destination

/// Packet header offsets (shared by TX and RX — bytes 0-16 are identical)
namespace pkt_offset {
constexpr size_t LENGTH = 0;        // Packet length byte (value excludes this byte)
constexpr size_t COUNTER = 1;       // Rolling counter
constexpr size_t TYPE = 2;          // Message type (0x6a=cmd, 0xca=status, etc.)
constexpr size_t TYPE2 = 3;         // Secondary type byte
constexpr size_t HOP = 4;           // Hop count
constexpr size_t SYS = 5;           // System address (always 0x01)
constexpr size_t CHANNEL = 6;       // RF channel
constexpr size_t SRC_ADDR = 7;      // Source address (3 bytes, big-endian)
constexpr size_t BWD_ADDR = 10;     // Backward address (3 bytes)
constexpr size_t FWD_ADDR = 13;     // Forward address (3 bytes)
constexpr size_t NUM_DESTS = 16;    // Number of destinations
constexpr size_t FIRST_DEST = 17;   // Start of destination address(es)
}  // namespace pkt_offset

/// TX-specific offsets (fixed layout: always 1 destination, 3-byte addressing)
/// For RX, destination count varies so offsets 17+ are computed dynamically.
namespace tx_offset {
// Header aliases (re-exported for convenience in TX building code)
constexpr size_t LENGTH = pkt_offset::LENGTH;
constexpr size_t COUNTER = pkt_offset::COUNTER;
constexpr size_t TYPE = pkt_offset::TYPE;
constexpr size_t TYPE2 = pkt_offset::TYPE2;
constexpr size_t HOP = pkt_offset::HOP;
constexpr size_t SYS = pkt_offset::SYS;
constexpr size_t CHANNEL = pkt_offset::CHANNEL;
constexpr size_t SRC_ADDR = pkt_offset::SRC_ADDR;
constexpr size_t BWD_ADDR = pkt_offset::BWD_ADDR;
constexpr size_t FWD_ADDR = pkt_offset::FWD_ADDR;
constexpr size_t NUM_DESTS = pkt_offset::NUM_DESTS;
constexpr size_t DST_ADDR = pkt_offset::FIRST_DEST;
// Fixed offsets for single-destination TX
constexpr size_t PAYLOAD = 20;      // Payload start (payload_1, payload_2)
constexpr size_t CRYPTO_CODE = 22;  // Start of 8-byte encrypted section
}  // namespace tx_offset

/// Address size in bytes (big-endian 24-bit addresses)
constexpr size_t ADDR_SIZE = 3;

/// CC1101 appends 2 bytes after packet data: RSSI (1 byte) + LQI with CRC bit (1 byte)
constexpr uint8_t CC1101_APPEND_SIZE = 2;

/// Total overhead beyond length field value for buffer validation:
/// 1 (length byte itself) + 2 (RSSI/LQI appended by CC1101) = 3
constexpr uint8_t PACKET_TOTAL_OVERHEAD = 3;

/// Decrypted payload offsets (after msg_decode)
/// These offsets index into the 8-byte encrypted section (starting at absolute offset 22).
/// Layout: [crypto_hi, crypto_lo, command, cmd2, 0, 0, state, parity]
/// Same layout for TX and RX — verified against eleropy, andyboeh, and pfriedrich84.
namespace payload_offset {
constexpr size_t CRYPTO_HIGH = 0;   // Crypto code high byte
constexpr size_t CRYPTO_LOW = 1;    // Crypto code low byte
constexpr size_t COMMAND = 2;       // Command byte (for 0x6A/0x69 packets)
constexpr size_t COMMAND2 = 3;      // Secondary command byte
constexpr size_t STATE = 6;         // State byte (for 0xCA/0xC9 packets, from blinds)
constexpr size_t PARITY = 7;        // Parity byte
}  // namespace payload_offset

/// Parameters for building a TX packet.
struct TxParams {
  uint8_t counter{1};                       ///< Rolling message counter
  uint32_t dst_addr{0};                     ///< Destination address (blind)
  uint32_t src_addr{0};                     ///< Source address (emulated remote)
  uint8_t channel{0};                       ///< RF channel
  uint8_t type{msg_type::COMMAND};          ///< Message type
  uint8_t type2{defaults::TYPE2};           ///< Secondary type byte
  uint8_t hop{defaults::HOP};               ///< Hop count
  uint8_t command{command::CHECK};          ///< Command byte
  uint8_t payload_1{defaults::PAYLOAD_1};   ///< Payload byte at offset 20
  uint8_t payload_2{defaults::PAYLOAD_2};   ///< Payload byte at offset 21
};

/// Write a 24-bit address in big-endian format.
/// @param buf Destination buffer (3 bytes)
/// @param addr 24-bit address value
inline void write_addr(uint8_t* buf, uint32_t addr) {
  buf[0] = (addr >> 16) & 0xFF;
  buf[1] = (addr >> 8) & 0xFF;
  buf[2] = addr & 0xFF;
}

/// Calculate the rolling code XOR bytes from the counter.
/// @param counter Rolling message counter
/// @return 16-bit code (high byte first)
inline uint16_t calc_crypto_code(uint8_t counter) {
  return (0x0000 - (static_cast<uint32_t>(counter) * TX_CRYPTO_MULT)) & TX_CRYPTO_MASK;
}

/// Build a TX packet from command parameters.
///
/// @param params Command parameters
/// @param out_buf Output buffer (must be at least 30 bytes)
/// @return Packet length (always TX_MSG_LENGTH + 1 = 30)
size_t build_tx_packet(const TxParams& params, uint8_t* out_buf);

// ─── Cover State Mapping ────────────────────────────────────────────────────

/// Cover operation states (matches ESPHome CoverOperation enum)
enum class CoverOp : uint8_t {
  IDLE = 0,
  OPENING = 1,
  CLOSING = 2,
};

/// Result of mapping an Elero state byte to cover state.
struct CoverStateResult {
  float position;           ///< Cover position (0.0=closed, 1.0=open, -1.0=unchanged)
  float tilt;               ///< Tilt position (0.0=closed, 1.0=tilted)
  CoverOp operation;        ///< Cover operation state
  bool is_warning;          ///< True if state indicates a warning condition
  const char* warning_msg;  ///< Warning message (nullptr if no warning)
};

/// Map an Elero state byte to cover position/operation.
///
/// This is a pure function that extracts the state mapping logic from
/// EleroCover::set_rx_state() for unit testing.
///
/// @param elero_state State byte from status packet (payload[6])
/// @return CoverStateResult with position, tilt, and operation
CoverStateResult map_cover_state(uint8_t elero_state);

}  // namespace esphome::elero::packet
