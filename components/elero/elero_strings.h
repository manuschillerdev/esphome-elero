/// @file elero_strings.h
/// @brief String conversion functions for Elero protocol constants.
///
/// Extracted from elero.cpp so they can be unit tested without ESPHome dependencies.

#pragma once

#include <cstdint>
#include <string>
#include <cstdio>

namespace esphome::elero {

// ─── Action string constants (used in perform_action, MQTT, WebSocket) ───

namespace action {
inline constexpr const char *UP = "up";
inline constexpr const char *DOWN = "down";
inline constexpr const char *OPEN = "open";
inline constexpr const char *CLOSE = "close";
inline constexpr const char *STOP = "stop";
inline constexpr const char *CHECK = "check";
inline constexpr const char *TILT = "tilt";
inline constexpr const char *ON = "on";
inline constexpr const char *OFF = "off";
inline constexpr const char *DIM_UP = "dim_up";
inline constexpr const char *DIM_DOWN = "dim_down";
inline constexpr const char *INT = "int";
}  // namespace action

// ─── HA state strings ───

namespace ha_state {
inline constexpr const char *ON = "ON";
inline constexpr const char *OFF = "OFF";
}  // namespace ha_state

// ─── Shared display constants ───

inline constexpr const char *MANUFACTURER = "Elero";

/// Position/brightness scaling factor (0.0–1.0 ↔ 0–100)
inline constexpr float PERCENT_SCALE = 100.0f;

/// Round RSSI to 1 decimal place for display.
inline float round_rssi(float rssi) {
  return static_cast<float>(static_cast<int>(rssi * 10)) / 10.0f;
}

const char *elero_state_to_string(uint8_t state);
const char *elero_command_to_string(uint8_t command);

/// Convert action string ("up", "down", "stop", etc.) to command byte.
/// Returns 0xFF if action is not recognized.
uint8_t elero_action_to_command(const char *action);

/// Format a uint32_t as "0xNNNNNN" hex string (for JSON values).
inline std::string hex_str(uint32_t val) {
  char buf[12];
  snprintf(buf, sizeof(buf), "0x%06x", val);
  return buf;
}

/// Format a uint8_t as "0xNN" hex string (for JSON values).
inline std::string hex_str8(uint8_t val) {
  char buf[8];
  snprintf(buf, sizeof(buf), "0x%02x", val);
  return buf;
}

}  // namespace esphome::elero
