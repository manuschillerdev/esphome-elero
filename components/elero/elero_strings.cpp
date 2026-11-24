/// @file elero_strings.cpp
/// @brief String conversion functions for Elero protocol constants.

#include "elero_strings.h"
#include "elero_status_names.h"
#include "elero_command_names.h"
#include <cstring>

namespace esphome::elero {

const char *elero_state_to_string(uint8_t state) {
  return state < ELERO_STATUS_NAMES.size() ? ELERO_STATUS_NAMES[state] : ELERO_STATUS_NAMES[packet::state::UNKNOWN];
}

const char *elero_command_to_string(uint8_t command) {
  return decode_command_names(packet::msg_type::COMMAND, command).legacy;
}

const char *elero_rf_command_name(uint8_t type, uint8_t key) {
  return decode_command_names(type, key).name;
}

uint8_t elero_action_to_command(const char *action) {
  if (action == nullptr)
    return packet::command::INVALID;
  if (strcmp(action, action::FULL_UP) == 0) return packet::command::FULL_UP;
  if (strcmp(action, action::FULL_DOWN) == 0) return packet::command::FULL_DOWN;
  if (strcmp(action, action::UP) == 0 || strcmp(action, action::OPEN) == 0)
    return packet::command::UP;
  if (strcmp(action, action::DOWN) == 0 || strcmp(action, action::CLOSE) == 0)
    return packet::command::DOWN;
  if (strcmp(action, action::STOP) == 0)
    return packet::command::STOP;
  if (strcmp(action, action::CHECK) == 0)
    return packet::command::CHECK;
  if (strcmp(action, action::TILT) == 0)
    return packet::command::TILT;
  if (strcmp(action, action::INT) == 0)
    return packet::command::INTERMEDIATE;
  return packet::command::INVALID;
}

const char *elero_command_to_action(uint8_t cmd_byte) {
  switch (cmd_byte) {
    case packet::command::FULL_UP: return action::FULL_UP;
    case packet::command::FULL_DOWN: return action::FULL_DOWN;
    case packet::command::UP: return action::UP;
    case packet::command::DOWN: return action::DOWN;
    case packet::command::STOP: return action::STOP;
    case packet::command::CHECK: return action::CHECK;
    case packet::command::TILT: return action::TILT;
    case packet::command::INTERMEDIATE: return action::INT;
    default: return nullptr;
  }
}

}  // namespace esphome::elero
