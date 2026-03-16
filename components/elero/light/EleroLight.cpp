#include "EleroLight.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

using namespace esphome::light;

static const char *const TAG = "elero.light";

void EleroLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero Light:");
  ESP_LOGCONFIG(TAG, "  dst_address: 0x%06x", this->sender_.command().dst_addr);
  ESP_LOGCONFIG(TAG, "  src_address: 0x%06x", this->sender_.command().src_addr);
  ESP_LOGCONFIG(TAG, "  channel: %d", this->sender_.command().channel);
  ESP_LOGCONFIG(TAG, "  hop: 0x%02x", this->sender_.command().hop);
  ESP_LOGCONFIG(TAG, "  type: 0x%02x, type2: 0x%02x", this->sender_.command().type,
                this->sender_.command().type2);
  if (this->core_.config.dim_duration_ms > 0)
    ESP_LOGCONFIG(TAG, "  Dim Duration: %dms", this->core_.config.dim_duration_ms);
  ESP_LOGCONFIG(TAG, "  cmd_on: 0x%02x, cmd_off: 0x%02x, cmd_stop: 0x%02x",
                this->core_.command_on, this->core_.command_off, this->core_.command_stop);
  ESP_LOGCONFIG(TAG, "  cmd_dim_up: 0x%02x, cmd_dim_down: 0x%02x",
                this->core_.command_dim_up, this->core_.command_dim_down);
}

void EleroLight::setup() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "Elero parent not configured");
    this->mark_failed();
    return;
  }
  this->parent_->register_light(this);
}

LightTraits EleroLight::get_traits() {
  auto traits = LightTraits();
  if (this->core_.supports_brightness()) {
    traits.set_supported_color_modes({ColorMode::BRIGHTNESS});
  } else {
    traits.set_supported_color_modes({ColorMode::ON_OFF});
  }
  return traits;
}

void EleroLight::write_state(LightState *state) {
  if (this->ignore_write_state_)
    return;
  this->state_ = state;

  bool new_on = state->current_values.is_on();
  float new_brightness = state->current_values.get_brightness();

  auto action = this->core_.compute_write_action(new_on, new_brightness);

  if (action.command != 0) {
    if (!this->sender_.enqueue(action.command)) {
      ESP_LOGW(TAG, "Command queue full for light 0x%06x", this->sender_.command().dst_addr);
    }
  }

  if (action.start_dimming) {
    ESP_LOGD(TAG, "Dimming %s 0x%06x from %.2f to %.2f",
             action.dim_dir == DimDirection::UP ? "up" : "down",
             this->sender_.command().dst_addr, this->core_.brightness, new_brightness);
  }

  uint32_t now = millis();
  this->core_.apply_write_action(new_on, new_brightness, action, now);

  // If we just turned on from off and target < 1.0, we need a second pass for dimming
  if (new_on && new_brightness < 1.0f - BRIGHTNESS_EPSILON && !action.start_dimming && this->core_.brightness >= 1.0f) {
    auto dim_action = this->core_.compute_write_action(true, new_brightness);
    if (dim_action.command != 0) {
      if (!this->sender_.enqueue(dim_action.command)) {
        ESP_LOGW(TAG, "Command queue full for light 0x%06x", this->sender_.command().dst_addr);
      }
    }
    this->core_.apply_write_action(true, new_brightness, dim_action, now);
  }
}

void EleroLight::loop() {
  const uint32_t now = millis();

  this->sender_.process_queue(now, this->parent_, TAG);

  if (this->core_.dim_direction != DimDirection::NONE && this->core_.supports_brightness()) {
    this->core_.recompute_brightness(now);

    if (this->core_.is_at_target()) {
      if (!this->sender_.enqueue(this->core_.command_stop)) {
        ESP_LOGW(TAG, "Command queue full for light 0x%06x", this->sender_.command().dst_addr);
      }
      this->core_.brightness = this->core_.target_brightness;
      this->core_.dim_direction = DimDirection::NONE;
      if (this->state_ != nullptr)
        this->state_->publish_state();
    }

    // Publish estimated brightness every second while dimming
    if (now - this->last_publish_ > packet::timing::PUBLISH_THROTTLE_MS) {
      if (this->state_ != nullptr)
        this->state_->publish_state();
      this->last_publish_ = now;
    }
  }
}

bool EleroLight::perform_command(uint8_t cmd) {
  bool changed = false;

  if (cmd == this->core_.command_on) {
    (void)this->sender_.enqueue(cmd);
    this->core_.is_on = true;
    this->core_.brightness = 1.0f;
    changed = true;
  } else if (cmd == this->core_.command_off) {
    (void)this->sender_.enqueue(cmd);
    this->core_.turn_off();
    changed = true;
  } else if (cmd == this->core_.command_stop) {
    (void)this->sender_.enqueue(cmd);
    this->core_.dim_direction = DimDirection::NONE;
    changed = true;
  } else if (cmd == this->core_.command_dim_up || cmd == this->core_.command_dim_down) {
    (void)this->sender_.enqueue(cmd);
  } else {
    (void)this->sender_.enqueue(cmd);
  }

  if (changed && this->state_ != nullptr) {
    this->state_->current_values.set_state(this->core_.is_on);
    if (this->core_.supports_brightness()) {
      this->state_->current_values.set_brightness(this->core_.brightness);
    }
    this->state_->publish_state();
  }
  return true;
}

void EleroLight::schedule_immediate_poll() {
  if (!this->sender_.enqueue(this->command_check_)) {
    ESP_LOGW(TAG, "Command queue full for light 0x%06x", this->sender_.command().dst_addr);
  }
}

bool EleroLight::perform_action(const char *action) {
  if (strcmp(action, action::ON) == 0 || strcmp(action, action::UP) == 0) {
    if (this->state_ != nullptr) {
      auto call = this->state_->make_call();
      call.set_state(true);
      if (this->core_.supports_brightness())
        call.set_brightness(1.0f);
      call.perform();
    } else {
      this->sender_.enqueue(this->core_.command_on);
    }
    return true;
  }
  if (strcmp(action, action::OFF) == 0 || strcmp(action, action::DOWN) == 0) {
    if (this->state_ != nullptr) {
      auto call = this->state_->make_call();
      call.set_state(false);
      call.perform();
    } else {
      this->sender_.enqueue(this->core_.command_off);
    }
    return true;
  }
  if (strcmp(action, action::STOP) == 0) {
    this->sender_.enqueue(this->core_.command_stop);
    return true;
  }
  if (strcmp(action, action::DIM_UP) == 0) {
    this->sender_.enqueue(this->core_.command_dim_up);
    return true;
  }
  if (strcmp(action, action::DIM_DOWN) == 0) {
    this->sender_.enqueue(this->core_.command_dim_down);
    return true;
  }
  if (strcmp(action, action::CHECK) == 0) {
    this->sender_.enqueue(this->command_check_);
    return true;
  }
  return false;
}

void EleroLight::set_rx_state(uint8_t state) {
  ESP_LOGV(TAG, "Got state: 0x%02x for light 0x%06x", state, this->sender_.command().dst_addr);

  bool changed = this->core_.on_rx_state(state);
  if (changed && this->state_ != nullptr) {
    this->ignore_write_state_ = true;
    auto call = this->state_->make_call();
    call.set_state(this->core_.is_on);
    if (this->core_.supports_brightness() && this->core_.is_on)
      call.set_brightness(this->core_.brightness);
    call.perform();
    this->ignore_write_state_ = false;
  }
}

}  // namespace elero
}  // namespace esphome
