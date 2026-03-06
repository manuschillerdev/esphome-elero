#pragma once

#include "elero.h"
#include "nvs_config.h"
#include "elero_packet.h"
#include <queue>
#include <functional>

namespace esphome::elero {

/// Dynamic light slot for MQTT mode.
/// Pre-allocated at compile time, activated at runtime from NVS config.
class EleroDynamicLight : public EleroLightBase {
 public:
  using StateCallback = std::function<void(EleroDynamicLight *)>;

  // ─── Activation ───

  bool activate(const NvsDeviceConfig &config, Elero *parent);
  void deactivate();
  bool is_active() const { return active_; }

  // ─── EleroLightBase interface ───

  uint32_t get_blind_address() override { return config_.dst_address; }
  void set_rx_state(uint8_t state) override;
  void notify_rx_meta(uint32_t ms, float rssi) override;
  void enqueue_command(uint8_t cmd_byte) override;
  void schedule_immediate_poll() override;
  std::string get_light_name() const override { return std::string(config_.name); }
  uint8_t get_channel() const override { return config_.channel; }
  uint32_t get_remote_address() const override { return config_.src_address; }
  uint32_t get_dim_duration_ms() const override { return 0; }
  float get_brightness() const override { return is_on_ ? 1.0f : 0.0f; }
  bool get_is_on() const override { return is_on_; }
  const char *get_operation_str() const override { return "idle"; }
  uint32_t get_last_seen_ms() const override { return last_seen_ms_; }
  float get_last_rssi() const override { return last_rssi_; }
  uint8_t get_last_state_raw() const override { return last_state_raw_; }
  bool perform_action(const char *action) override;

  // ─── State callback ───

  void set_state_callback(StateCallback cb) { state_callback_ = std::move(cb); }

  const NvsDeviceConfig &config() const { return config_; }

  // ─── Loop ───

  void loop(uint32_t now);

 private:
  void publish_state_();
  void send_next_command_();

  bool active_{false};
  NvsDeviceConfig config_{};
  Elero *parent_{nullptr};
  StateCallback state_callback_{};

  bool is_on_{false};
  uint8_t last_state_raw_{0};
  uint32_t last_seen_ms_{0};
  float last_rssi_{0.0f};

  std::queue<uint8_t> commands_;
  uint32_t last_command_ms_{0};
  uint8_t counter_{1};
};

}  // namespace esphome::elero
