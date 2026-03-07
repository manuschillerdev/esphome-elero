#include "EleroDynamicLight.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

// ─── Config sync ───

void EleroDynamicLight::sync_config_to_core() {
  core_.sync_from_nvs_config(config_);
}

// ─── State handling ───

void EleroDynamicLight::set_rx_state(uint8_t state) {
  last_state_raw_ = state;
  if (core_.on_rx_state(state)) {
    publish_state_();
  }
}

void EleroDynamicLight::schedule_immediate_poll() {
  (void)sender_.enqueue(packet::command::CHECK);
}

bool EleroDynamicLight::perform_action(const char *action) {
  if (action == nullptr) return false;
  if (strcmp(action, action::ON) == 0 || strcmp(action, action::UP) == 0) {
    (void)sender_.enqueue(core_.command_on);
    return true;
  }
  if (strcmp(action, action::OFF) == 0 || strcmp(action, action::DOWN) == 0) {
    (void)sender_.enqueue(core_.command_off);
    return true;
  }
  if (strcmp(action, action::STOP) == 0) {
    (void)sender_.enqueue(core_.command_stop);
    return true;
  }
  if (strcmp(action, action::DIM_UP) == 0) {
    (void)sender_.enqueue(core_.command_dim_up);
    return true;
  }
  if (strcmp(action, action::DIM_DOWN) == 0) {
    (void)sender_.enqueue(core_.command_dim_down);
    return true;
  }
  if (strcmp(action, action::CHECK) == 0) {
    (void)sender_.enqueue(packet::command::CHECK);
    return true;
  }
  return false;
}

// ─── Brightness control ───

void EleroDynamicLight::set_brightness(float target, uint32_t now) {
  if (!core_.supports_brightness()) return;

  bool target_on = target > BRIGHTNESS_EPSILON;
  auto action = core_.compute_write_action(target_on, target);
  if (action.command != 0) {
    (void)sender_.enqueue(action.command);
  }
  core_.apply_write_action(target_on, target, action, now);
  publish_state_();
}

// ─── Loop ───

void EleroDynamicLight::loop(uint32_t now) {
  if (!active_ || parent_ == nullptr || !registered_) return;

  sender_.process_queue(now, parent_, entity_tag_());

  // Dimming logic (if brightness control is supported)
  if (core_.dim_direction != DimDirection::NONE && core_.supports_brightness()) {
    core_.recompute_brightness(now);

    if (core_.is_at_target()) {
      (void)sender_.enqueue(core_.command_stop);
      core_.brightness = core_.target_brightness;
      core_.dim_direction = DimDirection::NONE;
      publish_state_();
    }

    // Publish estimated brightness every second while dimming
    if (now - last_publish_ > packet::timing::PUBLISH_THROTTLE_MS) {
      publish_state_();
      last_publish_ = now;
    }
  }
}

void EleroDynamicLight::publish_state_() {
  if (state_callback_) {
    state_callback_(this);
  }
}

}  // namespace elero
}  // namespace esphome
