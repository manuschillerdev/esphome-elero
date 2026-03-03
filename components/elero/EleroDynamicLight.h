#pragma once

/// @file EleroDynamicLight.h
/// @brief Dynamic light slot that can be activated at runtime via NVS config.
///
/// Unlike EleroLight which is configured at compile time via YAML and uses
/// ESPHome's native API, EleroDynamicLight:
/// - Is pre-allocated at compile time (dormant until activated)
/// - Activated at runtime with NvsDeviceConfig
/// - Does NOT register with ESPHome's native light API
/// - Uses MQTT discovery for Home Assistant integration

#include "esphome/core/component.h"
#include "elero.h"
#include "command_sender.h"
#include "nvs_config.h"

namespace esphome::elero {

/// Callback type for light state changes (used by MQTT bridge)
using DynamicLightStateCallback = std::function<void(uint32_t addr, bool on, float brightness)>;

/// Dynamic light slot that can be activated at runtime.
///
/// This class implements light logic without ESPHome's native Light base class,
/// enabling MQTT-only operation.
class EleroDynamicLight : public Component, public EleroLightBase {
 public:
  EleroDynamicLight() = default;

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
  void set_state_callback(DynamicLightStateCallback cb) { on_state_change_ = std::move(cb); }

  // ─── Component interface ───
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // ─── Address accessors ───
  /// Get the destination address (alias for get_blind_address for light context)
  [[nodiscard]] uint32_t get_light_address() const { return config_.dst_address; }

  // ─── EleroLightBase interface ───
  uint32_t get_blind_address() override { return config_.dst_address; }
  void set_rx_state(uint8_t state) override;
  void notify_rx_meta(uint32_t ms, float rssi) override {
    last_seen_ms_ = ms;
    last_rssi_ = rssi;
  }
  void enqueue_command(uint8_t cmd_byte) override { (void)sender_.enqueue(cmd_byte); }
  void schedule_immediate_poll() override;
  std::string get_light_name() const override { return std::string(config_.name); }
  uint8_t get_channel() const override { return config_.channel; }
  uint32_t get_remote_address() const override { return config_.src_address; }
  uint32_t get_dim_duration_ms() const override { return dim_duration_; }

  // ─── Light operations ───
  void command_on();
  void command_off();
  void command_toggle();
  void set_brightness(float brightness);

  /// Get current brightness (0.0-1.0)
  [[nodiscard]] float brightness() const { return brightness_; }

  /// Get whether light is on
  [[nodiscard]] bool is_on() const { return is_on_; }

 private:
  /// Direction of dimming operation
  enum class DimDirection : uint8_t {
    NONE,
    UP,
    DOWN,
  };

  void recompute_brightness();
  void publish_state();

  // Slot state
  bool active_{false};
  NvsDeviceConfig config_;
  Elero* parent_{nullptr};

  // Light state
  bool is_on_{false};
  float brightness_{0.0f};
  float target_brightness_{1.0f};
  DimDirection dim_direction_{DimDirection::NONE};
  uint32_t dimming_start_{0};
  uint32_t last_recompute_time_{0};
  uint32_t last_publish_{0};

  // Timing (extracted from config or set via YAML for non-dynamic)
  uint32_t dim_duration_{0};  // 0 = on/off only, >0 = supports dimming

  // Command sender
  CommandSender sender_;

  // Metadata
  uint32_t last_seen_ms_{0};
  float last_rssi_{0.0f};

  // Command bytes
  uint8_t command_on_{packet::command::UP};
  uint8_t command_off_{packet::command::DOWN};
  uint8_t command_dim_up_{packet::command::UP};
  uint8_t command_dim_down_{packet::command::DOWN};
  uint8_t command_stop_{packet::command::STOP};
  uint8_t command_check_{packet::command::CHECK};

  // Callbacks
  DynamicLightStateCallback on_state_change_;
};

}  // namespace esphome::elero
