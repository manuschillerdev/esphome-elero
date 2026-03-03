#include "EleroDynamicCover.h"
#include "esphome/core/log.h"
#include "elero_packet.h"

namespace esphome::elero {

static constexpr const char* TAG = "elero.dyn_cover";

bool EleroDynamicCover::activate(const NvsDeviceConfig& config, Elero* parent) {
  if (active_) {
    ESP_LOGW(TAG, "Slot already active for 0x%06x", config_.dst_address);
    return false;
  }

  if (!config.is_valid()) {
    ESP_LOGE(TAG, "Invalid config for activation");
    return false;
  }

  if (!config.is_cover()) {
    ESP_LOGE(TAG, "Config is not a cover type");
    return false;
  }

  if (parent == nullptr) {
    ESP_LOGE(TAG, "Parent hub is null");
    return false;
  }

  config_ = config;
  parent_ = parent;

  // Configure command sender with RF parameters
  sender_.command().dst_addr = config_.dst_address;
  sender_.command().src_addr = config_.src_address;
  sender_.command().channel = config_.channel;
  sender_.command().hop = config_.hop;
  sender_.command().type = config_.type_byte;
  sender_.command().type2 = config_.type2;
  sender_.command().payload[0] = config_.payload_1;
  sender_.command().payload[1] = config_.payload_2;

  // Register with hub for RF callbacks
  parent_->register_cover(this);

  active_ = true;
  ESP_LOGI(TAG, "Activated dynamic cover '%s' at 0x%06x", config_.name, config_.dst_address);

  return true;
}

void EleroDynamicCover::deactivate() {
  if (!active_) {
    return;
  }

  ESP_LOGI(TAG, "Deactivating dynamic cover 0x%06x", config_.dst_address);

  // Clear state
  active_ = false;
  parent_ = nullptr;
  config_ = NvsDeviceConfig{};
  position_ = 0.5f;
  tilt_ = 0.0f;
  current_operation_ = Operation::IDLE;
  last_state_raw_ = packet::state::UNKNOWN;
  last_seen_ms_ = 0;
  last_rssi_ = 0.0f;

  // Clear command queue
  sender_.clear_queue();
}

void EleroDynamicCover::setup() {
  // Dynamic covers are set up via activate(), not ESPHome's setup
  // This is called during component initialization but slot is dormant
}

void EleroDynamicCover::loop() {
  if (!active_ || parent_ == nullptr) {
    return;
  }

  uint32_t intvl = config_.poll_interval_ms;
  uint32_t now = millis();

  // Handle movement timeout
  if (current_operation_ != Operation::IDLE) {
    if ((now - movement_start_) < packet::timing::TIMEOUT_MOVEMENT) {
      // Poll frequently while moving
      intvl = packet::timing::POLL_INTERVAL_MOVING;
    } else {
      // Movement timed out
      ESP_LOGW(TAG, "Movement timeout for 0x%06x, resetting to IDLE", config_.dst_address);
      current_operation_ = Operation::IDLE;
      publish_state();
    }
  }

  // Periodic polling
  if ((now > poll_offset_) && (now - poll_offset_ - last_poll_) > intvl) {
    if (sender_.enqueue(command_check_)) {
      last_poll_ = now - poll_offset_;
    }
  }

  // Process command queue
  sender_.process_queue(now, parent_, TAG);

  // Position tracking
  if ((current_operation_ != Operation::IDLE) &&
      (config_.open_duration_ms > 0) &&
      (config_.close_duration_ms > 0)) {
    recompute_position();
    if (is_at_target()) {
      if (sender_.enqueue(command_stop_)) {
        current_operation_ = Operation::IDLE;
        target_position_ = 1.0f;  // Reset target to open
      }
    }

    // Publish position every second
    if (now - last_publish_ > 1000) {
      publish_state();
      last_publish_ = now;
    }
  }
}

void EleroDynamicCover::dump_config() {
  if (!active_) {
    ESP_LOGCONFIG(TAG, "  Slot: dormant");
    return;
  }

  ESP_LOGCONFIG(TAG, "  Dynamic Cover '%s':", config_.name);
  ESP_LOGCONFIG(TAG, "    dst_address: 0x%06x", config_.dst_address);
  ESP_LOGCONFIG(TAG, "    src_address: 0x%06x", config_.src_address);
  ESP_LOGCONFIG(TAG, "    channel: %d", config_.channel);
  if (config_.open_duration_ms > 0) {
    ESP_LOGCONFIG(TAG, "    Open Duration: %ums", config_.open_duration_ms);
  }
  if (config_.close_duration_ms > 0) {
    ESP_LOGCONFIG(TAG, "    Close Duration: %ums", config_.close_duration_ms);
  }
  ESP_LOGCONFIG(TAG, "    Poll Interval: %ums", config_.poll_interval_ms);
}

void EleroDynamicCover::set_rx_state(uint8_t state) {
  if (!active_) {
    return;
  }

  last_state_raw_ = state;
  ESP_LOGV(TAG, "Got state: 0x%02x (%s) for blind 0x%06x", state,
           elero_state_to_string(state), config_.dst_address);

  auto result = packet::map_cover_state(state);

  // Log warnings
  if (result.is_warning) {
    ESP_LOGW(TAG, "Blind 0x%06x reports %s", config_.dst_address, result.warning_msg);
  }

  // Map packet::CoverOp to our Operation enum
  Operation op = static_cast<Operation>(result.operation);

  // Use result position, or keep current if unchanged (-1)
  float pos = (result.position >= 0.0f) ? result.position : position_;
  float current_tilt = result.tilt;

  if ((pos != position_) || (op != current_operation_) || (current_tilt != tilt_)) {
    position_ = pos;
    tilt_ = current_tilt;
    current_operation_ = op;
    publish_state();
  }
}

const char* EleroDynamicCover::get_operation_str() const {
  switch (current_operation_) {
    case Operation::IDLE:
      return "idle";
    case Operation::OPENING:
      return "opening";
    case Operation::CLOSING:
      return "closing";
    default:
      return "unknown";
  }
}

bool EleroDynamicCover::perform_action(const char* action) {
  if (!active_) {
    return false;
  }

  if (strcmp(action, "up") == 0 || strcmp(action, "open") == 0) {
    command_open();
    return true;
  }
  if (strcmp(action, "down") == 0 || strcmp(action, "close") == 0) {
    command_close();
    return true;
  }
  if (strcmp(action, "stop") == 0) {
    command_stop();
    return true;
  }
  if (strcmp(action, "tilt") == 0) {
    command_tilt();
    return true;
  }
  if (strcmp(action, "check") == 0) {
    if (!sender_.enqueue(command_check_)) {
      ESP_LOGW(TAG, "Command queue full for cover 0x%06x", config_.dst_address);
    }
    return true;
  }
  return false;
}

void EleroDynamicCover::schedule_immediate_poll() {
  if (!active_) {
    return;
  }
  if (!sender_.enqueue(command_check_)) {
    ESP_LOGW(TAG, "Command queue full for cover 0x%06x", config_.dst_address);
  }
}

void EleroDynamicCover::apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms,
                                                uint32_t poll_intvl_ms) {
  if (open_dur_ms != 0) {
    config_.open_duration_ms = open_dur_ms;
  }
  if (close_dur_ms != 0) {
    config_.close_duration_ms = close_dur_ms;
  }
  if (poll_intvl_ms != 0) {
    config_.poll_interval_ms = poll_intvl_ms;
  }
}

