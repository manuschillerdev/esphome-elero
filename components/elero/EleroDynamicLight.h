#pragma once

#include "elero.h"
#include "dynamic_entity_base.h"
#include "light_core.h"
#include <functional>

namespace esphome {
namespace elero {

/// Dynamic light slot for MQTT mode.
/// Pre-allocated at compile time, activated at runtime from NVS config.
/// Now supports brightness/dimming via LightCore (same logic as YAML EleroLight).
class EleroDynamicLight : public EleroLightBase, public DynamicEntityBase {
 public:
  using StateCallback = std::function<void(EleroDynamicLight *)>;

  // ─── EleroLightBase interface ───

  uint32_t get_blind_address() override { return config().dst_address; }
  void set_rx_state(uint8_t state) override;
  void notify_rx_meta(uint32_t ms, float rssi) override { DynamicEntityBase::notify_rx_meta(ms, rssi); }
  void enqueue_command(uint8_t cmd_byte) override { (void)sender_.enqueue(cmd_byte); }
  void schedule_immediate_poll() override;
  std::string get_light_name() const override { return std::string(config().name); }
  uint8_t get_channel() const override { return config().channel; }
  uint32_t get_remote_address() const override { return config().src_address; }
  uint32_t get_dim_duration_ms() const override { return config().dim_duration_ms; }
  bool is_enabled() const override { return config().is_enabled(); }
  uint32_t get_updated_at() const override { return config().updated_at; }
  float get_brightness() const override { return core_.brightness; }
  bool get_is_on() const override { return core_.is_on; }
  const char *get_operation_str() const override { return core_.operation_str(); }
  uint32_t get_last_seen_ms() const override { return DynamicEntityBase::get_last_seen_ms(); }
  float get_last_rssi() const override { return DynamicEntityBase::get_last_rssi(); }
  uint8_t get_last_state_raw() const override { return DynamicEntityBase::get_last_state_raw(); }
  bool perform_action(const char *action) override;

  /// Set target brightness (0.0–1.0) and start dimming if needed.
  /// Used by MQTT handler to process HA brightness commands.
  void set_brightness(float target, uint32_t now);

  // ─── State callback ───

  void set_state_callback(StateCallback cb) { state_callback_ = std::move(cb); }

  // ─── Accessors ───

  const LightCore &core() const { return core_; }

  // ─── Loop ───

  void loop(uint32_t now);

  // ─── Sync config to core ───

  void sync_config_to_core();

 protected:
  // ─── DynamicEntityBase hooks ───

  const char *entity_tag_() const override { return "elero.dyn_light"; }
  const char *entity_type_str_() const override { return "light"; }
  bool is_matching_type_(const NvsDeviceConfig &cfg) const override { return cfg.is_light(); }
  void do_register_() override { parent_->register_light(this); }
  void do_unregister_() override { parent_->unregister_light(config_.dst_address); }
  void reset_entity_state_() override { core_.reset(); }

 private:
  void publish_state_();

  StateCallback state_callback_{};
  LightCore core_;
  uint32_t last_publish_{0};
};

}  // namespace elero
}  // namespace esphome
