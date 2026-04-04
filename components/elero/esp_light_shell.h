/// @file esp_light_shell.h
/// @brief Thin ESPHome light::LightOutput adapter — delegates all state to the Device model.

#pragma once

#ifdef USE_LIGHT
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#include "device.h"
#include "device_registry.h"
#include "light_sm.h"
#include "state_snapshot.h"  // state_change:: flags
#include "elero_strings.h"   // PERCENT_SCALE
#include "elero_packet.h"
#ifdef USE_BUTTON
#include "refresh_button.h"
#endif

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

  // ── Sensor setters (published from sync_and_publish_ via snapshot) ──
#ifdef USE_SENSOR
  void set_rssi_sensor(sensor::Sensor *s) { rssi_sensor_ = s; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_status_sensor(text_sensor::TextSensor *s) { status_sensor_ = s; }
#endif
#ifdef USE_BUTTON
  void set_refresh_button(RefreshButton *b) { refresh_button_ = b; }
#endif

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
#ifdef USE_BUTTON
    if (refresh_button_ && device_) {
      refresh_button_->set_device(device_);
      refresh_button_->set_registry(registry_);
    }
#endif
  }

  void loop() override {
    if (!device_ || !device_->active) return;
    // No initial_published_ needed here — ESPHome LightState defaults to "off",
    // which is a valid state. Covers need it because cover::Cover starts as "Unknown".
    if (device_->last_notify_ms > last_published_ms_) {
      sync_and_publish_(device_->last_changes);
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
    if (!device_ || !device_->is_light() || !registry_ || ignore_write_state_) return;

    float brightness;
    state->current_values_as_brightness(&brightness);
    bool is_on = state->current_values.is_on();

    if (!is_on) {
      registry_->command_light(*device_, packet::command::DOWN);
    } else {
      registry_->set_light_brightness(*device_, brightness);
    }
  }

 private:
  void sync_and_publish_(uint16_t changes) {
    if (!device_ || !device_->is_light() || !light_state_) return;
    const auto &pub = std::get<LightDevice>(device_->logic).published;

    if (changes & state_change::BRIGHTNESS) {
      ignore_write_state_ = true;
      auto call = light_state_->make_call();
      call.set_state(pub.is_on);
      auto ctx = light_context(device_->config);
      if (pub.is_on && light_sm::supports_brightness(ctx)) {
        call.set_brightness(static_cast<float>(pub.brightness_pct) / PERCENT_SCALE);
      }
      call.perform();
      ignore_write_state_ = false;
    }

#ifdef USE_SENSOR
    if ((changes & state_change::RSSI) && rssi_sensor_ != nullptr)
      rssi_sensor_->publish_state(static_cast<float>(pub.rssi_rounded));
#endif
#ifdef USE_TEXT_SENSOR
    if ((changes & state_change::STATE_STRING) && status_sensor_ != nullptr)
      status_sensor_->publish_state(pub.state_string);
#endif
  }

  NvsDeviceConfig cfg_{};
  DeviceRegistry *registry_{nullptr};
  Device *device_{nullptr};
  light::LightState *light_state_{nullptr};
  uint32_t last_published_ms_{0};
  bool ignore_write_state_{false};
  int slot_index_{-1};  ///< -1 = native mode, >=0 = NVS mode

#ifdef USE_SENSOR
  sensor::Sensor *rssi_sensor_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *status_sensor_{nullptr};
#endif
#ifdef USE_BUTTON
  RefreshButton *refresh_button_{nullptr};
#endif
};

}  // namespace elero
}  // namespace esphome

#endif  // USE_LIGHT
