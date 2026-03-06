#include "EleroDynamicLight.h"
#include "esphome/core/log.h"

namespace esphome::elero {

static constexpr const char *TAG = "elero.dyn_light";

bool EleroDynamicLight::activate(const NvsDeviceConfig &config, Elero *parent) {
  if (active_) {
    ESP_LOGW(TAG, "Slot already active for 0x%06x", config_.dst_address);
    return false;
  }
  if (!config.is_valid() || !config.is_light() || parent == nullptr) {
    ESP_LOGE(TAG, "Invalid config or null parent for activation");
    return false;
  }

  config_ = config;
  parent_ = parent;
  parent_->register_light(this);
  active_ = true;

  ESP_LOGI(TAG, "Activated dynamic light '%s' at 0x%06x", config_.name, config_.dst_address);
  return true;
}

void EleroDynamicLight::deactivate() {
  if (!active_) return;
  ESP_LOGI(TAG, "Deactivating dynamic light 0x%06x", config_.dst_address);
  active_ = false;
  parent_ = nullptr;
  config_ = NvsDeviceConfig{};
  is_on_ = false;
  last_state_raw_ = 0;
  last_seen_ms_ = 0;
  while (!commands_.empty()) commands_.pop();
}

void EleroDynamicLight::set_rx_state(uint8_t state) {
  last_state_raw_ = state;
  if (state == packet::state::LIGHT_ON) {
    is_on_ = true;
  } else if (state == packet::state::LIGHT_OFF || state == packet::state::BOTTOM_TILT) {
    is_on_ = false;
  }
  publish_state_();
}

void EleroDynamicLight::notify_rx_meta(uint32_t ms, float rssi) {
  last_seen_ms_ = ms;
  last_rssi_ = rssi;
}

void EleroDynamicLight::enqueue_command(uint8_t cmd_byte) {
  if (!active_ || parent_ == nullptr) return;
  if (commands_.size() >= packet::limits::MAX_COMMAND_QUEUE) {
    ESP_LOGW(TAG, "Command queue full for 0x%06x", config_.dst_address);
    return;
  }
  commands_.push(cmd_byte);
}

void EleroDynamicLight::schedule_immediate_poll() {
  enqueue_command(packet::command::CHECK);
}

bool EleroDynamicLight::perform_action(const char *action) {
  if (action == nullptr) return false;
  if (strcmp(action, "on") == 0) {
    enqueue_command(packet::command::UP);
    return true;
  }
  if (strcmp(action, "off") == 0) {
    enqueue_command(packet::command::DOWN);
    return true;
  }
  if (strcmp(action, "stop") == 0) {
    enqueue_command(packet::command::STOP);
    return true;
  }
  if (strcmp(action, "check") == 0) {
    enqueue_command(packet::command::CHECK);
    return true;
  }
  return false;
}

void EleroDynamicLight::loop(uint32_t now) {
  if (!active_ || parent_ == nullptr) return;

  if (!commands_.empty() && (now - last_command_ms_ >= packet::timing::DELAY_SEND_PACKETS)) {
    send_next_command_();
    last_command_ms_ = now;
  }
}

void EleroDynamicLight::publish_state_() {
  if (state_callback_) {
    state_callback_(this);
  }
}

void EleroDynamicLight::send_next_command_() {
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
