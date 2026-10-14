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

  // ── Binding (called by NvsAdapter at construction time) ──
  void set_registry(DeviceRegistry *r) { registry_ = r; }
  void set_device(Device *d) { device_ = d; }
  void set_light_state(light::LightState *s) { light_state_ = s; }

  // ── Sensor setters (published from sync_and_publish via snapshot) ──
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
#ifdef USE_BUTTON
    if (refresh_button_ && device_) {
      refresh_button_->set_device(device_);
      refresh_button_->set_registry(registry_);
    }
#endif
    if (device_ && device_->active) {
      sync_and_publish(state_change::ALL);
    }
    initialized_ = true;
  }

  // ── ESPHome Light interface ────────────────────────────────
  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    const auto &cfg = device_->config;
    auto ctx = light_context(cfg);
    traits.set_supported_color_modes({
        light_sm::supports_brightness(ctx)
            ? light::ColorMode::BRIGHTNESS
            : light::ColorMode::ON_OFF
    });
    return traits;
  }

  // Queue user intent synchronously. Deferred writes are unnecessary for an RF
  // adapter and can echo reports or lose commands when an RF report arrives
  // between LightCall::perform() and the next LightState loop.
  void update_state(light::LightState *state) override {
    if (!initialized_ || !device_ || !device_->is_light() || !registry_) return;

    float brightness;
    state->current_values_as_brightness(&brightness);
    bool is_on = state->current_values.is_on();

    if (!is_on) {
      registry_->command_light(*device_, packet::command::DOWN);
    } else {
      registry_->set_light_brightness(*device_, brightness);
    }
  }

  void write_state(light::LightState *) override {}

  void sync_and_publish(uint16_t changes) {
    if (!device_ || !device_->is_light() || !light_state_) return;
    const auto &pub = std::get<LightDevice>(device_->logic).published;

    if (changes & state_change::BRIGHTNESS) {
      auto ctx = light_context(device_->config);
      const float brightness = light_sm::supports_brightness(ctx)
          ? static_cast<float>(pub.brightness_pct) / PERCENT_SCALE : 1.0f;
      // Reports publish existing registry state directly. They do not schedule
      // output writes, cancel pending user calls, or reset active transformers.
      auto values = light_state_->remote_values;
      values.set_state(pub.is_on);
      if (pub.is_on) values.set_brightness(brightness);
      light_state_->remote_values = values;
      light_state_->current_values = values;
      light_state_->publish_state();
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

  DeviceRegistry *registry_{nullptr};
  Device *device_{nullptr};
  light::LightState *light_state_{nullptr};
  bool initialized_{false};  ///< Ignore LightState's boot restore before binding publishes registry state.

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
