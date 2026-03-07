#include "EleroDynamicCover.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

// ─── DynamicEntityBase hooks ───

void EleroDynamicCover::reset_entity_state_() {
  position_ = 0.5f;
  tilt_ = 0.0f;
  current_operation_ = Operation::IDLE;
}

// ─── State handling ───

void EleroDynamicCover::set_rx_state(uint8_t state) {
  last_state_raw_ = state;
  auto result = packet::map_cover_state(state);

  if (result.position >= 0.0f) {
    position_ = result.position;
  }
  tilt_ = result.tilt;

  switch (result.operation) {
    case packet::CoverOp::OPENING:
      if (current_operation_ != Operation::OPENING) {
        current_operation_ = Operation::OPENING;
        movement_start_ms_ = millis();
        movement_start_pos_ = position_;
      }
      break;
    case packet::CoverOp::CLOSING:
      if (current_operation_ != Operation::CLOSING) {
        current_operation_ = Operation::CLOSING;
        movement_start_ms_ = millis();
        movement_start_pos_ = position_;
      }
      break;
    case packet::CoverOp::IDLE:
    default:
      current_operation_ = Operation::IDLE;
      break;
  }

  if (result.is_warning) {
    ESP_LOGW(entity_tag_(), "Cover 0x%06x: %s", config_.dst_address, result.warning_msg);
  }

  publish_state_();
}

const char *EleroDynamicCover::get_operation_str() const {
  switch (current_operation_) {
    case Operation::OPENING: return "opening";
    case Operation::CLOSING: return "closing";
    default: return "idle";
  }
}

bool EleroDynamicCover::perform_action(const char *action) {
  uint8_t cmd = elero_action_to_command(action);
  if (cmd == packet::command::INVALID) return false;
  (void)sender_.enqueue(cmd);
  return true;
}

void EleroDynamicCover::schedule_immediate_poll() {
  immediate_poll_ = true;
}

void EleroDynamicCover::apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) {
  if (open_dur_ms != 0)
    config_.open_duration_ms = open_dur_ms;
  if (close_dur_ms != 0)
    config_.close_duration_ms = close_dur_ms;
  if (poll_intvl_ms != 0)
    config_.poll_interval_ms = poll_intvl_ms;
}

// ─── Loop ───

void EleroDynamicCover::loop(uint32_t now) {
  if (!active_ || parent_ == nullptr || !registered_) return;

  // Process command queue (handles retries, timing, non-blocking TX)
  sender_.process_queue(now, parent_, entity_tag_());

  // Position estimation during movement
  if (current_operation_ != Operation::IDLE && movement_start_ms_ > 0) {
    uint32_t elapsed = now - movement_start_ms_;
    uint32_t duration = (current_operation_ == Operation::OPENING)
                            ? config_.open_duration_ms
                            : config_.close_duration_ms;
    if (duration > 0) {
      float delta = static_cast<float>(elapsed) / static_cast<float>(duration);
      if (current_operation_ == Operation::OPENING) {
        position_ = std::min(1.0f, movement_start_pos_ + delta);
      } else {
        position_ = std::max(0.0f, movement_start_pos_ - delta);
      }
    }

    // Timeout
    if (elapsed > packet::timing::TIMEOUT_MOVEMENT) {
      ESP_LOGW(entity_tag_(), "Movement timeout for 0x%06x", config_.dst_address);
      current_operation_ = Operation::IDLE;
      publish_state_();
    }
  }

  // Periodic polling
  uint32_t poll_interval = (current_operation_ != Operation::IDLE)
                               ? packet::timing::POLL_INTERVAL_MOVING
                               : config_.poll_interval_ms;
  bool should_poll = immediate_poll_ ||
                     (poll_interval > 0 && poll_interval < UINT32_MAX &&
                      (now - last_poll_ms_ >= poll_interval + poll_offset_));
  if (should_poll) {
    (void)sender_.enqueue(packet::command::CHECK);
    last_poll_ms_ = now;
    immediate_poll_ = false;
  }
}

void EleroDynamicCover::publish_state_() {
  if (state_callback_) {
    state_callback_(this);
  }
}

}  // namespace elero
}  // namespace esphome
