/// @file elero_packet.cpp
/// @brief Implementation of packet parsing functions.

#include "elero_packet.h"
#include <algorithm>
#include <cstring>
#include <iterator>

namespace esphome::elero {

bool EleroCommand::set_frame_data(std::span<const uint8_t> destination_bytes,
                                  std::span<const uint8_t> trailing_bytes) {
  const size_t width = packet::uses_one_byte_selector_envelope(type) ? 1 : packet::ADDR_SIZE;
  const size_t dest_size = destination_bytes.empty() ? width : destination_bytes.size();
  if (dest_size > packet::MAX_VARIABLE_BYTES || dest_size % width != 0 ||
      trailing_bytes.size() > packet::MAX_VARIABLE_BYTES - dest_size) {
    return false;
  }

  num_dests = static_cast<uint8_t>(destination_bytes.size() / width);
  trailing_length = static_cast<uint8_t>(trailing_bytes.size());
  uint8_t bytes[sizeof(frame_data)]{};
  std::copy(destination_bytes.begin(), destination_bytes.end(), bytes);
  std::copy(trailing_bytes.begin(), trailing_bytes.end(), std::end(bytes) - trailing_length);
  memcpy(frame_data, bytes, sizeof(frame_data));
  return true;
}

}  // namespace esphome::elero

