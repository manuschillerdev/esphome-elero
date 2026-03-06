/// @file elero_strings.h
/// @brief String conversion functions for Elero protocol constants.
///
/// Extracted from elero.cpp so they can be unit tested without ESPHome dependencies.

#pragma once

#include <cstdint>

namespace esphome::elero {

const char *elero_state_to_string(uint8_t state);
const char *elero_command_to_string(uint8_t command);

/// Convert action string ("up", "down", "stop", etc.) to command byte.
/// Returns 0xFF if action is not recognized.
uint8_t elero_action_to_command(const char *action);

}  // namespace esphome::elero
