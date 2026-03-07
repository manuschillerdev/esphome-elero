/// @file cover_core.h
/// @brief Pure C++ cover logic — position tracking, state mapping, polling decisions.
///
/// No ESPHome dependencies. Testable with GoogleTest on host.
/// Composed by EleroCover (YAML), EleroDynamicCover (MQTT), and NativeNvsCover (native+NVS).

#pragma once

#include <cstdint>
#include <algorithm>
#include "elero_packet.h"

namespace esphome {
namespace elero {

struct NvsDeviceConfig;  // Forward declaration

/// Cover position/target sentinel values.
namespace cover_pos {
inline constexpr float UNKNOWN = 0.5f;         ///< Default position when unknown (mid-point)
inline constexpr float FULLY_OPEN = 1.0f;      ///< Fully open position
inline constexpr float FULLY_CLOSED = 0.0f;    ///< Fully closed position
}  // namespace cover_pos

/// Cover operation string constants (used in MQTT state, web API, logs).
namespace cover_op_str {
inline constexpr const char *OPENING = "opening";
inline constexpr const char *CLOSING = "closing";
inline constexpr const char *OPEN = "open";
inline constexpr const char *CLOSED = "closed";
inline constexpr const char *STOPPED = "stopped";
}  // namespace cover_op_str

class CoverCore {
 public:
  struct Config {
    uint32_t open_duration_ms{0};
    uint32_t close_duration_ms{0};
    uint32_t poll_interval_ms{0};
    uint32_t poll_offset{0};
    bool supports_tilt{false};
  };

  enum class Operation : uint8_t {
    IDLE = 0,
    OPENING = 1,
    CLOSING = 2,
  };

  /// Result of processing a received state byte.
  struct RxStateResult {
    bool changed;             ///< True if state changed (caller should publish)
    bool is_warning;          ///< True if state indicates a warning condition
    const char *warning_msg;  ///< Warning message (nullptr if no warning)
  };

  /// Process a received state byte from an RF status packet.
  /// Handles state mapping, position/tilt update, movement tracking
  /// (sets movement_start_ms/movement_start_pos on operation change).
  RxStateResult on_rx_state(uint8_t state, uint32_t now);

  /// Recompute position based on elapsed movement time.
  /// Call from loop() when moving and durations are configured.
  void recompute_position(uint32_t now);

  /// Check if movement has timed out (2 min).
  /// Returns true if timeout occurred and operation was reset to IDLE.
  bool check_movement_timeout(uint32_t now);

  /// Determine effective poll interval (fast while moving).
  uint32_t effective_poll_interval() const;

  /// Check if it's time to poll.
  bool should_poll(uint32_t now) const;

  /// Record that a poll was sent.
  void mark_polled(uint32_t now);

  /// Check if cover has reached its target position (for intermediate stops).
  /// Returns false for fully open/closed targets (blind handles those).
  bool is_at_target() const;

  /// Start a movement. Sets operation, records start time/position.
  void start_movement(Operation op, uint32_t now);

  /// Sync core config from an NvsDeviceConfig (covers timing + tilt).
  void sync_from_nvs_config(const NvsDeviceConfig &cfg);

  /// Apply runtime settings (non-zero values only). Updates core config.
  void apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms);

  /// Reset state to defaults (mid-position, idle).
  void reset() {
    position = cover_pos::UNKNOWN;
    tilt = 0.0f;
    operation = Operation::IDLE;
    target_position = cover_pos::FULLY_OPEN;
    movement_start_ms = 0;
    movement_start_pos = 0.0f;
  }

  /// Whether position tracking is enabled (both durations > 0).
  bool has_position_tracking() const {
    return config.open_duration_ms > 0 && config.close_duration_ms > 0;
  }

  /// Human-readable operation state, position-aware when idle.
  /// Returns "opening", "closing", "open", "closed", or "stopped".
  const char *operation_str() const {
    switch (operation) {
      case Operation::OPENING: return cover_op_str::OPENING;
      case Operation::CLOSING: return cover_op_str::CLOSING;
      default:
        if (position >= cover_pos::FULLY_OPEN) return cover_op_str::OPEN;
        if (position <= cover_pos::FULLY_CLOSED) return cover_op_str::CLOSED;
        return cover_op_str::STOPPED;
    }
  }

  // ─── State (read/write by adapters) ───

  float position{cover_pos::UNKNOWN};
  float tilt{0.0f};
  Operation operation{Operation::IDLE};
  Operation last_direction{Operation::OPENING};
  float target_position{cover_pos::FULLY_OPEN};
  Config config;

  // Movement tracking (public for adapter access)
  uint32_t movement_start_ms{0};
  float movement_start_pos{0.0f};

  // Polling tracking
  uint32_t last_poll_ms{0};
  bool immediate_poll{false};
};

}  // namespace elero
}  // namespace esphome
