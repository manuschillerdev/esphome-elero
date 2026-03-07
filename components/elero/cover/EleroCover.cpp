#include "EleroCover.h"
#include "esphome/core/log.h"
#include "../elero_packet.h"

namespace esphome {
namespace elero {

using namespace esphome::cover;

static const char *const TAG = "elero.cover";

void EleroCover::dump_config() {
  LOG_COVER("", "Elero Cover", this);
  ESP_LOGCONFIG(TAG, "  dst_address: 0x%06x", this->sender_.command().dst_addr);
  ESP_LOGCONFIG(TAG, "  src_address: 0x%06x", this->sender_.command().src_addr);
  ESP_LOGCONFIG(TAG, "  channel: %d", this->sender_.command().channel);
  ESP_LOGCONFIG(TAG, "  hop: 0x%02x", this->sender_.command().hop);
  ESP_LOGCONFIG(TAG, "  type: 0x%02x, type2: 0x%02x", this->sender_.command().type,
                this->sender_.command().type2);
  if (this->core_.config.open_duration_ms > 0)
    ESP_LOGCONFIG(TAG, "  Open Duration: %dms", this->core_.config.open_duration_ms);
  if (this->core_.config.close_duration_ms > 0)
    ESP_LOGCONFIG(TAG, "  Close Duration: %dms", this->core_.config.close_duration_ms);
  ESP_LOGCONFIG(TAG, "  Poll Interval: %dms", this->core_.config.poll_interval_ms);
  ESP_LOGCONFIG(TAG, "  Supports Tilt: %s", YESNO(this->core_.config.supports_tilt));
}

void EleroCover::setup() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "Elero parent not configured");
    this->mark_failed();
    return;
  }
  this->parent_->register_cover(this);

  // Stagger first poll by poll_offset; subsequent polls use plain interval
  this->core_.last_poll_ms = 0 - this->core_.config.poll_interval_ms + this->core_.config.poll_offset;

  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(this);
  } else {
    if (this->core_.has_position_tracking())
      this->position = cover_pos::UNKNOWN;
  }

  // After restore, force operation to IDLE. We can't know if the blind is
  // actually moving after a reboot - the first poll will tell us the real state.
  this->current_operation = COVER_OPERATION_IDLE;
  this->core_.operation = CoverCore::Operation::IDLE;
  this->core_.movement_start_ms = 0;
  this->core_.position = this->position;
}

void EleroCover::loop() {
  uint32_t now = millis();

  // Check movement timeout
  if (this->core_.check_movement_timeout(now)) {
    ESP_LOGW(TAG, "Movement timeout for 0x%06x, resetting to IDLE", this->sender_.command().dst_addr);
    this->sync_from_core_();
    this->publish_state();
  }

  // Polling
  if (this->core_.should_poll(now)) {
    if (this->sender_.enqueue(this->command_check_)) {
      this->core_.mark_polled(now);
      this->core_.immediate_poll = false;
    }
  }

  this->sender_.process_queue(now, this->parent_, TAG);

  // Position tracking during movement
  if (this->core_.operation != CoverCore::Operation::IDLE && this->core_.has_position_tracking()) {
    this->core_.recompute_position(now);
    this->position = this->core_.position;

    if (this->core_.is_at_target()) {
      if (this->sender_.enqueue(this->command_stop_)) {
        this->core_.operation = CoverCore::Operation::IDLE;
        this->core_.target_position = cover_pos::FULLY_OPEN;
        this->sync_from_core_();
      }
    }

    // Publish position every second
    if (now - this->last_publish_ > packet::timing::PUBLISH_THROTTLE_MS) {
      this->publish_state(false);
      this->last_publish_ = now;
    }
  }
}

float EleroCover::get_setup_priority() const {
  return setup_priority::DATA;
}

cover::CoverTraits EleroCover::get_traits() {
  auto traits = cover::CoverTraits();
  traits.set_supports_stop(true);
  traits.set_supports_position(this->core_.has_position_tracking());
  traits.set_supports_toggle(true);
  traits.set_is_assumed_state(true);
  traits.set_supports_tilt(this->core_.config.supports_tilt);
  return traits;
}

