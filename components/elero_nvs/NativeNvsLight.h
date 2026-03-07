#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_traits.h"
#include "../elero/elero.h"
#include "../elero/dynamic_entity_base.h"
#include "../elero/light_core.h"
#include "../elero/command_sender.h"

namespace esphome {
namespace elero {

/// Native+NVS light: IS a light::LightOutput for ESPHome native API,
/// uses DynamicEntityBase for NVS persistence, LightCore for logic.
class NativeNvsLight : public light::LightOutput, public Component, public EleroLightBase, public DynamicEntityBase {
 public:
  // ─── Component lifecycle ───

  void setup() override {}
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // ─── light::LightOutput ───

  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

  // ─── EleroLightBase interface ───

  uint32_t get_blind_address() override { return config().dst_address; }
  void set_rx_state(uint8_t state) override;
  void notify_rx_meta(uint32_t ms, float rssi) override { DynamicEntityBase::notify_rx_meta(ms, rssi); }
  void enqueue_command(uint8_t cmd_byte) override { (void)sender_.enqueue(cmd_byte); }
  void schedule_immediate_poll() override { sender_.enqueue(packet::command::CHECK); }
  std::string get_light_name() const override {
    return light_state_ ? std::string(light_state_->get_name().c_str()) : std::string(config().name);
  }
  uint8_t get_channel() const override { return config().channel; }
  uint32_t get_remote_address() const override { return config().src_address; }
  uint32_t get_dim_duration_ms() const override { return config().dim_duration_ms; }
  float get_brightness() const override { return core_.brightness; }
  bool get_is_on() const override { return core_.is_on; }
  const char *get_operation_str() const override { return core_.operation_str(); }
  uint32_t get_last_seen_ms() const override { return DynamicEntityBase::get_last_seen_ms(); }
  float get_last_rssi() const override { return DynamicEntityBase::get_last_rssi(); }
  uint8_t get_last_state_raw() const override { return DynamicEntityBase::get_last_state_raw(); }
  bool perform_action(const char *action) override;

  // ─── Sync config to core ───

  void sync_config_to_core();

  // ─── Set entity name from NVS config ───

  void apply_name_from_config();

 protected:
  // ─── DynamicEntityBase hooks ───

  const char *entity_tag_() const override { return "elero.nvs_light"; }
  const char *entity_type_str_() const override { return "light"; }
  bool is_matching_type_(const NvsDeviceConfig &cfg) const override { return cfg.is_light(); }
  void do_register_() override { parent_->register_light(this); }
  void do_unregister_() override { parent_->unregister_light(config_.dst_address); }
  void reset_entity_state_() override { core_.reset(); }

 private:
  LightCore core_;
  light::LightState *light_state_{nullptr};
  bool ignore_write_state_{false};
  uint32_t last_publish_{0};
};

}  // namespace elero
}  // namespace esphome
