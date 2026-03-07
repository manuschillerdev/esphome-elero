#include "NativeNvsLight.h"
#include "esphome/core/log.h"
#include "../elero/elero_packet.h"

namespace esphome {
namespace elero {

using namespace esphome::light;

static const char *const TAG = "elero.nvs_light";

void NativeNvsLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero NVS Light:");
  ESP_LOGCONFIG(TAG, "  dst_address: 0x%06x", config_.dst_address);
  ESP_LOGCONFIG(TAG, "  src_address: 0x%06x", config_.src_address);
  ESP_LOGCONFIG(TAG, "  channel: %d", config_.channel);
  if (core_.config.dim_duration_ms > 0)
    ESP_LOGCONFIG(TAG, "  Dim Duration: %dms", core_.config.dim_duration_ms);
}

void NativeNvsLight::sync_config_to_core() {
  core_.sync_from_nvs_config(config_);
}

void NativeNvsLight::apply_name_from_config() {
  // Name is set when LightState is created by the device manager
}

LightTraits NativeNvsLight::get_traits() {
  auto traits = LightTraits();
  if (core_.supports_brightness()) {
    traits.set_supported_color_modes({ColorMode::BRIGHTNESS});
  } else {
    traits.set_supported_color_modes({ColorMode::ON_OFF});
  }
  return traits;
}

void NativeNvsLight::write_state(LightState *state) {
  if (ignore_write_state_) return;
  light_state_ = state;

  bool new_on = state->current_values.is_on();
  float new_brightness = state->current_values.get_brightness();

  auto action = core_.compute_write_action(new_on, new_brightness);

  if (action.command != 0) {
    sender_.enqueue(action.command);
  }

  uint32_t now = millis();
  core_.apply_write_action(new_on, new_brightness, action, now);

  if (new_on && new_brightness < 1.0f - BRIGHTNESS_EPSILON && !action.start_dimming && core_.brightness >= 1.0f) {
    auto dim_action = core_.compute_write_action(true, new_brightness);
    if (dim_action.command != 0) {
      sender_.enqueue(dim_action.command);
    }
    core_.apply_write_action(true, new_brightness, dim_action, now);
  }
}

void NativeNvsLight::loop() {
  if (!active_ || parent_ == nullptr || !registered_) return;

  uint32_t now = millis();
  sender_.process_queue(now, parent_, TAG);

  if (core_.dim_direction != DimDirection::NONE && core_.supports_brightness()) {
    core_.recompute_brightness(now);

    if (core_.is_at_target()) {
      sender_.enqueue(core_.command_stop);
      core_.brightness = core_.target_brightness;
      core_.dim_direction = DimDirection::NONE;
      if (light_state_ != nullptr)
        light_state_->publish_state();
    }

    if (now - last_publish_ > packet::timing::PUBLISH_THROTTLE_MS) {
      if (light_state_ != nullptr)
        light_state_->publish_state();
      last_publish_ = now;
    }
  }
}

void NativeNvsLight::set_rx_state(uint8_t state) {
  last_state_raw_ = state;
  bool changed = core_.on_rx_state(state);
  if (changed && light_state_ != nullptr) {
    ignore_write_state_ = true;
    auto call = light_state_->make_call();
    call.set_state(core_.is_on);
    if (core_.supports_brightness() && core_.is_on)
      call.set_brightness(core_.brightness);
    call.perform();
    ignore_write_state_ = false;
  }
}

bool NativeNvsLight::perform_action(const char *action) {
  if (strcmp(action, action::ON) == 0 || strcmp(action, action::UP) == 0) {
    if (light_state_ != nullptr) {
      auto call = light_state_->make_call();
      call.set_state(true);
      if (core_.supports_brightness())
        call.set_brightness(1.0f);
      call.perform();
    } else {
      sender_.enqueue(core_.command_on);
    }
    return true;
  }
  if (strcmp(action, action::OFF) == 0 || strcmp(action, action::DOWN) == 0) {
    if (light_state_ != nullptr) {
      auto call = light_state_->make_call();
      call.set_state(false);
      call.perform();
    } else {
      sender_.enqueue(core_.command_off);
    }
    return true;
  }
  if (strcmp(action, action::STOP) == 0) {
    sender_.enqueue(core_.command_stop);
    return true;
  }
  if (strcmp(action, action::DIM_UP) == 0) {
    sender_.enqueue(core_.command_dim_up);
    return true;
  }
  if (strcmp(action, action::DIM_DOWN) == 0) {
    sender_.enqueue(core_.command_dim_down);
    return true;
  }
  if (strcmp(action, action::CHECK) == 0) {
    sender_.enqueue(packet::command::CHECK);
    return true;
  }
  return false;
}

}  // namespace elero
}  // namespace esphome
