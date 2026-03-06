#include "EleroDynamicCover.h"
#include "esphome/core/log.h"

namespace esphome::elero {

static constexpr const char *TAG = "elero.dyn_cover";

bool EleroDynamicCover::activate(const NvsDeviceConfig &config, Elero *parent) {
  if (active_) {
    ESP_LOGW(TAG, "Slot already active for 0x%06x", config_.dst_address);
    return false;
  }
  if (!config.is_valid() || !config.is_cover() || parent == nullptr) {
    ESP_LOGE(TAG, "Invalid config or null parent for activation");
    return false;
  }

  config_ = config;
  parent_ = parent;
  parent_->register_cover(this);
  active_ = true;

  ESP_LOGI(TAG, "Activated dynamic cover '%s' at 0x%06x", config_.name, config_.dst_address);
  return true;
}

void EleroDynamicCover::deactivate() {
  if (!active_) return;
  ESP_LOGI(TAG, "Deactivating dynamic cover 0x%06x", config_.dst_address);
  active_ = false;
  parent_ = nullptr;
  config_ = NvsDeviceConfig{};
  position_ = 0.5f;
  tilt_ = 0.0f;
  current_operation_ = Operation::IDLE;
  last_state_raw_ = 0;
  last_seen_ms_ = 0;
  // Clear command queue
  while (!commands_.empty()) commands_.pop();
}

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
    ESP_LOGW(TAG, "Cover 0x%06x: %s", config_.dst_address, result.warning_msg);
  }

  publish_state_();
}

void EleroDynamicCover::notify_rx_meta(uint32_t ms, float rssi) {
  last_seen_ms_ = ms;
  last_rssi_ = rssi;
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
  enqueue_command(cmd);
  return true;
}

void EleroDynamicCover::enqueue_command(uint8_t cmd_byte) {
  if (!active_ || parent_ == nullptr) return;
  if (commands_.size() >= packet::limits::MAX_COMMAND_QUEUE) {
    ESP_LOGW(TAG, "Command queue full for 0x%06x", config_.dst_address);
    return;
  }
  commands_.push(cmd_byte);
}

void EleroDynamicCover::schedule_immediate_poll() {
  immediate_poll_ = true;
}

void EleroDynamicCover::apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) {
  config_.open_duration_ms = open_dur_ms;
  config_.close_duration_ms = close_dur_ms;
  config_.poll_interval_ms = poll_intvl_ms;
}

void EleroDynamicCover::loop(uint32_t now) {
  if (!active_ || parent_ == nullptr) return;

  // Send queued commands
  if (!commands_.empty() && (now - last_command_ms_ >= packet::timing::DELAY_SEND_PACKETS)) {
    send_next_command_();
    last_command_ms_ = now;
  }

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
      ESP_LOGW(TAG, "Movement timeout for 0x%06x", config_.dst_address);
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
    enqueue_command(packet::command::CHECK);
    last_poll_ms_ = now;
    immediate_poll_ = false;
  }
}

void EleroDynamicCover::publish_state_() {
  if (state_callback_) {
    state_callback_(this);
  }
}

void EleroDynamicCover::send_next_command_() {
  if (commands_.empty() || parent_ == nullptr) return;

  uint8_t cmd_byte = commands_.front();
  commands_.pop();

  EleroCommand cmd{};
  cmd.counter = counter_++;
  cmd.dst_addr = config_.dst_address;
  cmd.src_addr = config_.src_address;
  cmd.channel = config_.channel;
  cmd.type = config_.type_byte;
  cmd.type2 = config_.type2;
  cmd.hop = config_.hop;
  cmd.payload[0] = config_.payload_1;
  cmd.payload[1] = config_.payload_2;
  cmd.payload[2] = cmd_byte;

  if (!parent_->send_command(&cmd)) {
    ESP_LOGW(TAG, "TX failed for 0x%06x cmd=0x%02x", config_.dst_address, cmd_byte);
  }
}

}  // namespace esphome::elero
