#include "EleroLight.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

using namespace esphome::light;

static const char *const TAG = "elero.light";

void EleroLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero Light:");
  ESP_LOGCONFIG(TAG, "  Blind Address: 0x%06x", this->sender_.command().blind_addr);
  ESP_LOGCONFIG(TAG, "  Remote Address: 0x%06x", this->sender_.command().remote_addr);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->sender_.command().channel);
  ESP_LOGCONFIG(TAG, "  Hop: 0x%02x", this->sender_.command().hop);
  ESP_LOGCONFIG(TAG, "  pck_inf1: 0x%02x, pck_inf2: 0x%02x", this->sender_.command().pck_inf[0],
                this->sender_.command().pck_inf[1]);
  if (this->dim_duration_ > 0)
    ESP_LOGCONFIG(TAG, "  Dim Duration: %dms", this->dim_duration_);
  ESP_LOGCONFIG(TAG, "  cmd_on: 0x%02x, cmd_off: 0x%02x, cmd_stop: 0x%02x", this->command_on_, this->command_off_,
                this->command_stop_);
  ESP_LOGCONFIG(TAG, "  cmd_dim_up: 0x%02x, cmd_dim_down: 0x%02x", this->command_dim_up_, this->command_dim_down_);
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
  if (this->dim_duration_ > 0) {
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

  if (!new_on) {
    this->sender_.enqueue(this->command_off_);
    this->is_on_ = false;
    this->dim_direction_ = DimDirection::NONE;
    this->brightness_ = 0.0f;
    return;
  }

  // Light should be on
  this->is_on_ = true;

  if (this->dim_duration_ == 0) {
    // No brightness support: just toggle on
    this->sender_.enqueue(this->command_on_);
    this->brightness_ = 1.0f;
    return;
  }

  // Brightness control via timing
  this->target_brightness_ = new_brightness;
  this->dim_direction_ = DimDirection::NONE;

  if (new_brightness >= 1.0f) {
    // Full brightness shortcut
    this->sender_.enqueue(this->command_on_);
    this->brightness_ = 1.0f;
    return;
  }

  if (this->brightness_ < 0.01f) {
    // Currently off; turn on to full first, then dim down
    this->sender_.enqueue(this->command_on_);
    this->brightness_ = 1.0f;
    // Now fall through and initiate dim-down
  }

  if (new_brightness > this->brightness_ + 0.01f) {
    ESP_LOGD(TAG, "Dimming up 0x%06x from %.2f to %.2f", this->sender_.command().blind_addr, this->brightness_,
             new_brightness);
    this->sender_.enqueue(this->command_dim_up_);
    this->dim_direction_ = DimDirection::UP;
    this->dimming_start_ = millis();
    this->last_recompute_time_ = millis();
  } else if (new_brightness < this->brightness_ - 0.01f) {
    ESP_LOGD(TAG, "Dimming down 0x%06x from %.2f to %.2f", this->sender_.command().blind_addr, this->brightness_,
             new_brightness);
    this->sender_.enqueue(this->command_dim_down_);
    this->dim_direction_ = DimDirection::DOWN;
    this->dimming_start_ = millis();
    this->last_recompute_time_ = millis();
  }
  // If within tolerance: no action needed, current level is already correct
}

void EleroLight::loop() {
  const uint32_t now = millis();

  this->sender_.process_queue(now, this->parent_, TAG);

  if (this->dim_direction_ != DimDirection::NONE && this->dim_duration_ > 0) {
    this->recompute_brightness();

    bool at_target;
    if (this->dim_direction_ == DimDirection::UP) {
      at_target = this->brightness_ >= this->target_brightness_;
    } else {
      at_target = this->brightness_ <= this->target_brightness_;
    }

    if (at_target) {
      this->sender_.enqueue(this->command_stop_);
      this->brightness_ = this->target_brightness_;
      this->dim_direction_ = DimDirection::NONE;
    }

    // Publish estimated brightness every second while dimming
    if (now - this->last_publish_ > 1000) {
      if (this->state_ != nullptr)
        this->state_->publish_state();
      this->last_publish_ = now;
    }
  }
}

void EleroLight::schedule_immediate_poll() { this->sender_.enqueue(this->command_check_); }

void EleroLight::recompute_brightness() {
  if (this->dim_direction_ == DimDirection::NONE)
    return;

  const uint32_t now = millis();
  float dir = (this->dim_direction_ == DimDirection::UP) ? 1.0f : -1.0f;
  this->brightness_ += dir * static_cast<float>(now - this->last_recompute_time_) / static_cast<float>(this->dim_duration_);
  this->brightness_ = clamp(this->brightness_, 0.0f, 1.0f);
  this->last_recompute_time_ = now;
}

void EleroLight::set_rx_state(uint8_t state) {
  ESP_LOGV(TAG, "Got state: 0x%02x for light 0x%06x", state, this->sender_.command().blind_addr);

  if (state == ELERO_STATE_ON) {
    if (!this->is_on_) {
      this->is_on_ = true;
      this->brightness_ = 1.0f;
      if (this->state_ != nullptr) {
        this->ignore_write_state_ = true;
        auto call = this->state_->make_call();
        call.set_state(true);
        if (this->dim_duration_ > 0)
          call.set_brightness(1.0f);
        call.perform();
        this->ignore_write_state_ = false;
      }
    }
  } else if (state == ELERO_STATE_OFF) {
    if (this->is_on_) {
      this->is_on_ = false;
      this->brightness_ = 0.0f;
      if (this->state_ != nullptr) {
        this->ignore_write_state_ = true;
        auto call = this->state_->make_call();
        call.set_state(false);
        call.perform();
        this->ignore_write_state_ = false;
      }
    }
  }
}

}  // namespace elero
}  // namespace esphome
