#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/cover/cover.h"
#include "../elero.h"
#include "../command_sender.h"
#include "../cover_core.h"

namespace esphome {
namespace elero {

class EleroCover : public cover::Cover, public Component, public EleroBlindBase {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  cover::CoverTraits get_traits() override;

  void set_elero_parent(Elero *parent) { this->parent_ = parent; }
  void set_dst_address(uint32_t address) { this->sender_.command().dst_addr = address; }
  void set_channel(uint8_t channel) { this->sender_.command().channel = channel; }
  void set_src_address(uint32_t src) { this->sender_.command().src_addr = src; }
  void set_payload_1(uint8_t payload) { this->sender_.command().payload[0] = payload; }
  void set_payload_2(uint8_t payload) { this->sender_.command().payload[1] = payload; }
  void set_hop(uint8_t hop) { this->sender_.command().hop = hop; }
  void set_type(uint8_t type) { this->sender_.command().type = type; }
  void set_type2(uint8_t type2) { this->sender_.command().type2 = type2; }
  void set_command_up(uint8_t cmd) { this->command_up_ = cmd; }
  void set_command_down(uint8_t cmd) { this->command_down_ = cmd; }
  void set_command_stop(uint8_t cmd) { this->command_stop_ = cmd; }
  void set_command_check(uint8_t cmd) { this->command_check_ = cmd; }
  void set_command_tilt(uint8_t cmd) { this->command_tilt_ = cmd; }
  void set_poll_offset(uint32_t offset) override { this->core_.config.poll_offset = offset; }
  void set_close_duration(uint32_t dur) { this->core_.config.close_duration_ms = dur; }
  void set_open_duration(uint32_t dur) { this->core_.config.open_duration_ms = dur; }
  void set_poll_interval(uint32_t intvl) { this->core_.config.poll_interval_ms = intvl; }
  uint32_t get_blind_address() override { return this->sender_.command().dst_addr; }
  void set_supports_tilt(bool tilt) { this->core_.config.supports_tilt = tilt; }
  void set_rx_state(uint8_t state) override;
  void notify_rx_meta(uint32_t ms, float rssi) override {
    this->last_seen_ms_ = ms;
    this->last_rssi_ = rssi;
  }
  // EleroBlindBase web API helpers — identity & state
  std::string get_blind_name() const override { return std::string(this->get_name().c_str()); }
  float get_cover_position() const override { return this->position; }
  const char *get_operation_str() const override { return core_.operation_str(); }
  uint32_t get_last_seen_ms() const override { return this->last_seen_ms_; }
  float get_last_rssi() const override { return this->last_rssi_; }
  uint8_t get_last_state_raw() const override { return this->last_state_raw_; }
  // EleroBlindBase web API helpers — configuration
  uint8_t get_channel() const override { return this->sender_.command().channel; }
  uint32_t get_remote_address() const override { return this->sender_.command().src_addr; }
  uint32_t get_poll_interval_ms() const override { return this->core_.config.poll_interval_ms; }
  uint32_t get_open_duration_ms() const override { return this->core_.config.open_duration_ms; }
  uint32_t get_close_duration_ms() const override { return this->core_.config.close_duration_ms; }
  bool get_supports_tilt() const override { return this->core_.config.supports_tilt; }
  // EleroBlindBase web API commands
  bool perform_action(const char *action) override;
  void enqueue_command(uint8_t cmd_byte) override { (void)this->sender_.enqueue(cmd_byte); }
  // Apply runtime settings. Values of 0 mean "keep existing".
  void apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) override {
    this->core_.apply_runtime_settings(open_dur_ms, close_dur_ms, poll_intvl_ms);
  }

  void schedule_immediate_poll() override;
  void on_remote_command(uint8_t command_byte) override;

 protected:
  void control(const cover::CoverCall &call) override;

  /// Sync ESPHome cover state from CoverCore state.
  void sync_from_core_();

  /// Start a movement via the command sender.
  void start_movement(cover::CoverOperation dir);

  CoverCore core_;
  CommandSender sender_;
  Elero *parent_;
  uint32_t last_publish_{0};
  uint32_t last_seen_ms_{0};
  float last_rssi_{0.0f};
  uint8_t last_state_raw_{packet::state::UNKNOWN};
  uint8_t command_up_{packet::command::UP};
  uint8_t command_down_{packet::command::DOWN};
  uint8_t command_check_{packet::command::CHECK};
  uint8_t command_stop_{packet::command::STOP};
  uint8_t command_tilt_{packet::command::TILT};
};

}  // namespace elero
}  // namespace esphome