namespace esphome::elero::packet {

// All radio backends use this layout. Only the command block is encoded.
size_t build_command_packet(const EleroCommand &cmd, uint8_t *out) {
  const bool selector = uses_one_byte_selector_envelope(cmd.type);
  const size_t width = selector ? 1 : ADDR_SIZE;
  const uint8_t count = cmd.num_dests == 0 ? 1 : cmd.num_dests;
  const size_t dest_size = count * width;
  const size_t prefix = pkt_offset::FIRST_DEST + dest_size;
  const size_t block = prefix + PREFIX_SIZE;
  const size_t tail = block + COMMAND_BLOCK_SIZE;
  const size_t body_size = tail - 1 + cmd.trailing_length;
  if (body_size > MAX_PACKET_SIZE) return 0;

  out[pkt_offset::LENGTH] = static_cast<uint8_t>(body_size);
  out[pkt_offset::COUNTER] = cmd.counter;
  out[pkt_offset::TYPE] = cmd.type;
  out[pkt_offset::TYPE2] = cmd.type2;
  out[pkt_offset::HOP] = cmd.hop;
  out[pkt_offset::SYS] = TX_SYS_ADDR;
  const bool group_button = cmd.type == msg_type::BUTTON && count > 1;
  out[pkt_offset::CHANNEL] = group_button ? button::GROUP_CHANNEL : cmd.channel;
  write_addr(&out[pkt_offset::SRC_ADDR], cmd.src_addr);
  write_addr(&out[pkt_offset::BWD_ADDR], cmd.src_addr);
  write_addr(&out[pkt_offset::FWD_ADDR], cmd.src_addr);
  out[pkt_offset::NUM_DESTS] = count;
  if (cmd.num_dests != 0) {
    memcpy(&out[pkt_offset::FIRST_DEST], cmd.frame_data, dest_size);
  } else if (selector) {
    out[pkt_offset::FIRST_DEST] = cmd.channel;
  } else {
    write_addr(&out[pkt_offset::FIRST_DEST], cmd.dst_addr);
  }

  // The normal button and programming paths have fixed prefix values.
  if (cmd.type == msg_type::BUTTON || cmd.type == msg_type::PROGRAM) {
    out[prefix] = 0;
    out[prefix + 1] = group_button ? defaults::PAYLOAD_2 : 0x03;
  } else {
    out[prefix] = cmd.payload[0];
    out[prefix + 1] = cmd.payload[1];
  }
  auto *encoded = &out[block];
  memset(encoded, 0, COMMAND_BLOCK_SIZE);
  const uint16_t code = calc_crypto_code(cmd.counter);
  encoded[payload_offset::CRYPTO_HIGH] = code >> 8;
  encoded[payload_offset::CRYPTO_LOW] = code & 0xff;
  encoded[payload_offset::COMMAND] = cmd.payload[4];
  protocol::msg_encode(encoded);
  memcpy(out + tail, std::end(cmd.frame_data) - cmd.trailing_length, cmd.trailing_length);
  return body_size + 1;
}

ParseResult parse_packet(const uint8_t* raw, size_t raw_len) {
  ParseResult r;

  // Basic sanity checks
  if (raw == nullptr || raw_len < MIN_PACKET_SIZE) {
    r.reject_reason = "too_short";
    return r;
  }

  r.length = raw[pkt_offset::LENGTH];

  // Check packet length against maximum
  if (r.length > MAX_PACKET_SIZE) {
    r.reject_reason = "too_long";
    return r;
  }

  // Check we have enough data for header (need at least FIRST_DEST bytes for basic header)
  if (raw_len < pkt_offset::FIRST_DEST) {
    r.reject_reason = "truncated_header";
    return r;
  }

  // Extract header fields
  r.counter = raw[pkt_offset::COUNTER];
  r.type = raw[pkt_offset::TYPE];
  r.type2 = raw[pkt_offset::TYPE2];
  r.hop = raw[pkt_offset::HOP];
  r.syst = raw[pkt_offset::SYS];
  r.channel = raw[pkt_offset::CHANNEL];

  // Extract addresses
  r.src_addr = extract_addr(&raw[pkt_offset::SRC_ADDR]);
  r.bwd_addr = extract_addr(&raw[pkt_offset::BWD_ADDR]);
  r.fwd_addr = extract_addr(&raw[pkt_offset::FWD_ADDR]);

  // Destination count
  r.num_dests = raw[pkt_offset::NUM_DESTS];

  // Validate destination count
  if (r.num_dests > GROUP_MAX_DESTS) {
    r.reject_reason = "too_many_dests";
    return r;
  }

  // Calculate destination length from the explicit packet envelope shape.
  if (uses_three_byte_address_envelope(r.type)) {
    if (r.num_dests > ELERO_MAX_ADDRESS_DESTS) {
      r.reject_reason = "dests_len_too_long";
      return r;
    }
    r.dests_len = r.num_dests * ADDR_SIZE;
  } else if (uses_one_byte_selector_envelope(r.type)) {
    r.dests_len = r.num_dests;
  } else {
    r.reject_reason = "unsupported_type";
    return r;
  }

  const size_t block = pkt_offset::FIRST_DEST + r.dests_len + PREFIX_SIZE;
  const size_t tail = block + COMMAND_BLOCK_SIZE;
  const size_t frame_end = static_cast<size_t>(r.length) + 1;
  if (tail > frame_end) {
    r.reject_reason = "dests_len_too_long";
    return r;
  }
  if (frame_end > raw_len) {
    r.reject_reason = "truncated_payload";
    return r;
  }
  if (frame_end + CC1101_APPEND_SIZE > raw_len) {
    r.reject_reason = "rssi_oob";
    return r;
  }

  if (uses_three_byte_address_envelope(r.type)) {
    for (uint8_t i = 0; i < r.num_dests; ++i) {
      r.dst_addrs[i] = extract_addr(&raw[pkt_offset::FIRST_DEST + i * ADDR_SIZE]);
    }
    r.dst_addr = r.dst_addrs[0];
  } else if (r.num_dests != 0) {
    r.dst_addr = raw[pkt_offset::FIRST_DEST];
  }

  r.rssi_raw = raw[r.length + 1];  // +1 because length byte is at [0]
  r.rssi = calc_rssi(r.rssi_raw);
  r.lqi = raw[r.length + CC1101_APPEND_SIZE] & cc1101_status::LQI_MASK;
  r.crc_ok = (raw[r.length + CC1101_APPEND_SIZE] & cc1101_status::CRC_OK_BIT) ? 1 : 0;

  memcpy(r.payload, raw + block, sizeof(r.payload));
  protocol::msg_decode(r.payload);
  r.trailing.length = static_cast<uint8_t>(frame_end - tail);
  memcpy(r.trailing.bytes, raw + tail, r.trailing.length);

  // Extract command/state based on packet type
  if (carries_command(r.type)) {
    r.command = r.payload[payload_offset::COMMAND];
  } else if (is_status_packet(r.type)) {
    r.state = r.payload[payload_offset::STATE];
  }

  r.valid = true;
  return r;
}

size_t build_tx_packet(const TxParams& params, uint8_t* out_buf) {
  EleroCommand cmd{};
  cmd.counter = params.counter;
  cmd.src_addr = params.src_addr;
  cmd.dst_addr = params.dst_addr;
  cmd.channel = params.channel;
  cmd.type = params.type;
  cmd.type2 = params.type2;
  cmd.hop = params.hop;
  cmd.payload[0] = params.payload_1;
  cmd.payload[1] = params.payload_2;
  cmd.payload[4] = params.command;
  return build_command_packet(cmd, out_buf);
}

size_t build_button_packet(const ButtonTxParams& params, uint8_t* out_buf) {
  return build_tx_packet({.counter = params.counter, .src_addr = params.src_addr,
                          .channel = params.channel, .type = msg_type::BUTTON,
                          .type2 = params.type2, .hop = params.hop, .command = params.command}, out_buf);
}

size_t build_program_packet(const ProgramTxParams& params, uint8_t* out_buf) {
  return build_tx_packet({.counter = params.counter, .src_addr = params.src_addr,
                          .channel = params.channel, .type = msg_type::PROGRAM,
                          .type2 = params.type2, .hop = params.hop, .command = params.command}, out_buf);
}

size_t build_group_button_packet(const GroupButtonTxParams& params, uint8_t* out_buf) {
  if (params.num_dests < 2 || params.num_dests > GROUP_MAX_DESTS || params.dest_channels == nullptr) return 0;
  EleroCommand cmd{};
  cmd.counter = params.counter;
  cmd.src_addr = params.src_addr;
  cmd.type = msg_type::BUTTON;
  cmd.type2 = params.type2;
  cmd.hop = params.hop;
  cmd.payload[4] = params.command;
  cmd.num_dests = params.num_dests;
  memcpy(cmd.frame_data, params.dest_channels, cmd.num_dests);
  return build_command_packet(cmd, out_buf);
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
      r.tilt = -1.0f;  // This endpoint report gives no tilt information.
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
