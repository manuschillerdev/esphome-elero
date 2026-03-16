/// @file elero_strings.cpp
/// @brief String conversion functions for Elero protocol constants.

#include "elero_strings.h"
#include "elero_packet.h"
#include <cstring>

namespace esphome::elero {

const char *elero_state_to_string(uint8_t state) {
  switch (state) {
    case packet::state::TOP:
      return "top";
    case packet::state::BOTTOM:
      return "bottom";
    case packet::state::INTERMEDIATE:
      return "intermediate";
    case packet::state::TILT:
      return "tilt";
    case packet::state::BLOCKING:
      return "blocking";
    case packet::state::OVERHEATED:
      return "overheated";
    case packet::state::TIMEOUT:
      return "timeout";
    case packet::state::START_MOVING_UP:
      return "start_moving_up";
    case packet::state::START_MOVING_DOWN:
      return "start_moving_down";
    case packet::state::MOVING_UP:
      return "moving_up";
    case packet::state::MOVING_DOWN:
      return "moving_down";
    case packet::state::STOPPED:
      return "stopped";
    case packet::state::TOP_TILT:
      return "top_tilt";
    case packet::state::BOTTOM_TILT:
      return "bottom_tilt";
    case packet::state::LIGHT_ON:
      return "on";
    default:
      return "unknown";
  }
}

const char *elero_command_to_string(uint8_t command) {
  switch (command) {
    case packet::command::CHECK:
      return "CHECK";
    case packet::command::STOP:
      return "STOP";
    case packet::command::UP:
      return "UP";
    case packet::command::TILT:
      return "TILT";
    case packet::command::DOWN:
      return "DOWN";
    case packet::command::INTERMEDIATE:
      return "INTERMEDIATE";
    default:
      return "UNKNOWN";
  }
}

uint8_t elero_action_to_command(const char *action) {
  if (action == nullptr)
    return packet::command::INVALID;
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
