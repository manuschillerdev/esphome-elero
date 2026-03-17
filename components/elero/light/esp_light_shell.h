/// @file esp_light_shell.h
/// @brief Thin ESPHome light::LightOutput adapter — delegates all state to the Device model.

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "../device.h"
#include "../device_registry.h"
#include "../light_sm.h"
#include "../elero_packet.h"

namespace esphome {
namespace elero {

class EspLightShell : public light::LightOutput, public Component {
 public:
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

  // ── Config setters (called by Python codegen) ──────────────
  void set_registry(DeviceRegistry *r) { registry_ = r; }
  void set_dst_address(uint32_t v) { cfg_.dst_address = v; }
  void set_src_address(uint32_t v) { cfg_.src_address = v; }
  void set_channel(uint8_t v) { cfg_.channel = v; }
  void set_hop(uint8_t v) { cfg_.hop = v; }
  void set_payload_1(uint8_t v) { cfg_.payload_1 = v; }
  void set_payload_2(uint8_t v) { cfg_.payload_2 = v; }
  void set_type(uint8_t v) { cfg_.type_byte = v; }
  void set_type2(uint8_t v) { cfg_.type2 = v; }
  void set_dim_duration(uint32_t v) { cfg_.dim_duration_ms = v; }
  void set_light_state(light::LightState *s) { light_state_ = s; }
  void set_device_name(const char *n) { cfg_.set_name(n); }
  void set_slot_index(int idx) { slot_index_ = idx; }  ///< NVS mode: bind to pre-restored slot

  // ── ESPHome Component lifecycle ────────────────────────────
  void setup() override {
    if (!registry_) return;
    if (slot_index_ >= 0) {
      // NVS mode: bind to pre-restored registry slot
      device_ = registry_->slot(static_cast<size_t>(slot_index_));
    } else {
      // Native mode: register device from config
      cfg_.type = DeviceType::LIGHT;
      device_ = registry_->register_device(cfg_);
    }
  }

  void loop() override {
    if (!device_ || !device_->active) return;
    if (device_->last_notify_ms > last_published_ms_) {
      sync_and_publish_();
      last_published_ms_ = device_->last_notify_ms;
    }
  }

  // ── ESPHome Light interface ────────────────────────────────
  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    // Use device config when bound (NVS mode), fall back to local cfg_ (native mode)
    const auto &cfg = (device_ && device_->active) ? device_->config : cfg_;
    auto ctx = light_context(cfg);
    traits.set_supported_color_modes({
        light_sm::supports_brightness(ctx)
            ? light::ColorMode::BRIGHTNESS
            : light::ColorMode::ON_OFF
    });
    return traits;
  }

  void write_state(light::LightState *state) override {
    if (!device_ || !device_->is_light() || ignore_write_state_) return;

    auto &light = std::get<LightDevice>(device_->logic);
    auto ctx = light_context(device_->config);
    uint32_t now = esphome::millis();

    float brightness;
    state->current_values_as_brightness(&brightness);
    bool is_on = state->current_values.is_on();

    if (!is_on) {
      light.state = light_sm::on_turn_off(light.state);
      device_->sender.clear_queue();
      device_->sender.enqueue(packet::command::DOWN);
      return;
    }

    if (!light_sm::supports_brightness(ctx)) {
      light.state = light_sm::on_turn_on(light.state, now, ctx);
      device_->sender.enqueue(packet::command::UP);
      return;
    }

    light.state = light_sm::on_set_brightness(light.state, brightness, now, ctx);
    if (std::holds_alternative<light_sm::DimmingUp>(light.state)) {
      device_->sender.enqueue(packet::command::UP);
    } else if (std::holds_alternative<light_sm::DimmingDown>(light.state)) {
      device_->sender.enqueue(packet::command::DOWN);
    } else if (light_sm::is_on(light.state)) {
      device_->sender.enqueue(packet::command::UP);
    }
  }

 private:
  void sync_and_publish_() {
    if (!device_ || !device_->is_light() || !light_state_) return;

    auto &light = std::get<LightDevice>(device_->logic);
    auto ctx = light_context(device_->config);
    uint32_t now = esphome::millis();

    bool on = light_sm::is_on(light.state);
    float bri = light_sm::brightness(light.state, now, ctx);

    ignore_write_state_ = true;
    auto call = light_state_->make_call();
    call.set_state(on);
    if (on && light_sm::supports_brightness(ctx)) {
      call.set_brightness(bri);
    }
    call.perform();
    ignore_write_state_ = false;
  }

  NvsDeviceConfig cfg_{};
  DeviceRegistry *registry_{nullptr};
  Device *device_{nullptr};
  light::LightState *light_state_{nullptr};
  uint32_t last_published_ms_{0};
  bool ignore_write_state_{false};
  int slot_index_{-1};  ///< -1 = native mode, >=0 = NVS mode
};

}  // namespace elero
}  // namespace esphome
