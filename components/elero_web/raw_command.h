#pragma once

#include "../elero/elero_packet.h"
#include "esphome/components/json/json_util.h"
#include <cstdlib>
#include <limits>

namespace esphome::elero {

// Accept numeric fields and the hexadecimal strings used by WebSocket clients.
template<typename T> bool read_raw_field(JsonVariantConst value, T &out) {
  if (value.isNull()) return true;
  if (value.is<T>()) {
    out = value.as<T>();
    return true;
  }
  const char *str = value.as<const char *>();
  if (!str || *str < '0' || *str > '9') return false;
  char *end;
  const auto number = strtoul(str, &end, 0);
  if (*end || number > std::numeric_limits<T>::max()) return false;
  out = static_cast<T>(number);
  return true;
}

inline const char *parse_raw_command(JsonObjectConst root, EleroCommand &cmd) {
  cmd = {};
  cmd.type = packet::msg_type::COMMAND;
  if (!read_raw_field(root["msg_type"], cmd.type)) return "Invalid message type";
  const bool button = cmd.type == packet::msg_type::BUTTON;
  cmd.payload[0] = packet::defaults::PAYLOAD_1;
  cmd.payload[1] = button ? 3 : packet::defaults::PAYLOAD_2;
  cmd.type2 = button ? packet::button::TYPE2 : packet::defaults::TYPE2;
  cmd.hop = button ? packet::button::HOP : packet::defaults::HOP;
  if (!read_raw_field(root["dst_address"], cmd.dst_addr) ||
      !read_raw_field(root["src_address"], cmd.src_addr) ||
      !read_raw_field(root["channel"], cmd.channel) ||
      !read_raw_field(root["command"], cmd.payload[4]) ||
      !read_raw_field(root["payload_1"], cmd.payload[0]) ||
      !read_raw_field(root["payload_2"], cmd.payload[1]) ||
      !read_raw_field(root["type2"], cmd.type2) ||
      !read_raw_field(root["hop"], cmd.hop)) return "Invalid raw transmit field";
  return packet::raw_command_error(cmd);
}

}  // namespace esphome::elero
