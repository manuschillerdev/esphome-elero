/// @file light_core.h
/// @brief Pure C++ light logic — brightness tracking, dimming, state mapping.
///
/// No ESPHome dependencies. Testable with GoogleTest on host.
/// Composed by EleroLight (YAML), EleroDynamicLight (MQTT), and NativeNvsLight (native+NVS).

#pragma once

#include <cstdint>
#include <algorithm>
#include "elero_packet.h"

namespace esphome {
namespace elero {

struct NvsDeviceConfig;  // Forward declaration

/// Direction of dimming operation.
enum class DimDirection : uint8_t {
  NONE,
  UP,
  DOWN,
};

/// Light operation string constants (used in MQTT state, web API, logs).
namespace light_op_str {
inline constexpr const char *DIMMING_UP = "dimming_up";
inline constexpr const char *DIMMING_DOWN = "dimming_down";
inline constexpr const char *IDLE = "idle";
}  // namespace light_op_str

/// Brightness comparison tolerance — two brightness values within this range
/// are considered equal (avoids floating-point jitter causing unnecessary dim commands).
constexpr float BRIGHTNESS_EPSILON = 0.01f;

class LightCore {
 public:
  struct Config {
    uint32_t dim_duration_ms{0};  ///< 0 = on/off only, >0 = brightness control
  };

  /// Result of computing what write action to take.
  struct WriteAction {
    uint8_t command{0};         ///< Command byte to send (0 = no action)
    bool start_dimming{false};  ///< True if dimming should start
    DimDirection dim_dir{DimDirection::NONE};
  };

  /// Process a received state byte from an RF status packet.
  /// Returns true if state changed (caller should publish).
  bool on_rx_state(uint8_t state);

  /// Recompute brightness based on elapsed dimming time.
  void recompute_brightness(uint32_t now);

  /// Check if dimming has reached the target brightness.
  /// Returns true if target reached (caller should send STOP).
  bool is_at_target() const;

  /// Compute write action for a requested on/off + brightness.
  /// Pure decision function — does NOT modify state. Caller applies results.
  WriteAction compute_write_action(bool target_on, float target_brightness) const;

  /// Apply the result of compute_write_action to internal state.
  void apply_write_action(bool target_on, float target_brightness, const WriteAction &action, uint32_t now);

  /// Turn off the light (resets brightness and dimming state).
  void turn_off();

  /// Whether brightness control is supported.
  bool supports_brightness() const { return config.dim_duration_ms > 0; }

  /// Sync core config from an NvsDeviceConfig (dim duration).
  void sync_from_nvs_config(const NvsDeviceConfig &cfg);

  /// Human-readable operation state for dimming.
  const char *operation_str() const {
    if (dim_direction == DimDirection::UP) return light_op_str::DIMMING_UP;
    if (dim_direction == DimDirection::DOWN) return light_op_str::DIMMING_DOWN;
    return light_op_str::IDLE;
  }

  /// Reset all state to defaults (off, no brightness, no dimming).
  void reset() {
    is_on = false;
    brightness = 0.0f;
    dim_direction = DimDirection::NONE;
    target_brightness = 0.0f;
    dimming_start = 0;
    last_recompute_time = 0;
  }

  // ─── State (read/write by adapters) ───

  bool is_on{false};
  float brightness{0.0f};
  float target_brightness{0.0f};
  DimDirection dim_direction{DimDirection::NONE};
  Config config;

  // Dimming tracking
  uint32_t dimming_start{0};
  uint32_t last_recompute_time{0};

  // Configurable command bytes
  uint8_t command_on{packet::command::UP};
  uint8_t command_off{packet::command::DOWN};
  uint8_t command_dim_up{packet::command::UP};
  uint8_t command_dim_down{packet::command::DOWN};
  uint8_t command_stop{packet::command::STOP};
};

}  // namespace elero
}  // namespace esphome
