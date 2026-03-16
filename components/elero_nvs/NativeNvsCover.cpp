#include "NativeNvsCover.h"
#include "esphome/core/log.h"
#include "../elero/elero_packet.h"
#include "../elero/elero_strings.h"

namespace esphome {
namespace elero {

using namespace esphome::cover;

static const char *const TAG = "elero.nvs_cover";

void NativeNvsCover::dump_config() {
  LOG_COVER("", "Elero NVS Cover", this);
  ESP_LOGCONFIG(TAG, "  dst_address: 0x%06x", config_.dst_address);
  ESP_LOGCONFIG(TAG, "  src_address: 0x%06x", config_.src_address);
  ESP_LOGCONFIG(TAG, "  channel: %d", config_.channel);
}

void NativeNvsCover::sync_config_to_core() {
  core_.sync_from_nvs_config(config_);
}

void NativeNvsCover::apply_name_from_config() {
  if (config_.name[0] != '\0') {
    this->set_name(config_.name);
  }
}

void NativeNvsCover::loop() {
  if (!active_ || parent_ == nullptr || !registered_) return;

  uint32_t now = millis();

  // Movement timeout
  if (core_.check_movement_timeout(now)) {
    ESP_LOGW(TAG, "Movement timeout for 0x%06x", config_.dst_address);
    this->current_operation = COVER_OPERATION_IDLE;
    this->publish_state();
  }

  // Polling
  if (core_.should_poll(now)) {
    if (sender_.enqueue(packet::command::CHECK)) {
      core_.mark_polled(now);
      core_.immediate_poll = false;
    }
  }

  sender_.process_queue(now, parent_, TAG);

  // Position tracking
  if (core_.operation != CoverCore::Operation::IDLE && core_.has_position_tracking()) {
    core_.recompute_position(now);
    this->position = core_.position;

    if (core_.is_at_target()) {
      if (sender_.enqueue(packet::command::STOP)) {
        core_.stop_movement(now);
        core_.target_position = cover_pos::FULLY_OPEN;
        this->current_operation = COVER_OPERATION_IDLE;
      }
    }

    if (now - last_publish_ > packet::timing::PUBLISH_THROTTLE_MS) {
      this->publish_state(false);
      last_publish_ = now;
    }
  }
}

CoverTraits NativeNvsCover::get_traits() {
  auto traits = CoverTraits();
  traits.set_supports_stop(true);
  traits.set_supports_position(core_.has_position_tracking());
  traits.set_supports_toggle(true);
  traits.set_is_assumed_state(true);
  traits.set_supports_tilt(core_.config.supports_tilt);
  return traits;
}

void NativeNvsCover::set_rx_state(uint8_t state) {
  last_state_raw_ = state;
  ESP_LOGV(TAG, "Got state: 0x%02x (%s) for blind 0x%06x", state, elero_state_to_string(state),
           config_.dst_address);

  uint32_t now = millis();
  auto result = core_.on_rx_state(state, now);

  if (result.is_warning) {
    ESP_LOGW(TAG, "Blind 0x%06x reports %s", config_.dst_address, result.warning_msg);
  }

  if (result.changed) {
    this->position = core_.position;
    this->tilt = core_.tilt;
    this->current_operation = static_cast<CoverOperation>(core_.operation);
    this->publish_state();
  }
}

void NativeNvsCover::control(const CoverCall &call) {
  if (call.get_stop()) {
    start_movement_(COVER_OPERATION_IDLE);
  }
  if (call.get_position().has_value()) {
    auto pos = *call.get_position();
    core_.target_position = pos;
    if ((pos > this->position) || (pos == COVER_OPEN)) {
      start_movement_(COVER_OPERATION_OPENING);
    } else {
      start_movement_(COVER_OPERATION_CLOSING);
    }
  }
  if (call.get_tilt().has_value()) {
    auto tilt_val = *call.get_tilt();
    if (tilt_val > 0) {
      if (sender_.enqueue(packet::command::TILT)) {
        this->tilt = 1.0f;
        core_.tilt = 1.0f;
      }
    } else {
      if (sender_.enqueue(packet::command::DOWN)) {
        this->tilt = 0.0f;
        core_.tilt = 0.0f;
      }
    }
  }
  if (call.get_toggle().has_value()) {
    if (this->current_operation != COVER_OPERATION_IDLE) {
      start_movement_(COVER_OPERATION_IDLE);
    } else {
      if (this->position == COVER_CLOSED || core_.last_direction == CoverCore::Operation::CLOSING) {
        core_.target_position = COVER_OPEN;
        start_movement_(COVER_OPERATION_OPENING);
      } else {
        core_.target_position = COVER_CLOSED;
        start_movement_(COVER_OPERATION_CLOSING);
      }
    }
  }
}

bool NativeNvsCover::perform_command(uint8_t cmd) {
  auto op = CoverCore::command_to_operation(cmd);

  if (op == CoverCore::Operation::OPENING) {
    this->start_movement_(COVER_OPERATION_OPENING);
  } else if (op == CoverCore::Operation::CLOSING) {
    this->start_movement_(COVER_OPERATION_CLOSING);
  } else if (cmd == packet::command::STOP) {
    this->start_movement_(COVER_OPERATION_IDLE);
  } else {
    sender_.enqueue(cmd);
  }

  this->position = core_.position;
  this->tilt = core_.tilt;
  this->current_operation = static_cast<CoverOperation>(core_.operation);
  this->publish_state();
  return true;
}

bool NativeNvsCover::perform_action(const char *action) {
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
    return perform_command(packet::command::CHECK);
  }
  return false;
}

void NativeNvsCover::apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) {
  core_.apply_runtime_settings(open_dur_ms, close_dur_ms, poll_intvl_ms);
  if (open_dur_ms != 0) config_.open_duration_ms = open_dur_ms;
  if (close_dur_ms != 0) config_.close_duration_ms = close_dur_ms;
  if (poll_intvl_ms != 0) config_.poll_interval_ms = poll_intvl_ms;
}

void NativeNvsCover::on_remote_command(uint8_t command_byte) {
  auto op = CoverCore::command_to_operation(command_byte);
  uint32_t now = millis();
  if (op == CoverCore::Operation::OPENING || op == CoverCore::Operation::CLOSING) {
    core_.start_movement(op, now);
  } else {
    core_.stop_movement(now);
  }
  this->position = core_.position;
  this->tilt = core_.tilt;
  this->current_operation = static_cast<CoverOperation>(core_.operation);
  this->publish_state();
}

void NativeNvsCover::start_movement_(CoverOperation dir) {
  uint32_t now = millis();

  switch (dir) {
    case COVER_OPERATION_OPENING:
      if (sender_.enqueue(packet::command::UP)) {
        core_.start_movement(CoverCore::Operation::OPENING, now);
      }
      break;
    case COVER_OPERATION_CLOSING:
      if (sender_.enqueue(packet::command::DOWN)) {
        core_.start_movement(CoverCore::Operation::CLOSING, now);
      }
      break;
    case COVER_OPERATION_IDLE:
      if (sender_.state() != CommandSender::State::TX_PENDING ||
          sender_.command().payload[4] != packet::command::STOP) {
        sender_.clear_queue();
      }
      sender_.enqueue(packet::command::STOP);
      core_.stop_movement(now);
      break;
  }

  this->position = core_.position;
  this->tilt = core_.tilt;
  this->current_operation = static_cast<CoverOperation>(core_.operation);
  this->publish_state();
}

}  // namespace elero
}  // namespace esphome