void EleroDynamicCover::command_open() {
  if (!active_) {
    return;
  }
  target_position_ = 1.0f;
  start_movement(Operation::OPENING);
}

void EleroDynamicCover::command_close() {
  if (!active_) {
    return;
  }
  target_position_ = 0.0f;
  start_movement(Operation::CLOSING);
}

void EleroDynamicCover::command_stop() {
  if (!active_) {
    return;
  }
  start_movement(Operation::IDLE);
}

void EleroDynamicCover::command_tilt() {
  if (!active_) {
    return;
  }
  if (sender_.enqueue(command_tilt_)) {
    tilt_ = 1.0f;
    publish_state();
  }
}

void EleroDynamicCover::command_set_position(float position) {
  if (!active_) {
    return;
  }

  position = std::max(0.0f, std::min(1.0f, position));
  target_position_ = position;

  if (position > position_) {
    start_movement(Operation::OPENING);
  } else if (position < position_) {
    start_movement(Operation::CLOSING);
  }
  // If position == position_, do nothing
}

void EleroDynamicCover::start_movement(Operation dir) {
  switch (dir) {
    case Operation::OPENING:
      ESP_LOGV(TAG, "Sending OPEN command");
      if (sender_.enqueue(command_up_)) {
        tilt_ = 0.0f;
        last_operation_ = Operation::OPENING;
      }
      break;
    case Operation::CLOSING:
      ESP_LOGV(TAG, "Sending CLOSE command");
      if (sender_.enqueue(command_down_)) {
        tilt_ = 0.0f;
        last_operation_ = Operation::CLOSING;
      }
      break;
    case Operation::IDLE:
      sender_.clear_queue();
      if (!sender_.enqueue(command_stop_)) {
        ESP_LOGW(TAG, "Command queue full for cover 0x%06x", config_.dst_address);
      }
      break;
  }

  if (dir == current_operation_) {
    return;
  }

  current_operation_ = dir;
  start_position_ = position_;
  movement_start_ = millis();
  publish_state();
}

void EleroDynamicCover::recompute_position() {
  if (current_operation_ == Operation::IDLE) {
    return;
  }

  float dir = 0.0f;
  float action_dur = 0.0f;
  switch (current_operation_) {
    case Operation::OPENING:
      dir = 1.0f;
      action_dur = static_cast<float>(config_.open_duration_ms);
      break;
    case Operation::CLOSING:
      dir = -1.0f;
      action_dur = static_cast<float>(config_.close_duration_ms);
      break;
    default:
      return;
  }

  if (action_dur == 0.0f) {
    return;
  }

  const uint32_t now = millis();
  float elapsed_ratio = static_cast<float>(now - movement_start_) / action_dur;
  position_ = start_position_ + dir * elapsed_ratio;
  position_ = std::max(0.0f, std::min(1.0f, position_));
}

bool EleroDynamicCover::is_at_target() const {
  // Don't send stop for fully open or closed
  if ((target_position_ == 1.0f) || (target_position_ == 0.0f)) {
    return false;
  }

  switch (current_operation_) {
    case Operation::OPENING:
      return position_ >= target_position_;
    case Operation::CLOSING:
      return position_ <= target_position_;
    case Operation::IDLE:
    default:
      return true;
  }
}

void EleroDynamicCover::publish_state() {
  if (!active_) {
    return;
  }

  last_publish_ = millis();

  // Notify MQTT bridge of state change
  if (on_state_change_) {
    on_state_change_(config_.dst_address, get_operation_str(), position_);
  }
}

}  // namespace esphome::elero
