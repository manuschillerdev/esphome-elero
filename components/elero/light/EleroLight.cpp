#include "EleroLight.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

using namespace esphome::light;

static const char *const TAG = "elero.light";

void EleroLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero Light:");
  ESP_LOGCONFIG(TAG, "  Blind Address: 0x%06x", this->command_.blind_addr);
  ESP_LOGCONFIG(TAG, "  Remote Address: 0x%06x", this->command_.remote_addr);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->command_.channel);
  ESP_LOGCONFIG(TAG, "  Hop: 0x%02x", this->command_.hop);
  ESP_LOGCONFIG(TAG, "  pck_inf1: 0x%02x, pck_inf2: 0x%02x",
                this->command_.pck_inf[0], this->command_.pck_inf[1]);
  if (this->dim_duration_ > 0)
    ESP_LOGCONFIG(TAG, "  Dim Duration: %dms", this->dim_duration_);
  ESP_LOGCONFIG(TAG, "  cmd_on: 0x%02x, cmd_off: 0x%02x, cmd_stop: 0x%02x",
                this->command_on_, this->command_off_, this->command_stop_);
  ESP_LOGCONFIG(TAG, "  cmd_dim_up: 0x%02x, cmd_dim_down: 0x%02x",
                this->command_dim_up_, this->command_dim_down_);
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
  this->state_ = state;

  bool new_on = state->current_values.is_on();
  float new_brightness = state->current_values.get_brightness();

  if (!new_on) {
    this->commands_to_send_.push(this->command_off_);
    this->is_on_ = false;
    this->is_dimming_ = false;
    this->brightness_ = 0.0f;
    return;
  }

  // Light should be on
  this->is_on_ = true;

  if (this->dim_duration_ == 0) {
    // No brightness support: just toggle on
    this->commands_to_send_.push(this->command_on_);
    this->brightness_ = 1.0f;
    return;
  }

  // Brightness control via timing
  this->target_brightness_ = new_brightness;
  this->is_dimming_ = false;

  if (new_brightness >= 1.0f) {
    // Full brightness shortcut
    this->commands_to_send_.push(this->command_on_);
    this->brightness_ = 1.0f;
    return;
  }

  if (this->brightness_ < 0.01f) {
    // Currently off; turn on to full first, then dim down
    this->commands_to_send_.push(this->command_on_);
    this->brightness_ = 1.0f;
    // Now fall through and initiate dim-down
  }

  if (new_brightness > this->brightness_ + 0.01f) {
    ESP_LOGD(TAG, "Dimming up 0x%06x from %.2f to %.2f",
             this->command_.blind_addr, this->brightness_, new_brightness);
    this->commands_to_send_.push(this->command_dim_up_);
    this->is_dimming_ = true;
    this->dim_up_ = true;
    this->dimming_start_ = millis();
    this->last_recompute_time_ = millis();
  } else if (new_brightness < this->brightness_ - 0.01f) {
    ESP_LOGD(TAG, "Dimming down 0x%06x from %.2f to %.2f",
             this->command_.blind_addr, this->brightness_, new_brightness);
    this->commands_to_send_.push(this->command_dim_down_);
    this->is_dimming_ = true;
    this->dim_up_ = false;
    this->dimming_start_ = millis();
    this->last_recompute_time_ = millis();
  }
  // If within tolerance: no action needed, current level is already correct
}

void EleroLight::loop() {
  const uint32_t now = millis();

  this->handle_commands(now);

  if (this->is_dimming_ && this->dim_duration_ > 0) {
    this->recompute_brightness();

    bool at_target;
    if (this->dim_up_) {
      at_target = this->brightness_ >= this->target_brightness_;
    } else {
      at_target = this->brightness_ <= this->target_brightness_;
    }

    if (at_target) {
      this->commands_to_send_.push(this->command_stop_);
      this->brightness_ = this->target_brightness_;
      this->is_dimming_ = false;
    }

    // Publish estimated brightness every second while dimming
    if (now - this->last_publish_ > 1000) {
      if (this->state_ != nullptr)
        this->state_->publish_state();
      this->last_publish_ = now;
    }
  }
}

void EleroLight::handle_commands(uint32_t now) {
  if ((now - this->last_command_) > ELERO_DELAY_SEND_PACKETS) {
    if (!this->commands_to_send_.empty()) {
      this->command_.payload[8] = this->commands_to_send_.front();
      if (this->parent_->send_command(&this->command_)) {
        this->send_packets_++;
        this->send_retries_ = 0;
        if (this->send_packets_ >= ELERO_SEND_PACKETS) {
          this->commands_to_send_.pop();
          this->send_packets_ = 0;
          this->increase_counter();
        }
      } else {
        ESP_LOGD(TAG, "Retry #%d for light 0x%06x",
                 this->send_retries_, this->command_.blind_addr);
        this->send_retries_++;
        if (this->send_retries_ > ELERO_SEND_RETRIES) {
          ESP_LOGE(TAG, "Hit maximum retries for light 0x%06x, giving up.",
                   this->command_.blind_addr);
          this->send_retries_ = 0;
          this->commands_to_send_.pop();
        }
      }
      this->last_command_ = now;
    }
  }
}

void EleroLight::recompute_brightness() {
  if (!this->is_dimming_)
    return;

  const uint32_t now = millis();
  float dir = this->dim_up_ ? 1.0f : -1.0f;
  this->brightness_ += dir * (float)(now - this->last_recompute_time_) / (float)this->dim_duration_;
  this->brightness_ = clamp(this->brightness_, 0.0f, 1.0f);
  this->last_recompute_time_ = now;
}

void EleroLight::set_rx_state(uint8_t state) {
  ESP_LOGV(TAG, "Got state: 0x%02x for light 0x%06x",
           state, this->command_.blind_addr);

  if (state == ELERO_STATE_ON) {
    if (!this->is_on_) {
      this->is_on_ = true;
      this->brightness_ = 1.0f;
      if (this->state_ != nullptr) {
        auto call = this->state_->make_call();
        call.set_state(true);
        if (this->dim_duration_ > 0)
          call.set_brightness(1.0f);
        call.perform();
      }
    }
  } else if (state == ELERO_STATE_OFF) {
    if (this->is_on_) {
      this->is_on_ = false;
      this->brightness_ = 0.0f;
      if (this->state_ != nullptr) {
        auto call = this->state_->make_call();
        call.set_state(false);
        call.perform();
      }
    }
  }
}

void EleroLight::increase_counter() {
  if (this->command_.counter == 0xff)
    this->command_.counter = 1;
  else
    this->command_.counter += 1;
}

}  // namespace elero
}  // namespace esphome
