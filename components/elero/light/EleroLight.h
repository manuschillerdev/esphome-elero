#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_traits.h"
#include "../elero.h"
#include "../command_sender.h"
#include "../light_core.h"

namespace esphome {
namespace elero {

class EleroLight : public light::LightOutput, public Component, public EleroLightBase {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

  // EleroLightBase interface
  uint32_t get_blind_address() override { return this->sender_.command().dst_addr; }
  void set_rx_state(uint8_t state) override;
  void notify_rx_meta(uint32_t ms, float rssi) override {
    this->last_seen_ms_ = ms;
    this->last_rssi_ = rssi;
  }
  void enqueue_command(uint8_t cmd_byte) override { (void)this->sender_.enqueue(cmd_byte); }
  void schedule_immediate_poll() override;
  std::string get_light_name() const override { return this->state_ ? std::string(this->state_->get_name().c_str()) : std::string(); }
  uint8_t get_channel() const override { return this->sender_.command().channel; }
  uint32_t get_remote_address() const override { return this->sender_.command().src_addr; }
  uint32_t get_dim_duration_ms() const override { return this->core_.config.dim_duration_ms; }
  float get_brightness() const override { return this->core_.brightness; }
  bool get_is_on() const override { return this->core_.is_on; }
  const char *get_operation_str() const override { return this->core_.operation_str(); }
  uint32_t get_last_seen_ms() const override { return this->last_seen_ms_; }
  float get_last_rssi() const override { return this->last_rssi_; }
  uint8_t get_last_state_raw() const override {
    return this->core_.is_on ? packet::state::LIGHT_ON : packet::state::LIGHT_OFF;
  }
  bool perform_command(uint8_t cmd_byte) override;
  bool perform_action(const char *action) override;

  // RF parameter setters
  void set_elero_parent(Elero *parent) { this->parent_ = parent; }
  void set_dst_address(uint32_t address) { this->sender_.command().dst_addr = address; }
  void set_channel(uint8_t channel) { this->sender_.command().channel = channel; }
  void set_src_address(uint32_t src) { this->sender_.command().src_addr = src; }
  void set_payload_1(uint8_t payload) { this->sender_.command().payload[0] = payload; }
  void set_payload_2(uint8_t payload) { this->sender_.command().payload[1] = payload; }
  void set_hop(uint8_t hop) { this->sender_.command().hop = hop; }
  void set_type(uint8_t type) { this->sender_.command().type = type; }
  void set_type2(uint8_t type2) { this->sender_.command().type2 = type2; }
  void set_dim_duration(uint32_t dur) { this->core_.config.dim_duration_ms = dur; }
  void set_command_on(uint8_t cmd) { this->core_.command_on = cmd; }
  void set_command_off(uint8_t cmd) { this->core_.command_off = cmd; }
  void set_command_dim_up(uint8_t cmd) { this->core_.command_dim_up = cmd; }
  void set_command_dim_down(uint8_t cmd) { this->core_.command_dim_down = cmd; }
  void set_command_stop(uint8_t cmd) { this->core_.command_stop = cmd; }
  void set_command_check(uint8_t cmd) { this->command_check_ = cmd; }

 protected:
  LightCore core_;
  CommandSender sender_;
  Elero *parent_{nullptr};
  light::LightState *state_{nullptr};

  uint32_t last_publish_{0};

  // Metadata
  uint32_t last_seen_ms_{0};
  float last_rssi_{0.0f};

  // Prevents feedback loop: set_rx_state() -> call.perform() -> write_state() -> send command
  bool ignore_write_state_{false};

  uint8_t command_check_{packet::command::CHECK};
};

}  // namespace elero
}  // namespace esphome
