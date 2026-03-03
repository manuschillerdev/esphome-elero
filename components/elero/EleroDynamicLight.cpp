#include "EleroDynamicLight.h"
#include "esphome/core/log.h"
#include "elero_packet.h"

namespace esphome::elero {

static constexpr const char* TAG = "elero.dyn_light";

bool EleroDynamicLight::activate(const NvsDeviceConfig& config, Elero* parent) {
  if (active_) {
    ESP_LOGW(TAG, "Slot already active for 0x%06x", config_.dst_address);
    return false;
  }

  if (!config.is_valid()) {
    ESP_LOGE(TAG, "Invalid config for activation");
    return false;
  }

  if (!config.is_light()) {
    ESP_LOGE(TAG, "Config is not a light type");
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
  parent_->register_light(this);

  active_ = true;
  ESP_LOGI(TAG, "Activated dynamic light '%s' at 0x%06x", config_.name, config_.dst_address);

  return true;
}

void EleroDynamicLight::deactivate() {
  if (!active_) {
    return;
  }

  ESP_LOGI(TAG, "Deactivating dynamic light 0x%06x", config_.dst_address);

  // Clear state
  active_ = false;
  parent_ = nullptr;
  config_ = NvsDeviceConfig{};
  is_on_ = false;
  brightness_ = 0.0f;
  target_brightness_ = 1.0f;
  dim_direction_ = DimDirection::NONE;
  last_seen_ms_ = 0;
  last_rssi_ = 0.0f;

  // Clear command queue
  sender_.clear_queue();
}

void EleroDynamicLight::setup() {
  // Dynamic lights are set up via activate(), not ESPHome's setup
}

void EleroDynamicLight::loop() {
  if (!active_ || parent_ == nullptr) {
    return;
  }

  const uint32_t now = millis();

  // Process command queue
  sender_.process_queue(now, parent_, TAG);

  // Handle dimming
  if (dim_direction_ != DimDirection::NONE && dim_duration_ > 0) {
    recompute_brightness();

    bool at_target = false;
    if (dim_direction_ == DimDirection::UP) {
      at_target = brightness_ >= target_brightness_;
    } else {
      at_target = brightness_ <= target_brightness_;
    }

    if (at_target) {
      if (!sender_.enqueue(command_stop_)) {
        ESP_LOGW(TAG, "Command queue full for light 0x%06x", config_.dst_address);
      }
      brightness_ = target_brightness_;
      dim_direction_ = DimDirection::NONE;
      publish_state();
    }

    // Publish estimated brightness every second while dimming
    if (now - last_publish_ > 1000) {
      publish_state();
    }
  }
}

void EleroDynamicLight::dump_config() {
  if (!active_) {
    ESP_LOGCONFIG(TAG, "  Slot: dormant");
    return;
  }

  ESP_LOGCONFIG(TAG, "  Dynamic Light '%s':", config_.name);
  ESP_LOGCONFIG(TAG, "    dst_address: 0x%06x", config_.dst_address);
  ESP_LOGCONFIG(TAG, "    src_address: 0x%06x", config_.src_address);
  ESP_LOGCONFIG(TAG, "    channel: %d", config_.channel);
  if (dim_duration_ > 0) {
    ESP_LOGCONFIG(TAG, "    Dim Duration: %ums", dim_duration_);
  }
}

void EleroDynamicLight::set_rx_state(uint8_t state) {
  if (!active_) {
    return;
  }

  ESP_LOGV(TAG, "Got state: 0x%02x for light 0x%06x", state, config_.dst_address);

  if (state == packet::state::LIGHT_ON) {
    if (!is_on_) {
      is_on_ = true;
      brightness_ = 1.0f;
      publish_state();
    }
  } else if (state == packet::state::LIGHT_OFF) {
    if (is_on_) {
      is_on_ = false;
      brightness_ = 0.0f;
      publish_state();
    }
  }
}

void EleroDynamicLight::schedule_immediate_poll() {
  if (!active_) {
    return;
  }
  if (!sender_.enqueue(command_check_)) {
    ESP_LOGW(TAG, "Command queue full for light 0x%06x", config_.dst_address);
  }
}

void EleroDynamicLight::command_on() {
  if (!active_) {
    return;
  }

  if (!sender_.enqueue(command_on_)) {
    ESP_LOGW(TAG, "Command queue full for light 0x%06x", config_.dst_address);
    return;
  }

  is_on_ = true;
  brightness_ = 1.0f;
  dim_direction_ = DimDirection::NONE;
  publish_state();
}

void EleroDynamicLight::command_off() {
  if (!active_) {
    return;
  }

  if (!sender_.enqueue(command_off_)) {
    ESP_LOGW(TAG, "Command queue full for light 0x%06x", config_.dst_address);
    return;
  }

  is_on_ = false;
  brightness_ = 0.0f;
  dim_direction_ = DimDirection::NONE;
  publish_state();
}

void EleroDynamicLight::command_toggle() {
  if (is_on_) {
    command_off();
  } else {
    command_on();
  }
}

void EleroDynamicLight::set_brightness(float brightness) {
  if (!active_) {
    return;
  }

  brightness = std::max(0.0f, std::min(1.0f, brightness));

  if (brightness < 0.01f) {
    command_off();
    return;
  }

  if (brightness >= 0.99f) {
    command_on();
    return;
  }

  // Need dimming
  if (dim_duration_ == 0) {
    // No dimming support - just turn on
    command_on();
    return;
  }

  target_brightness_ = brightness;

  // If currently off, turn on first
  if (!is_on_ || brightness_ < 0.01f) {
    if (!sender_.enqueue(command_on_)) {
      ESP_LOGW(TAG, "Command queue full for light 0x%06x", config_.dst_address);
    }
    is_on_ = true;
    brightness_ = 1.0f;
  }

  // Start dimming
  if (brightness > brightness_ + 0.01f) {
    ESP_LOGD(TAG, "Dimming up 0x%06x from %.2f to %.2f", config_.dst_address, brightness_, brightness);
    if (!sender_.enqueue(command_dim_up_)) {
      ESP_LOGW(TAG, "Command queue full for light 0x%06x", config_.dst_address);
    }
    dim_direction_ = DimDirection::UP;
    dimming_start_ = millis();
    last_recompute_time_ = millis();
  } else if (brightness < brightness_ - 0.01f) {
    ESP_LOGD(TAG, "Dimming down 0x%06x from %.2f to %.2f", config_.dst_address, brightness_, brightness);
    if (!sender_.enqueue(command_dim_down_)) {
      ESP_LOGW(TAG, "Command queue full for light 0x%06x", config_.dst_address);
    }
    dim_direction_ = DimDirection::DOWN;
    dimming_start_ = millis();
    last_recompute_time_ = millis();
  }
}

void EleroDynamicLight::recompute_brightness() {
  if (dim_direction_ == DimDirection::NONE) {
    return;
  }

  const uint32_t now = millis();
  float dir = (dim_direction_ == DimDirection::UP) ? 1.0f : -1.0f;
  brightness_ += dir * static_cast<float>(now - last_recompute_time_) / static_cast<float>(dim_duration_);
  brightness_ = std::max(0.0f, std::min(1.0f, brightness_));
  last_recompute_time_ = now;
}

void EleroDynamicLight::publish_state() {
  if (!active_) {
    return;
  }

  last_publish_ = millis();

  // Notify MQTT bridge of state change
  if (on_state_change_) {
    on_state_change_(config_.dst_address, is_on_, brightness_);
  }
}

}  // namespace esphome::elero
