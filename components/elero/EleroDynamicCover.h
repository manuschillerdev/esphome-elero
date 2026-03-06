#pragma once

#include "elero.h"
#include "nvs_config.h"
#include "elero_packet.h"
#include <queue>
#include <functional>

namespace esphome::elero {

/// Dynamic cover slot for MQTT mode.
/// Pre-allocated at compile time, activated at runtime from NVS config.
/// Does NOT inherit from ESPHome Cover — state is published via MQTT, not native API.
class EleroDynamicCover : public EleroBlindBase {
 public:
  enum class Operation : uint8_t { IDLE = 0, OPENING = 1, CLOSING = 2 };

  using StateCallback = std::function<void(EleroDynamicCover *)>;

  // ─── Activation ───

  bool activate(const NvsDeviceConfig &config, Elero *parent);
  void deactivate();
  bool is_active() const { return active_; }

  // ─── EleroBlindBase interface ───

  void set_rx_state(uint8_t state) override;
  uint32_t get_blind_address() override { return config_.dst_address; }
  void set_poll_offset(uint32_t offset) override { poll_offset_ = offset; }
  void notify_rx_meta(uint32_t ms, float rssi) override;
  std::string get_blind_name() const override { return std::string(config_.name); }
  float get_cover_position() const override { return position_; }
  const char *get_operation_str() const override;
  uint32_t get_last_seen_ms() const override { return last_seen_ms_; }
  float get_last_rssi() const override { return last_rssi_; }
  uint8_t get_last_state_raw() const override { return last_state_raw_; }
  uint8_t get_channel() const override { return config_.channel; }
  uint32_t get_remote_address() const override { return config_.src_address; }
  uint32_t get_poll_interval_ms() const override { return config_.poll_interval_ms; }
  uint32_t get_open_duration_ms() const override { return config_.open_duration_ms; }
  uint32_t get_close_duration_ms() const override { return config_.close_duration_ms; }
  bool get_supports_tilt() const override { return false; }
  bool perform_action(const char *action) override;
  void enqueue_command(uint8_t cmd_byte) override;
  void schedule_immediate_poll() override;
  void apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) override;

  // ─── State callback ───

  void set_state_callback(StateCallback cb) { state_callback_ = std::move(cb); }

  // ─── Accessors ───

  const NvsDeviceConfig &config() const { return config_; }
  Operation current_operation() const { return current_operation_; }
  float position() const { return position_; }
  float tilt() const { return tilt_; }

  // ─── Loop (called by device manager) ───

  void loop(uint32_t now);

 private:
  void publish_state_();
  void send_next_command_();

  bool active_{false};
  NvsDeviceConfig config_{};
  Elero *parent_{nullptr};
  StateCallback state_callback_{};

  // State tracking
  float position_{0.5f};
  float tilt_{0.0f};
  Operation current_operation_{Operation::IDLE};
  uint8_t last_state_raw_{0};
  uint32_t last_seen_ms_{0};
  float last_rssi_{0.0f};

  // Command queue
  std::queue<uint8_t> commands_;
  uint32_t last_command_ms_{0};

  // Polling
  uint32_t poll_offset_{0};
  uint32_t last_poll_ms_{0};
  bool immediate_poll_{false};

  // Position tracking
  uint32_t movement_start_ms_{0};
  float movement_start_pos_{0.0f};

  // Rolling counter for TX
  uint8_t counter_{1};
};

}  // namespace esphome::elero
