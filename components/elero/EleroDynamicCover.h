#pragma once

#include "elero.h"
#include "dynamic_entity_base.h"
#include "elero_packet.h"
#include <functional>

namespace esphome {
namespace elero {

/// Dynamic cover slot for MQTT mode.
/// Pre-allocated at compile time, activated at runtime from NVS config.
/// Does NOT inherit from ESPHome Cover — state is published via MQTT, not native API.
class EleroDynamicCover : public EleroBlindBase, public DynamicEntityBase {
 public:
  enum class Operation : uint8_t { IDLE = 0, OPENING = 1, CLOSING = 2 };

  using StateCallback = std::function<void(EleroDynamicCover *)>;

  // ─── EleroBlindBase interface ───

  void set_rx_state(uint8_t state) override;
  uint32_t get_blind_address() override { return config().dst_address; }
  void set_poll_offset(uint32_t offset) override { poll_offset_ = offset; }
  void notify_rx_meta(uint32_t ms, float rssi) override { DynamicEntityBase::notify_rx_meta(ms, rssi); }
  std::string get_blind_name() const override { return std::string(config().name); }
  float get_cover_position() const override { return position_; }
  const char *get_operation_str() const override;
  uint32_t get_last_seen_ms() const override { return DynamicEntityBase::get_last_seen_ms(); }
  float get_last_rssi() const override { return DynamicEntityBase::get_last_rssi(); }
  uint8_t get_last_state_raw() const override { return DynamicEntityBase::get_last_state_raw(); }
  uint8_t get_channel() const override { return config().channel; }
  uint32_t get_remote_address() const override { return config().src_address; }
  uint32_t get_poll_interval_ms() const override { return config().poll_interval_ms; }
  uint32_t get_open_duration_ms() const override { return config().open_duration_ms; }
  uint32_t get_close_duration_ms() const override { return config().close_duration_ms; }
  bool get_supports_tilt() const override { return config().supports_tilt != 0; }
  bool perform_action(const char *action) override;
  void enqueue_command(uint8_t cmd_byte) override { (void)sender_.enqueue(cmd_byte); }
  void schedule_immediate_poll() override;
  void apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) override;

  // ─── State callback ───

  void set_state_callback(StateCallback cb) { state_callback_ = std::move(cb); }

  // ─── Accessors ───

  Operation current_operation() const { return current_operation_; }
  float position() const { return position_; }
  float tilt() const { return tilt_; }

  // ─── Loop (called by device manager) ───

  void loop(uint32_t now);

 protected:
  // ─── DynamicEntityBase hooks ───

  const char *entity_tag_() const override { return "elero.dyn_cover"; }
  const char *entity_type_str_() const override { return "cover"; }
  bool is_matching_type_(const NvsDeviceConfig &cfg) const override { return cfg.is_cover(); }
  void do_register_() override { parent_->register_cover(this); }
  void do_unregister_() override { parent_->unregister_cover(config_.dst_address); }
  void reset_entity_state_() override;

 private:
  void publish_state_();

  StateCallback state_callback_{};

  // Cover-specific state
  float position_{0.5f};
  float tilt_{0.0f};
  Operation current_operation_{Operation::IDLE};

  // Polling
  uint32_t poll_offset_{0};
  uint32_t last_poll_ms_{0};
  bool immediate_poll_{false};

  // Position tracking
  uint32_t movement_start_ms_{0};
  float movement_start_pos_{0.0f};
};

}  // namespace elero
}  // namespace esphome
