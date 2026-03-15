#pragma once

#include "esphome/core/component.h"
#include "esphome/components/cover/cover.h"
#include "../elero/elero.h"
#include "../elero/dynamic_entity_base.h"
#include "../elero/cover_core.h"
#include "../elero/command_sender.h"

namespace esphome {
namespace elero {

/// Native+NVS cover: IS a cover::Cover for ESPHome native API,
/// uses DynamicEntityBase for NVS persistence, CoverCore for logic.
/// Pre-allocated in a static array, activated at runtime from NVS.
class NativeNvsCover : public cover::Cover, public Component, public EleroBlindBase, public DynamicEntityBase {
 public:
  // ─── Component lifecycle ───

  void setup() override {}  // Setup handled by NativeNvsDeviceManager
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // ─── cover::Cover ───

  cover::CoverTraits get_traits() override;

  // ─── EleroBlindBase interface ───

  void set_rx_state(uint8_t state) override;
  uint32_t get_blind_address() override { return config().dst_address; }
  void set_poll_offset(uint32_t offset) override { core_.config.poll_offset = offset; }
  void notify_rx_meta(uint32_t ms, float rssi) override { DynamicEntityBase::notify_rx_meta(ms, rssi); }
  std::string get_blind_name() const override { return std::string(config().name); }
  float get_cover_position() const override { return this->position; }
  const char *get_operation_str() const override { return core_.operation_str(); }
  uint32_t get_last_seen_ms() const override { return DynamicEntityBase::get_last_seen_ms(); }
  float get_last_rssi() const override { return DynamicEntityBase::get_last_rssi(); }
  uint8_t get_last_state_raw() const override { return DynamicEntityBase::get_last_state_raw(); }
  uint8_t get_channel() const override { return config().channel; }
  uint32_t get_remote_address() const override { return config().src_address; }
  uint32_t get_poll_interval_ms() const override { return config().poll_interval_ms; }
  uint32_t get_open_duration_ms() const override { return config().open_duration_ms; }
  uint32_t get_close_duration_ms() const override { return config().close_duration_ms; }
  bool get_supports_tilt() const override { return config().supports_tilt != 0; }
  uint32_t get_updated_at() const override { return config().updated_at; }
  bool perform_action(const char *action) override;
  void enqueue_command(uint8_t cmd_byte) override { (void)sender_.enqueue(cmd_byte); }
  void schedule_immediate_poll() override { core_.immediate_poll = true; }
  void on_remote_command(uint8_t command_byte) override;
  void apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) override;

  // ─── Sync config to core ───

  void sync_config_to_core();

  // ─── Set entity name from NVS config ───

  void apply_name_from_config();

 protected:
  // ─── cover::Cover ───

  void control(const cover::CoverCall &call) override;

  // ─── DynamicEntityBase hooks ───

  const char *entity_tag_() const override { return "elero.nvs_cover"; }
  const char *entity_type_str_() const override { return "cover"; }
  bool is_matching_type_(const NvsDeviceConfig &cfg) const override { return cfg.is_cover(); }
  void do_register_() override { parent_->register_cover(this); }
  void do_unregister_() override { parent_->unregister_cover(config_.dst_address); }
  void reset_entity_state_() override { core_.reset(); }

 private:
  void start_movement_(cover::CoverOperation dir);

  CoverCore core_;
  uint32_t last_publish_{0};
};

}  // namespace elero
}  // namespace esphome
