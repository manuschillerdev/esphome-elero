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
  if (r.type > 0x60) {
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
  r.lqi = raw[r.length + 2] & 0x7f;
  r.crc_ok = raw[r.length + 2] >> 7;

  // Copy payload and decrypt
  // Note: We need a mutable copy for msg_decode
  uint8_t payload_buf[10];
  memcpy(payload_buf, &raw[payload_start], 10);
  protocol::msg_decode(payload_buf);
  memcpy(r.payload, payload_buf, 10);

  // Extract command/state based on packet type
  if (is_command_packet(r.type)) {
    r.command = r.payload[4];
  } else if (is_status_packet(r.type)) {
    r.state = r.payload[6];
  }

  r.valid = true;
  return r;
}

}  // namespace esphome::elero::packet
