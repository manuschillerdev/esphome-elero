#include "EleroDynamicCover.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

// ─── DynamicEntityBase hooks ───

void EleroDynamicCover::reset_entity_state_() {
  core_.reset();
}

// ─── Config sync ───

void EleroDynamicCover::sync_config_to_core() {
  core_.sync_from_nvs_config(config_);
}

// ─── State handling ───

void EleroDynamicCover::set_rx_state(uint8_t state) {
  last_state_raw_ = state;

  uint32_t now = millis();
  auto result = core_.on_rx_state(state, now);

  if (result.is_warning) {
    ESP_LOGW(entity_tag_(), "Cover 0x%06x: %s", config_.dst_address, result.warning_msg);
  }

  if (result.changed) {
    publish_state_();
  }
}

const char *EleroDynamicCover::get_operation_str() const {
  return core_.operation_str();
}

bool EleroDynamicCover::perform_action(const char *action) {
  uint8_t cmd = elero_action_to_command(action);
  if (cmd == packet::command::INVALID) return false;
  (void)sender_.enqueue(cmd);
  return true;
}

void EleroDynamicCover::schedule_immediate_poll() {
  core_.immediate_poll = true;
}

void EleroDynamicCover::on_remote_command(uint8_t command_byte) {
  auto op = CoverCore::command_to_operation(command_byte);
  uint32_t now = millis();
  if (op == CoverCore::Operation::OPENING || op == CoverCore::Operation::CLOSING) {
    core_.start_movement(op, now);
  } else {
    core_.operation = CoverCore::Operation::IDLE;
  }
  publish_state_();
  core_.immediate_poll = true;
}

void EleroDynamicCover::apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) {
  core_.apply_runtime_settings(open_dur_ms, close_dur_ms, poll_intvl_ms);
  if (open_dur_ms != 0) config_.open_duration_ms = open_dur_ms;
  if (close_dur_ms != 0) config_.close_duration_ms = close_dur_ms;
  if (poll_intvl_ms != 0) config_.poll_interval_ms = poll_intvl_ms;
}

// ─── Loop ───

void EleroDynamicCover::loop(uint32_t now) {
  if (!active_ || parent_ == nullptr || !registered_) return;

  // Movement timeout
  if (core_.check_movement_timeout(now)) {
    ESP_LOGW(entity_tag_(), "Movement timeout for 0x%06x", config_.dst_address);
    publish_state_();
  }

  // Periodic polling
  if (core_.should_poll(now)) {
    (void)sender_.enqueue(packet::command::CHECK);
    core_.mark_polled(now);
    core_.immediate_poll = false;
  }

  sender_.process_queue(now, parent_, entity_tag_());

  // Position estimation during movement
  if (core_.operation != CoverCore::Operation::IDLE && core_.has_position_tracking()) {
    core_.recompute_position(now);

    // Stop at intermediate target position
    if (core_.is_at_target()) {
      if (sender_.enqueue(packet::command::STOP)) {
        core_.operation = CoverCore::Operation::IDLE;
        core_.target_position = cover_pos::FULLY_OPEN;
        publish_state_();
      }
    }

    // Publish position every second during movement
    if (now - last_publish_ > packet::timing::PUBLISH_THROTTLE_MS) {
      publish_state_();
      last_publish_ = now;
    }
  }
}

void EleroDynamicCover::publish_state_() {
  if (state_callback_) {
    state_callback_(this);
  }
}

}  // namespace elero
}  // namespace esphome
