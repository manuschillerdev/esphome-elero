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
  if (strcmp(action, "up") == 0 || strcmp(action, "open") == 0)
    return packet::command::UP;
  if (strcmp(action, "down") == 0 || strcmp(action, "close") == 0)
    return packet::command::DOWN;
  if (strcmp(action, "stop") == 0)
    return packet::command::STOP;
  if (strcmp(action, "check") == 0)
    return packet::command::CHECK;
  if (strcmp(action, "tilt") == 0)
    return packet::command::TILT;
  if (strcmp(action, "int") == 0)
    return packet::command::INTERMEDIATE;
  return packet::command::INVALID;
}

}  // namespace esphome::elero
