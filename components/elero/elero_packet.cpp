/// @file elero_packet.cpp
/// @brief Implementation of packet parsing functions.

#include "elero_packet.h"
#include <cstring>

namespace esphome::elero::packet {

ParseResult parse_packet(const uint8_t* raw, size_t raw_len) {
  ParseResult r;

  // Basic sanity checks
  if (raw == nullptr || raw_len < MIN_PACKET_SIZE) {
    r.reject_reason = "too_short";
    return r;
  }

  r.length = raw[0];

  // Check packet length against maximum
  if (r.length > MAX_PACKET_SIZE) {
    r.reject_reason = "too_long";
    return r;
  }

  // Check we have enough data for header (need at least 17 bytes for basic header)
  if (raw_len < 17) {
    r.reject_reason = "truncated_header";
    return r;
  }

  // Extract header fields
  r.counter = raw[1];
  r.type = raw[2];
  r.type2 = raw[3];
  r.hop = raw[4];
  r.syst = raw[5];
  r.channel = raw[6];

  // Extract addresses
  r.src_addr = extract_addr(&raw[7]);
  r.bwd_addr = extract_addr(&raw[10]);
  r.fwd_addr = extract_addr(&raw[13]);

  // Destination count
  r.num_dests = raw[16];

  // Validate destination count
  if (r.num_dests > MAX_DESTINATIONS) {
    r.reject_reason = "too_many_dests";
    return r;
  }

  // Calculate destination length based on message type
  if (r.type > msg_type::ADDR_3BYTE_THRESHOLD) {
    // 3-byte addresses (command/status packets)
    r.dests_len = r.num_dests * 3;
    if (raw_len >= 20) {
      r.dst_addr = extract_addr(&raw[17]);
    }
  } else {
    // 1-byte addresses (button packets)
    r.dests_len = r.num_dests;
    if (raw_len >= 18) {
      r.dst_addr = raw[17];
    }
  }

  // Sanity check: payload access bounds
  // msg_decode accesses 8 bytes at raw[19 + dests_len]
  // Highest index is 26 + dests_len
  if (26 + r.dests_len > r.length || 26 + r.dests_len >= FIFO_LENGTH) {
    r.reject_reason = "dests_len_too_long";
    return r;
  }

  // Check we have enough raw data for payload
  size_t payload_start = 19 + r.dests_len;
  size_t payload_end = payload_start + 10;  // 10-byte payload
  if (payload_end > raw_len) {
    r.reject_reason = "truncated_payload";
    return r;
  }

  // Check RSSI/LQI bounds (at length+1 and length+2)
  if (r.length + 2 >= raw_len) {
    r.reject_reason = "rssi_oob";
    return r;
  }

  // Extract RSSI and LQI (appended by CC1101 after packet data)
  r.rssi_raw = raw[r.length + 1];
  r.rssi = calc_rssi(r.rssi_raw);
  r.lqi = raw[r.length + 2] & cc1101_status::LQI_MASK;
  r.crc_ok = (raw[r.length + 2] & cc1101_status::CRC_OK_BIT) ? 1 : 0;

  // Copy payload and decrypt
  // Note: We need a mutable copy for msg_decode
  uint8_t payload_buf[10];
  memcpy(payload_buf, &raw[payload_start], 10);
  protocol::msg_decode(payload_buf);
  memcpy(r.payload, payload_buf, 10);

  // Extract command/state based on packet type
  if (is_command_packet(r.type)) {
    r.command = r.payload[payload_offset::COMMAND];
  } else if (is_status_packet(r.type)) {
    r.state = r.payload[payload_offset::STATE];
  }

  r.valid = true;
  return r;
}

size_t build_tx_packet(const TxParams& params, uint8_t* out_buf) {
  // Clear buffer
  memset(out_buf, 0, TX_MSG_LENGTH + 1);

  // Header fields
  out_buf[tx_offset::LENGTH] = TX_MSG_LENGTH;
  out_buf[tx_offset::COUNTER] = params.counter;
  out_buf[tx_offset::TYPE] = params.type;
  out_buf[tx_offset::TYPE2] = params.type2;
  out_buf[tx_offset::HOP] = params.hop;
  out_buf[tx_offset::SYS] = TX_SYS_ADDR;
  out_buf[tx_offset::CHANNEL] = params.channel;

  // Addresses (all same for direct command)
  write_addr(&out_buf[tx_offset::SRC_ADDR], params.src_addr);
  write_addr(&out_buf[tx_offset::BWD_ADDR], params.src_addr);
  write_addr(&out_buf[tx_offset::FWD_ADDR], params.src_addr);

  // Destination
  out_buf[tx_offset::NUM_DESTS] = TX_DEST_COUNT;
  write_addr(&out_buf[tx_offset::DST_ADDR], params.dst_addr);

  // Payload setup (bytes 20-21 are payload_1/2, bytes 22-29 get encrypted)
  out_buf[tx_offset::PAYLOAD] = params.payload_1;
  out_buf[tx_offset::PAYLOAD + 1] = params.payload_2;

  // Crypto code and command go into the 8-byte encrypted section (offset 22)
  // The section is: [crypto_hi, crypto_lo, command, 0, 0, 0, state, parity]
  // We must zero positions 25-29 as the old code did (via payload[5-9] = 0)
  uint16_t code = calc_crypto_code(params.counter);
  out_buf[tx_offset::CRYPTO_CODE] = (code >> 8) & 0xFF;
  out_buf[tx_offset::CRYPTO_CODE + 1] = code & 0xFF;
  out_buf[tx_offset::COMMAND] = params.command;
  out_buf[25] = 0;  // padding
  out_buf[26] = 0;  // padding (state position for RX, unused for TX)
  out_buf[27] = 0;  // padding
  out_buf[28] = 0;  // padding
  out_buf[29] = 0;  // parity (will be set by msg_encode)

  // Encrypt the 8-byte payload starting at offset 22
  protocol::msg_encode(&out_buf[tx_offset::CRYPTO_CODE]);

  return TX_MSG_LENGTH + 1;
}

CoverStateResult map_cover_state(uint8_t elero_state) {
  CoverStateResult r{};
  r.position = -1.0f;  // -1 means "unchanged"
  r.tilt = 0.0f;
  r.operation = CoverOp::IDLE;
  r.is_warning = false;
  r.warning_msg = nullptr;

  switch (elero_state) {
    case state::TOP:
      r.position = 1.0f;  // Fully open
      r.operation = CoverOp::IDLE;
      break;

    case state::BOTTOM:
      r.position = 0.0f;  // Fully closed
      r.operation = CoverOp::IDLE;
      break;

    case state::INTERMEDIATE:
      r.position = 0.5f;  // Intermediate (exact position unknown)
      r.operation = CoverOp::IDLE;
      break;

    case state::TILT:
      r.position = 0.0f;
      r.tilt = 1.0f;
      r.operation = CoverOp::IDLE;
      break;

    case state::TOP_TILT:
      r.position = 1.0f;
      r.tilt = 1.0f;
      r.operation = CoverOp::IDLE;
      break;

    case state::BOTTOM_TILT:
      r.position = 0.0f;
      r.tilt = 1.0f;
      r.operation = CoverOp::IDLE;
      break;

    case state::START_MOVING_UP:
    case state::MOVING_UP:
      r.operation = CoverOp::OPENING;
      break;

    case state::START_MOVING_DOWN:
    case state::MOVING_DOWN:
      r.operation = CoverOp::CLOSING;
      break;

    case state::STOPPED:
      r.operation = CoverOp::IDLE;
      break;

    case state::BLOCKING:
      r.is_warning = true;
      r.warning_msg = "Obstacle detected";
      r.operation = CoverOp::IDLE;
      break;

    case state::OVERHEATED:
      r.is_warning = true;
      r.warning_msg = "Motor overheated";
      r.operation = CoverOp::IDLE;
      break;

    case state::TIMEOUT:
      r.is_warning = true;
      r.warning_msg = "Communication timeout";
      r.operation = CoverOp::IDLE;
      break;

    case state::UNKNOWN:
    default:
      // Keep defaults (position unchanged, idle)
      break;
  }

  return r;
}

}  // namespace esphome::elero::packet