void EleroCover::set_rx_state(uint8_t state) {
  this->last_state_raw_ = state;
  ESP_LOGV(TAG, "Got state: 0x%02x (%s) for blind 0x%06x", state, elero_state_to_string(state),
           this->sender_.command().dst_addr);

  uint32_t now = millis();
  auto result = this->core_.on_rx_state(state, now);

  if (result.is_warning) {
    ESP_LOGW(TAG, "Blind 0x%06x reports %s", this->sender_.command().dst_addr, result.warning_msg);
  }

  if (result.changed) {
    this->sync_from_core_();
    this->publish_state();
  }
}

void EleroCover::control(const cover::CoverCall &call) {
  if (call.get_stop()) {
    this->start_movement(COVER_OPERATION_IDLE);
  }
  if (call.get_position().has_value()) {
    auto pos = *call.get_position();
    this->core_.target_position = pos;
    if ((pos > this->position) || (pos == COVER_OPEN)) {
      this->start_movement(COVER_OPERATION_OPENING);
    } else {
      this->start_movement(COVER_OPERATION_CLOSING);
    }
  }
  if (call.get_tilt().has_value()) {
    auto tilt_val = *call.get_tilt();
    if (tilt_val > 0) {
      if (this->sender_.enqueue(this->command_tilt_)) {
        this->tilt = 1.0f;
        this->core_.tilt = 1.0f;
      }
    } else {
      if (this->sender_.enqueue(this->command_down_)) {
        this->tilt = 0.0f;
        this->core_.tilt = 0.0f;
      }
    }
  }
  if (call.get_toggle().has_value()) {
    if (this->current_operation != COVER_OPERATION_IDLE) {
      this->start_movement(COVER_OPERATION_IDLE);
    } else {
      if (this->position == COVER_CLOSED || this->core_.last_direction == CoverCore::Operation::CLOSING) {
        this->core_.target_position = COVER_OPEN;
        this->start_movement(COVER_OPERATION_OPENING);
      } else {
        this->core_.target_position = COVER_CLOSED;
        this->start_movement(COVER_OPERATION_CLOSING);
      }
    }
  }
}

bool EleroCover::perform_action(const char *action) {
  if (strcmp(action, action::UP) == 0 || strcmp(action, action::OPEN) == 0) {
    this->make_call().set_command_open().perform();
    return true;
  }
  if (strcmp(action, action::DOWN) == 0 || strcmp(action, action::CLOSE) == 0) {
    this->make_call().set_command_close().perform();
    return true;
  }
  if (strcmp(action, action::STOP) == 0) {
    this->make_call().set_command_stop().perform();
    return true;
  }
  if (strcmp(action, action::TILT) == 0) {
    this->make_call().set_tilt(1.0f).perform();
    return true;
  }
  if (strcmp(action, action::CHECK) == 0) {
    if (!this->sender_.enqueue(this->command_check_)) {
      ESP_LOGW(TAG, "Command queue full for cover 0x%06x", this->sender_.command().dst_addr);
    }
    return true;
  }
  return false;
}

void EleroCover::start_movement(CoverOperation dir) {
  uint32_t now = millis();

  switch (dir) {
    case COVER_OPERATION_OPENING:
      ESP_LOGV(TAG, "Sending OPEN command");
      if (this->sender_.enqueue(this->command_up_)) {
        this->core_.start_movement(CoverCore::Operation::OPENING, now);
      }
      break;
    case COVER_OPERATION_CLOSING:
      ESP_LOGV(TAG, "Sending CLOSE command");
      if (this->sender_.enqueue(this->command_down_)) {
        this->core_.start_movement(CoverCore::Operation::CLOSING, now);
      }
      break;
    case COVER_OPERATION_IDLE:
      this->sender_.clear_queue();
      if (!this->sender_.enqueue(this->command_stop_)) {
        ESP_LOGW(TAG, "Command queue full for cover 0x%06x", this->sender_.command().dst_addr);
      }
      this->core_.operation = CoverCore::Operation::IDLE;
      break;
  }

  this->sync_from_core_();
  this->publish_state();
}

void EleroCover::schedule_immediate_poll() {
  this->core_.immediate_poll = true;
}

void EleroCover::sync_from_core_() {
  this->position = this->core_.position;
  this->tilt = this->core_.tilt;
  this->current_operation = static_cast<CoverOperation>(this->core_.operation);
}

}  // namespace elero
}  // namespace esphome
