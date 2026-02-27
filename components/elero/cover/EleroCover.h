#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/cover/cover.h"
#include "../elero.h"
#include "../command_sender.h"

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
  void set_blind_address(uint32_t address) { this->sender_.command().blind_addr = address; }
  void set_channel(uint8_t channel) { this->sender_.command().channel = channel; }
  void set_remote_address(uint32_t remote) { this->sender_.command().remote_addr = remote; }
  void set_payload_1(uint8_t payload) { this->sender_.command().payload[0] = payload; }
  void set_payload_2(uint8_t payload) { this->sender_.command().payload[1] = payload; }
  void set_hop(uint8_t hop) { this->sender_.command().hop = hop; }
  void set_pckinf_1(uint8_t pckinf) { this->sender_.command().pck_inf[0] = pckinf; }
  void set_pckinf_2(uint8_t pckinf) { this->sender_.command().pck_inf[1] = pckinf; }
  void set_command_up(uint8_t cmd) { this->command_up_ = cmd; }
  void set_command_down(uint8_t cmd) { this->command_down_ = cmd; }
  void set_command_stop(uint8_t cmd) { this->command_stop_ = cmd; }
  void set_command_check(uint8_t cmd) { this->command_check_ = cmd; }
  void set_command_tilt(uint8_t cmd) { this->command_tilt_ = cmd; }
  void set_poll_offset(uint32_t offset) override { this->poll_offset_ = offset; }
  void set_close_duration(uint32_t dur) { this->close_duration_ = dur; }
  void set_open_duration(uint32_t dur) { this->open_duration_ = dur; }
  void set_poll_interval(uint32_t intvl) { this->poll_intvl_ = intvl; }
  uint32_t get_blind_address() override { return this->sender_.command().blind_addr; }
  void set_supports_tilt(bool tilt) { this->supports_tilt_ = tilt; }
  void set_rx_state(uint8_t state) override;
  void notify_rx_meta(uint32_t ms, float rssi) override {
    this->last_seen_ms_ = ms;
    this->last_rssi_ = rssi;
  }
  // EleroBlindBase web API helpers — identity & state
  std::string get_blind_name() const override { return std::string(this->get_name().c_str()); }
  float get_cover_position() const override { return this->position; }
  const char *get_operation_str() const override {
    return this->current_operation == cover::COVER_OPERATION_IDLE      ? "idle"
           : this->current_operation == cover::COVER_OPERATION_OPENING ? "opening"
                                                                       : "closing";
  }
  uint32_t get_last_seen_ms() const override { return this->last_seen_ms_; }
  float get_last_rssi() const override { return this->last_rssi_; }
  uint8_t get_last_state_raw() const override { return this->last_state_raw_; }
  // EleroBlindBase web API helpers — configuration
  uint8_t get_channel() const override { return this->sender_.command().channel; }
  uint32_t get_remote_address() const override { return this->sender_.command().remote_addr; }
  uint32_t get_poll_interval_ms() const override { return this->poll_intvl_; }
  uint32_t get_open_duration_ms() const override { return this->open_duration_; }
  uint32_t get_close_duration_ms() const override { return this->close_duration_; }
  bool get_supports_tilt() const override { return this->supports_tilt_; }
  // EleroBlindBase web API commands
  bool perform_action(const char *action) override;
  void enqueue_command(uint8_t cmd_byte) override { (void)this->sender_.enqueue(cmd_byte); }
  // Apply runtime settings. Values of 0 mean "keep existing".
  void apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) override {
    if (open_dur_ms != 0)
      this->open_duration_ = open_dur_ms;
    if (close_dur_ms != 0)
      this->close_duration_ = close_dur_ms;
    if (poll_intvl_ms != 0)
      this->poll_intvl_ = poll_intvl_ms;
  }

  void schedule_immediate_poll() override;
  void recompute_position();
  void start_movement(cover::CoverOperation op);
  bool is_at_target();

 protected:
  void control(const cover::CoverCall &call) override;

  CommandSender sender_;
  Elero *parent_;
  uint32_t last_poll_{0};
  uint32_t poll_offset_{0};
  uint32_t movement_start_{0};
  uint32_t open_duration_{0};
  uint32_t close_duration_{0};
  uint32_t last_publish_{0};
  uint32_t poll_intvl_{0};
  float target_position_{0};
  float start_position_{0.0f};  // Position at movement start for precise tracking
  bool supports_tilt_{false};
  uint32_t last_seen_ms_{0};
  float last_rssi_{0.0f};
  uint8_t last_state_raw_{ELERO_STATE_UNKNOWN};
  uint8_t command_up_{0x20};
  uint8_t command_down_{0x40};
  uint8_t command_check_{0x00};
  uint8_t command_stop_{0x10};
  uint8_t command_tilt_{0x24};
  cover::CoverOperation last_operation_{cover::COVER_OPERATION_OPENING};
};

}  // namespace elero
}  // namespace esphome
