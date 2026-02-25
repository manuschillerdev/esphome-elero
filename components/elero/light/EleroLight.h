#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_traits.h"
#include "../elero.h"
#include <queue>

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
  uint32_t get_blind_address() override { return this->command_.blind_addr; }
  void set_rx_state(uint8_t state) override;
  void notify_rx_meta(uint32_t ms, float rssi) override {
    this->last_seen_ms_ = ms;
    this->last_rssi_ = rssi;
  }
  void enqueue_command(uint8_t cmd_byte) override { this->commands_to_send_.push(cmd_byte); }

  // RF parameter setters
  void set_elero_parent(Elero *parent) { this->parent_ = parent; }
  void set_blind_address(uint32_t address) { this->command_.blind_addr = address; }
  void set_channel(uint8_t channel) { this->command_.channel = channel; }
  void set_remote_address(uint32_t remote) { this->command_.remote_addr = remote; }
  void set_payload_1(uint8_t payload) { this->command_.payload[0] = payload; }
  void set_payload_2(uint8_t payload) { this->command_.payload[1] = payload; }
  void set_hop(uint8_t hop) { this->command_.hop = hop; }
  void set_pckinf_1(uint8_t pckinf) { this->command_.pck_inf[0] = pckinf; }
  void set_pckinf_2(uint8_t pckinf) { this->command_.pck_inf[1] = pckinf; }
  void set_dim_duration(uint32_t dur) { this->dim_duration_ = dur; }
  void set_command_on(uint8_t cmd) { this->command_on_ = cmd; }
  void set_command_off(uint8_t cmd) { this->command_off_ = cmd; }
  void set_command_dim_up(uint8_t cmd) { this->command_dim_up_ = cmd; }
  void set_command_dim_down(uint8_t cmd) { this->command_dim_down_ = cmd; }
  void set_command_stop(uint8_t cmd) { this->command_stop_ = cmd; }
  void set_command_check(uint8_t cmd) { this->command_check_ = cmd; }

  void handle_commands(uint32_t now);
  void recompute_brightness();

 protected:
  void increase_counter();

  t_elero_command command_ = {
    .counter = 1,
  };
  Elero *parent_{nullptr};
  light::LightState *state_{nullptr};

  // Brightness tracking (0.0 = off, 1.0 = full brightness)
  float brightness_{0.0f};
  float target_brightness_{0.0f};
  bool is_on_{false};
  bool is_dimming_{false};
  bool dim_up_{true};
  uint32_t dimming_start_{0};
  uint32_t last_recompute_time_{0};
  uint32_t last_publish_{0};
  uint32_t dim_duration_{0};

  // Command queue / TX state (mirrors EleroCover)
  std::queue<uint8_t> commands_to_send_;
  uint32_t last_command_{0};
  uint8_t send_retries_{0};
  uint8_t send_packets_{0};

  // Metadata
  uint32_t last_seen_ms_{0};
  float last_rssi_{0.0f};

  // Prevents feedback loop: set_rx_state() → call.perform() → write_state() → send command
  bool ignore_write_state_{false};

  // Configurable command bytes
  uint8_t command_on_{0x20};
  uint8_t command_off_{0x40};
  uint8_t command_dim_up_{0x20};
  uint8_t command_dim_down_{0x40};
  uint8_t command_stop_{0x10};
  uint8_t command_check_{0x00};
};

}  // namespace elero
}  // namespace esphome
