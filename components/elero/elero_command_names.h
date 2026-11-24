// Generated from asyncapi.yaml by pnpm generate:types.
#pragma once
#include "elero_packet.h"

namespace esphome::elero {
struct CommandNames {
  const char *name;
  const char *legacy;
};
inline CommandNames decode_command_names(uint8_t type, uint8_t key) {
  if (!packet::carries_command(type)) return {nullptr, "UNKNOWN"};
  if (key == packet::command::CHECK && (type == packet::msg_type::COMMAND || type == packet::msg_type::COMMAND_ALT)) return {"check", "CHECK"};
  if (key == packet::button::RELEASE && (type == packet::msg_type::BUTTON)) return {"release", "RELEASE"};
  if (key == packet::command::STOP) return {"stop", "STOP"};
  if (key == packet::command::UP) return {"up", "UP"};
  if (key == packet::command::FULL_UP) return {"full_up", "FULL_UP"};
  if (key == packet::command::FULL_DOWN) return {"full_down", "FULL_DOWN"};
  if (key == packet::command::TILT) return {"tilt", "TILT"};
  if (key == packet::command::DOWN) return {"down", "DOWN"};
  if (key == packet::command::INTERMEDIATE) return {"intermediate", "INTERMEDIATE"};
  if (key == packet::command::PROGRAM) return {"program", "PROGRAM"};
  if (key == packet::command::PROGRAM_GROUP) return {"program_group", "PROGRAM_GROUP"};
  if (key == packet::command::PROGRAM_TARGET) return {"program_target", "PROGRAM_TARGET"};
  return {nullptr, "UNKNOWN"};
}
static_assert(packet::msg_type::BUTTON == 0x44);
static_assert(packet::msg_type::BUTTON_GROUP == 0x45);
static_assert(packet::msg_type::COMMAND == 0x6a);
static_assert(packet::msg_type::COMMAND_ALT == 0x69);
static_assert(packet::msg_type::PROGRAM == 0x70);
static_assert(packet::msg_type::STATUS == 0xca);
static_assert(packet::msg_type::STATUS_ALT == 0xc9);
static_assert(packet::command::CHECK == 0x00);
static_assert(packet::button::RELEASE == 0x00);
static_assert(packet::command::STOP == 0x10);
static_assert(packet::command::UP == 0x20);
static_assert(packet::command::FULL_UP == 0x21);
static_assert(packet::command::FULL_DOWN == 0x41);
static_assert(packet::command::TILT == 0x24);
static_assert(packet::command::DOWN == 0x40);
static_assert(packet::command::INTERMEDIATE == 0x44);
static_assert(packet::command::PROGRAM == 0x80);
static_assert(packet::command::PROGRAM_GROUP == 0x81);
static_assert(packet::command::PROGRAM_TARGET == 0x84);
}  // namespace esphome::elero
