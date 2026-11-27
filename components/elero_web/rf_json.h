#pragma once

#include <span>

#include "../elero/elero.h"
#include "../elero/elero_strings.h"
#include "web_json.h"

namespace esphome::elero {

inline std::string format_rf_bytes(std::span<const uint8_t> bytes) {
  constexpr char ELERO_HEX_DIGITS[] = "0123456789abcdef";
  std::string text;
  text.reserve(bytes.size() * 3);
  for (uint8_t byte : bytes) {
    if (!text.empty()) text += ' ';
    text += ELERO_HEX_DIGITS[byte >> 4];
    text += ELERO_HEX_DIGITS[byte & 0x0f];
  }
  return text;
}

inline std::string build_rf_json(const RfPacketInfo &pkt) {
  const std::span<const uint8_t> raw(pkt.raw, pkt.raw_len);
  return build_web_json([&](JsonObject root) {
    root["t"] = pkt.timestamp_ms;
    root["src"] = hex_str(pkt.src);
    root["channel"] = pkt.channel;
    root["type"] = hex_str8(pkt.type);
    root["type2"] = hex_str8(pkt.type2);
    root["cnt"] = pkt.cnt;
    root["rssi"] = round_rssi(pkt.rssi);
    root["hop"] = hex_str8(pkt.hop);
    root["raw"] = format_rf_bytes(raw);
    root["decoded"] = !pkt.raw_only;
    if (pkt.raw_only) return;

    root["dst"] = hex_str(pkt.dst);
    root["command"] = hex_str8(pkt.command);
    root["state"] = hex_str8(pkt.state);
    if (packet::is_status_packet(pkt.type)) root["state_name"] = elero_state_to_string(pkt.state);
    auto destinations = root["destinations"].to<JsonArray>();
    for (uint8_t i = 0; i < pkt.num_address_dests; ++i)
      destinations.add(hex_str(pkt.dst_addrs[i]));

    const size_t first = packet::pkt_offset::FIRST_DEST;
    const size_t prefix = first + pkt.destination_bytes;
    auto selectors = root["selectors"].to<JsonArray>();
    if (packet::uses_one_byte_selector_envelope(pkt.type)) {
      for (uint8_t selector : raw.subspan(first, pkt.destination_bytes)) selectors.add(selector);
    }
    if (pkt.type == packet::msg_type::BUTTON_GROUP) {
      root["bwd"] = hex_str(pkt.backward_address());
      root["fwd"] = hex_str(pkt.forward_address());
      root["encoded_bytes"] = format_rf_bytes({pkt.encoded_block(), packet::COMMAND_BLOCK_SIZE});
    }
    root["prefix"] = format_rf_bytes(raw.subspan(prefix, packet::PREFIX_SIZE));
    root["decoded_bytes"] = format_rf_bytes(pkt.payload);
    root["trailing"] = format_rf_bytes(raw.subspan(
        prefix + packet::PREFIX_SIZE + packet::COMMAND_BLOCK_SIZE, pkt.trailing_length));
  });
}

}  // namespace esphome::elero
