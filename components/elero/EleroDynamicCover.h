#pragma once

/// @file EleroDynamicCover.h
/// @brief Dynamic cover slot that can be activated at runtime via NVS config.
///
/// Unlike EleroCover which is configured at compile time via YAML and uses
/// ESPHome's native API, EleroDynamicCover:
/// - Is pre-allocated at compile time (dormant until activated)
/// - Activated at runtime with NvsDeviceConfig
/// - Does NOT register with ESPHome's native cover API
/// - Uses MQTT discovery for Home Assistant integration
///
/// This enables add/remove of covers without reflashing.

#include "esphome/core/component.h"
#include "elero.h"
#include "command_sender.h"
#include "nvs_config.h"

namespace esphome::elero {

/// Callback type for state changes (used by MQTT bridge)
using DynamicCoverStateCallback = std::function<void(uint32_t addr, const char* state, float position)>;

/// Dynamic cover slot that can be activated at runtime.
///
/// This class implements the cover logic without ESPHome's native Cover base class,
/// enabling MQTT-only operation. The MQTT bridge subscribes to state changes and
/// publishes discovery payloads.
class EleroDynamicCover : public Component, public EleroBlindBase {
 public:
  EleroDynamicCover() = default;

  /// Check if this slot is active
  [[nodiscard]] bool is_active() const { return active_; }

  /// Activate this slot with configuration from NVS
  /// @param config Device configuration
  /// @param parent Elero hub for RF communication
  /// @return true if activated successfully
  bool activate(const NvsDeviceConfig& config, Elero* parent);

  /// Deactivate this slot (returns to dormant state)
  void deactivate();

  /// Get the device configuration
  [[nodiscard]] const NvsDeviceConfig& config() const { return config_; }

  /// Set callback for state changes
  void set_state_callback(DynamicCoverStateCallback cb) { on_state_change_ = std::move(cb); }

  // ─── Component interface ───
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // ─── EleroBlindBase interface ───
  uint32_t get_blind_address() override { return config_.dst_address; }
  void set_rx_state(uint8_t state) override;
  void notify_rx_meta(uint32_t ms, float rssi) override {
    last_seen_ms_ = ms;
    last_rssi_ = rssi;
  }
  void set_poll_offset(uint32_t offset) override { poll_offset_ = offset; }
  std::string get_blind_name() const override { return std::string(config_.name); }
  float get_cover_position() const override { return position_; }
  const char* get_operation_str() const override;
  uint32_t get_last_seen_ms() const override { return last_seen_ms_; }
  float get_last_rssi() const override { return last_rssi_; }
  uint8_t get_last_state_raw() const override { return last_state_raw_; }
  uint8_t get_channel() const override { return config_.channel; }
  uint32_t get_remote_address() const override { return config_.src_address; }
  uint32_t get_poll_interval_ms() const override { return config_.poll_interval_ms; }
  uint32_t get_open_duration_ms() const override { return config_.open_duration_ms; }
  uint32_t get_close_duration_ms() const override { return config_.close_duration_ms; }
  bool get_supports_tilt() const override { return false; }  // Dynamic covers don't support tilt yet
  bool perform_action(const char* action) override;
  void enqueue_command(uint8_t cmd_byte) override { (void)sender_.enqueue(cmd_byte); }
  void schedule_immediate_poll() override;
  void apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) override;

  // ─── Cover operations ───
  void command_open();
  void command_close();
  void command_stop();
  void command_tilt();
  void command_set_position(float position);

 private:
  /// Operation state (matches ESPHome CoverOperation)
  enum class Operation : uint8_t {
    IDLE = 0,
    OPENING = 1,
    CLOSING = 2,
  };

  void start_movement(Operation dir);
  void recompute_position();
  bool is_at_target() const;
  void publish_state();

  // Slot state
  bool active_{false};
  NvsDeviceConfig config_;
  Elero* parent_{nullptr};

  // Cover state
  float position_{0.5f};
  float tilt_{0.0f};
  float target_position_{1.0f};
  float start_position_{0.5f};
  Operation current_operation_{Operation::IDLE};
  Operation last_operation_{Operation::OPENING};

  // Command sender
  CommandSender sender_;

  // Timing
  uint32_t last_poll_{0};
  uint32_t poll_offset_{0};
  uint32_t movement_start_{0};
  uint32_t last_publish_{0};

  // Metadata
  uint32_t last_seen_ms_{0};
  float last_rssi_{0.0f};
  uint8_t last_state_raw_{packet::state::UNKNOWN};

  // Command bytes (could be configurable in future)
  uint8_t command_up_{packet::command::UP};
  uint8_t command_down_{packet::command::DOWN};
  uint8_t command_stop_{packet::command::STOP};
  uint8_t command_check_{packet::command::CHECK};
  uint8_t command_tilt_{packet::command::TILT};

  // Callbacks
  DynamicCoverStateCallback on_state_change_;
};

}  // namespace esphome::elero
