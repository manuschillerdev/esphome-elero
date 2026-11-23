#pragma once

#include <cstdlib>
#include "esphome/components/json/json_util.h"
#include "../elero/elero_packet.h"

namespace esphome::elero {

/// Parse hex string ("0xNN" or decimal) to uint8_t, returning default if missing/null
inline uint8_t parse_hex_or(JsonObject root, const char *key, uint8_t def) {
  if (!root[key]) return def;
  if (root[key].is<const char *>()) {
    return (uint8_t) strtoul(root[key].as<const char *>(), nullptr, 0);
  }
  return root[key].as<uint8_t>();
}

/// Parse hex string ("0xNNNNNN" or decimal) to uint32_t, returning 0 if missing
inline uint32_t parse_hex32(JsonObject root, const char *key) {
  if (!root[key]) return 0;
  if (root[key].is<const char *>()) {
    return (uint32_t) strtoul(root[key].as<const char *>(), nullptr, 0);
  }
  return root[key].as<uint32_t>();
}

// A raw request supplies its own frame fields, including for saved destinations.
template<typename Hub> const char *submit_raw_tx(JsonObject root, Hub &hub) {
  const uint32_t dst = parse_hex32(root, "dst_address");
  const uint32_t src = parse_hex32(root, "src_address");
  const auto type = parse_hex_or(root, "msg_type", packet::msg_type::COMMAND);
  if (!src || (type != packet::msg_type::BUTTON && !dst)) return "Raw TX missing required address";

  const bool queued = hub.send_raw_command(dst, src,
      parse_hex_or(root, "channel", 0), parse_hex_or(root, "command", 0),
      parse_hex_or(root, "payload_1", packet::defaults::PAYLOAD_1),
      parse_hex_or(root, "payload_2", packet::default_payload_2(type)), type,
      parse_hex_or(root, "type2", packet::defaults::TYPE2),
      parse_hex_or(root, "hop", packet::defaults::HOP));
  return queued ? nullptr : "Raw TX queue rejected the request";
}

}  // namespace esphome::elero
