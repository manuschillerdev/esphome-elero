#include "mqtt_device_manager.h"
#include "../elero/elero.h"
#include "../elero/EleroDynamicCover.h"
#include "../elero/EleroDynamicLight.h"
#include "esphome_mqtt_adapter.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cstdio>

namespace esphome::elero {

static constexpr const char* TAG = "elero.mqtt_mgr";

void MqttDeviceManager::dump_config() {
  ESP_LOGCONFIG(TAG, "MQTT Device Manager:");
  ESP_LOGCONFIG(TAG, "  Topic Prefix: %s", topic_prefix_.c_str());
  ESP_LOGCONFIG(TAG, "  Discovery Prefix: %s", discovery_prefix_.c_str());
  ESP_LOGCONFIG(TAG, "  Device Name: %s", device_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Cover Slots: %u", cover_slot_count_);
  ESP_LOGCONFIG(TAG, "  Light Slots: %u", light_slot_count_);
}

void MqttDeviceManager::setup() {
  ESP_LOGI(TAG, "Setting up MQTT device manager");

  // Create NVS storage
  auto nvs = std::make_unique<EspNvsStorage>();
  if (!nvs->init()) {
    ESP_LOGW(TAG, "Failed to initialize NVS");
  }
  nvs_ = std::move(nvs);

  // Create MQTT adapter
  mqtt_ = std::make_unique<EspHomeMqttAdapter>();

  // Get device MAC for unique ID
  device_mac_ = get_mac_address_pretty();

  // Wire up to the hub
  if (hub_ != nullptr) {
    hub_->set_device_manager(this);
  } else {
    ESP_LOGE(TAG, "Hub not set!");
    return;
  }

  // Load devices from NVS
  if (nvs_ != nullptr) {
    // Load covers
    std::vector<NvsDeviceConfig> covers;
    NvsResult result = nvs_->load_covers(covers);
    if (result == NvsResult::OK || result == NvsResult::NOT_FOUND) {
      for (const auto& config : covers) {
        if (config.is_enabled() && config.is_valid()) {
          if (activate_cover(config)) {
            ESP_LOGD(TAG, "Loaded cover 0x%06x '%s'", config.dst_address, config.name);
          } else {
            ESP_LOGW(TAG, "Failed to activate cover 0x%06x", config.dst_address);
          }
        }
      }
    } else {
      ESP_LOGW(TAG, "Failed to load covers from NVS: %s", nvs_result_to_string(result));
    }

    // Load lights
    std::vector<NvsDeviceConfig> lights;
    result = nvs_->load_lights(lights);
    if (result == NvsResult::OK || result == NvsResult::NOT_FOUND) {
      for (const auto& config : lights) {
        if (config.is_enabled() && config.is_valid()) {
          if (activate_light(config)) {
            ESP_LOGD(TAG, "Loaded light 0x%06x '%s'", config.dst_address, config.name);
          } else {
            ESP_LOGW(TAG, "Failed to activate light 0x%06x", config.dst_address);
          }
        }
      }
    } else {
      ESP_LOGW(TAG, "Failed to load lights from NVS: %s", nvs_result_to_string(result));
    }

    ESP_LOGI(TAG, "Loaded devices from NVS");
  }

  // Subscribe to command topics
  subscribe_to_commands();

  setup_complete_ = true;
  ESP_LOGI(TAG, "MQTT device manager ready");
}

void MqttDeviceManager::loop() {
  if (!setup_complete_) {
    return;
  }

  // Check MQTT connection state changes
  bool is_connected = mqtt_ != nullptr && mqtt_->is_connected();

  if (is_connected && !was_connected_) {
    // Just connected - republish all discoveries
    ESP_LOGI(TAG, "MQTT connected, publishing discoveries");
    publish_all_discoveries();
  }

  was_connected_ = is_connected;
}

void MqttDeviceManager::on_rf_packet(const RfPacketInfo& pkt) {
  // Only process status packets
  if (!packet::is_status_packet(pkt.type)) {
    return;
  }

  // Check if this is one of our covers
  auto* cover = find_cover(pkt.src);
  if (cover != nullptr) {
    publish_cover_state(pkt.src, cover->get_operation_str(), cover->get_cover_position());
    return;
  }

  // Check if this is one of our lights
  auto* light = find_light(pkt.src);
  if (light != nullptr) {
    publish_light_state(pkt.src, light->is_on(), light->brightness());
    return;
  }
}

bool MqttDeviceManager::add_device(const NvsDeviceConfig& config) {
  if (!config.is_valid()) {
    ESP_LOGW(TAG, "Invalid device config");
    return false;
  }

  // Activate slot
  bool activated = false;
  if (config.is_cover()) {
    activated = activate_cover(config);
  } else {
    activated = activate_light(config);
  }

  if (!activated) {
    ESP_LOGE(TAG, "Failed to activate device 0x%06x", config.dst_address);
    return false;
  }

  // Save to NVS
  if (nvs_ != nullptr) {
    NvsResult result = nvs_->save_device(config);
    if (result != NvsResult::OK) {
      ESP_LOGE(TAG, "Failed to save device to NVS: %s", nvs_result_to_string(result));
      // Deactivate since save failed (ignore result, just trying to clean up)
      if (config.is_cover()) {
        (void)deactivate_cover(config.dst_address);
      } else {
        (void)deactivate_light(config.dst_address);
      }
      return false;
    }
  }

  // Publish discovery
  if (config.is_cover()) {
    publish_cover_discovery(config.dst_address, config.name);
  } else {
    // TODO: Check has_brightness from config
    publish_light_discovery(config.dst_address, config.name, false);
  }

  ESP_LOGI(TAG, "Added device 0x%06x '%s' (%s)", config.dst_address, config.name,
           config.is_cover() ? "cover" : "light");
  return true;
}

bool MqttDeviceManager::remove_device(uint32_t address, DeviceType type) {
  // Remove discovery from HA first
  remove_discovery(address, type == DeviceType::COVER);

  // Deactivate slot
  bool deactivated = false;
  if (type == DeviceType::COVER) {
    deactivated = deactivate_cover(address);
  } else {
    deactivated = deactivate_light(address);
  }

  // Remove from NVS
  if (nvs_ != nullptr) {
    NvsResult result = nvs_->remove_device(address, type);
    if (result != NvsResult::OK && result != NvsResult::NOT_FOUND) {
      ESP_LOGW(TAG, "Failed to remove device from NVS: %s", nvs_result_to_string(result));
    }
  }

  ESP_LOGI(TAG, "Removed device 0x%06x (%s)", address, type == DeviceType::COVER ? "cover" : "light");
  return deactivated;
}

bool MqttDeviceManager::update_device(const NvsDeviceConfig& config) {
  if (!config.is_valid()) {
    return false;
  }

  // Find existing device
  if (config.is_cover()) {
    auto* cover = find_cover(config.dst_address);
    if (cover == nullptr) {
      return false;
    }
    // Apply new settings
    cover->apply_runtime_settings(config.open_duration_ms, config.close_duration_ms, config.poll_interval_ms);
  } else {
    auto* light = find_light(config.dst_address);
    if (light == nullptr) {
      return false;
    }
    // Light doesn't have runtime settings to apply yet
  }

  // Save to NVS
  if (nvs_ != nullptr) {
    NvsResult result = nvs_->save_device(config);
    if (result != NvsResult::OK) {
      ESP_LOGW(TAG, "Failed to update device in NVS: %s", nvs_result_to_string(result));
      return false;
    }
  }

  // Re-publish discovery with new name if changed
  if (config.is_cover()) {
    publish_cover_discovery(config.dst_address, config.name);
  } else {
    publish_light_discovery(config.dst_address, config.name, false);
  }

  return true;
}

bool MqttDeviceManager::set_device_enabled(uint32_t address, DeviceType type, bool enabled) {
  // Load current config
  std::vector<NvsDeviceConfig> devices;
  NvsResult result;

  if (type == DeviceType::COVER) {
    result = nvs_->load_covers(devices);
  } else {
    result = nvs_->load_lights(devices);
  }

  if (result != NvsResult::OK) {
    return false;
  }

  // Find and update
  for (auto& config : devices) {
    if (config.dst_address == address) {
      config.set_enabled(enabled);

      // Activate or deactivate slot
      if (enabled) {
        if (type == DeviceType::COVER) {
          if (activate_cover(config)) {
            publish_cover_discovery(address, config.name);
          }
        } else {
          if (activate_light(config)) {
            publish_light_discovery(address, config.name, false);
          }
        }
      } else {
        remove_discovery(address, type == DeviceType::COVER);
        if (type == DeviceType::COVER) {
          (void)deactivate_cover(address);
        } else {
          (void)deactivate_light(address);
        }
      }

      // Save back to NVS
      if (type == DeviceType::COVER) {
        (void)nvs_->save_covers(devices);
      } else {
        (void)nvs_->save_lights(devices);
      }

      return true;
    }
  }

  return false;
}

std::vector<NvsDeviceConfig> MqttDeviceManager::get_cover_configs() const {
  std::vector<NvsDeviceConfig> configs;
  if (nvs_ != nullptr) {
    NvsResult result = nvs_->load_covers(configs);
    if (result != NvsResult::OK && result != NvsResult::NOT_FOUND) {
      ESP_LOGW(TAG, "Failed to load covers: %s", nvs_result_to_string(result));
    }
  }
  return configs;
}

std::vector<NvsDeviceConfig> MqttDeviceManager::get_light_configs() const {
  std::vector<NvsDeviceConfig> configs;
  if (nvs_ != nullptr) {
    NvsResult result = nvs_->load_lights(configs);
    if (result != NvsResult::OK && result != NvsResult::NOT_FOUND) {
      ESP_LOGW(TAG, "Failed to load lights: %s", nvs_result_to_string(result));
    }
  }
  return configs;
}

// ─── Slot Management ───

EleroDynamicCover* MqttDeviceManager::find_free_cover_slot() {
  for (size_t i = 0; i < cover_slot_count_; ++i) {
    if (!cover_slots_[i].is_active()) {
      return &cover_slots_[i];
    }
  }
  return nullptr;
}

EleroDynamicLight* MqttDeviceManager::find_free_light_slot() {
  for (size_t i = 0; i < light_slot_count_; ++i) {
    if (!light_slots_[i].is_active()) {
      return &light_slots_[i];
    }
  }
  return nullptr;
}

EleroDynamicCover* MqttDeviceManager::find_cover(uint32_t address) {
  for (size_t i = 0; i < cover_slot_count_; ++i) {
    if (cover_slots_[i].is_active() && cover_slots_[i].get_blind_address() == address) {
      return &cover_slots_[i];
    }
  }
  return nullptr;
}

EleroDynamicLight* MqttDeviceManager::find_light(uint32_t address) {
  for (size_t i = 0; i < light_slot_count_; ++i) {
    if (light_slots_[i].is_active() && light_slots_[i].get_light_address() == address) {
      return &light_slots_[i];
    }
  }
  return nullptr;
}

bool MqttDeviceManager::activate_cover(const NvsDeviceConfig& config) {
  // Check if already active
  if (find_cover(config.dst_address) != nullptr) {
    ESP_LOGW(TAG, "Cover 0x%06x already active", config.dst_address);
    return true;  // Consider it success
  }

  auto* slot = find_free_cover_slot();
  if (slot == nullptr) {
    ESP_LOGE(TAG, "No free cover slots (max %u)", cover_slot_count_);
    return false;
  }

  if (!slot->activate(config, hub_)) {
    ESP_LOGE(TAG, "Failed to activate cover slot");
    return false;
  }

  // Register with hub for RF packet routing
  hub_->register_cover(slot);

  return true;
}

bool MqttDeviceManager::activate_light(const NvsDeviceConfig& config) {
  // Check if already active
  if (find_light(config.dst_address) != nullptr) {
    ESP_LOGW(TAG, "Light 0x%06x already active", config.dst_address);
    return true;
  }

  auto* slot = find_free_light_slot();
  if (slot == nullptr) {
    ESP_LOGE(TAG, "No free light slots (max %u)", light_slot_count_);
    return false;
  }

  if (!slot->activate(config, hub_)) {
    ESP_LOGE(TAG, "Failed to activate light slot");
    return false;
  }

  // Register with hub for RF packet routing
  hub_->register_light(slot);

  return true;
}

bool MqttDeviceManager::deactivate_cover(uint32_t address) {
  auto* cover = find_cover(address);
  if (cover == nullptr) {
    return false;
  }
  cover->deactivate();
  // Note: We don't unregister from hub - the hub will skip inactive covers
  return true;
}

bool MqttDeviceManager::deactivate_light(uint32_t address) {
  auto* light = find_light(address);
  if (light == nullptr) {
    return false;
  }
  light->deactivate();
  return true;
}

// ─── MQTT Publishing ───

void MqttDeviceManager::publish_cover_discovery(uint32_t addr, const char* name) {
  if (mqtt_ == nullptr || !mqtt_->is_connected()) {
    return;
  }

  std::string topic = discovery_topic("cover", addr);
  std::string hex_addr = addr_to_hex(addr);

  // Build discovery payload
  std::string payload;
  payload.reserve(512);
  payload = "{";
  payload += "\"name\":\"" + std::string(name) + "\",";
  payload += "\"unique_id\":\"elero_cover_" + hex_addr + "\",";
  payload += "\"cmd_t\":\"" + cover_cmd_topic(addr) + "\",";
  payload += "\"stat_t\":\"" + cover_state_topic(addr) + "\",";
  payload += "\"pos_t\":\"" + cover_position_topic(addr) + "\",";
  payload += "\"set_pos_t\":\"" + cover_set_position_topic(addr) + "\",";
  payload += "\"pos_open\":100,";
  payload += "\"pos_clsd\":0,";
  payload += "\"pl_open\":\"open\",";
  payload += "\"pl_cls\":\"close\",";
  payload += "\"pl_stop\":\"stop\",";
  payload += "\"opt\":true,";
  payload += "\"device\":{";
  payload += "\"ids\":[\"elero_gateway_" + device_mac_ + "\"],";
  payload += "\"name\":\"" + device_name_ + "\",";
  payload += "\"mf\":\"Elero\",";
  payload += "\"mdl\":\"ESPHome Gateway\"";
  payload += "}";
  payload += "}";

  if (mqtt_->publish(topic, payload, true)) {
    cover_discovery_published_[addr] = true;
    ESP_LOGD(TAG, "Published cover discovery for 0x%06x", addr);
  } else {
    ESP_LOGW(TAG, "Failed to publish cover discovery for 0x%06x", addr);
  }
}

void MqttDeviceManager::publish_light_discovery(uint32_t addr, const char* name, bool has_brightness) {
  if (mqtt_ == nullptr || !mqtt_->is_connected()) {
    return;
  }

  std::string topic = discovery_topic("light", addr);
  std::string hex_addr = addr_to_hex(addr);

  // Build discovery payload
  std::string payload;
  payload.reserve(512);
  payload = "{";
  payload += "\"name\":\"" + std::string(name) + "\",";
  payload += "\"unique_id\":\"elero_light_" + hex_addr + "\",";
  payload += "\"cmd_t\":\"" + light_cmd_topic(addr) + "\",";
  payload += "\"stat_t\":\"" + light_state_topic(addr) + "\",";
  payload += "\"schema\":\"json\",";
  if (has_brightness) {
    payload += "\"brightness\":true,";
    payload += "\"brightness_scale\":100,";
  }
  payload += "\"device\":{";
  payload += "\"ids\":[\"elero_gateway_" + device_mac_ + "\"],";
  payload += "\"name\":\"" + device_name_ + "\",";
  payload += "\"mf\":\"Elero\",";
  payload += "\"mdl\":\"ESPHome Gateway\"";
  payload += "}";
  payload += "}";

  if (mqtt_->publish(topic, payload, true)) {
    light_discovery_published_[addr] = true;
    ESP_LOGD(TAG, "Published light discovery for 0x%06x", addr);
  } else {
    ESP_LOGW(TAG, "Failed to publish light discovery for 0x%06x", addr);
  }
}

void MqttDeviceManager::remove_discovery(uint32_t addr, bool is_cover) {
  if (mqtt_ == nullptr) {
    return;
  }

  std::string topic = discovery_topic(is_cover ? "cover" : "light", addr);

  // Empty payload removes from HA
  if (mqtt_->publish(topic, "", true)) {
    if (is_cover) {
      cover_discovery_published_.erase(addr);
    } else {
      light_discovery_published_.erase(addr);
    }
    ESP_LOGD(TAG, "Removed %s discovery for 0x%06x", is_cover ? "cover" : "light", addr);
  }
}

void MqttDeviceManager::publish_all_discoveries() {
  // Publish all active covers
  for (size_t i = 0; i < cover_slot_count_; ++i) {
    if (cover_slots_[i].is_active()) {
      publish_cover_discovery(cover_slots_[i].get_blind_address(), cover_slots_[i].get_blind_name().c_str());
    }
  }

  // Publish all active lights
  for (size_t i = 0; i < light_slot_count_; ++i) {
    if (light_slots_[i].is_active()) {
      // TODO: Get has_brightness from config
      publish_light_discovery(light_slots_[i].get_light_address(), light_slots_[i].get_light_name().c_str(), false);
    }
  }
}

void MqttDeviceManager::publish_cover_state(uint32_t addr, const char* state, float position) {
  if (mqtt_ == nullptr || !mqtt_->is_connected()) {
    return;
  }

  // Publish state (open/closed/opening/closing)
  std::string state_topic = cover_state_topic(addr);
  std::string state_val;
  if (strcmp(state, "idle") == 0) {
    state_val = (position > 0.5f) ? "open" : "closed";
  } else if (strcmp(state, "opening") == 0) {
    state_val = "opening";
  } else if (strcmp(state, "closing") == 0) {
    state_val = "closing";
  } else {
    state_val = state;
  }
  mqtt_->publish(state_topic, state_val, false);

  // Publish position (0-100)
  std::string pos_topic = cover_position_topic(addr);
  char pos_str[8];
  snprintf(pos_str, sizeof(pos_str), "%d", static_cast<int>(position * 100.0f));
  mqtt_->publish(pos_topic, pos_str, false);
}

void MqttDeviceManager::publish_light_state(uint32_t addr, bool on, float brightness) {
  if (mqtt_ == nullptr || !mqtt_->is_connected()) {
    return;
  }

  std::string topic = light_state_topic(addr);

  // JSON schema state
  std::string payload;
  payload.reserve(64);
  payload = "{\"state\":\"";
  payload += on ? "ON" : "OFF";
  payload += "\"";
  if (on && brightness > 0.0f) {
    char bri_str[8];
    snprintf(bri_str, sizeof(bri_str), "%d", static_cast<int>(brightness * 100.0f));
    payload += ",\"brightness\":";
    payload += bri_str;
  }
  payload += "}";

  mqtt_->publish(topic, payload, false);
}

// ─── Command Handling ───

void MqttDeviceManager::subscribe_to_commands() {
  if (mqtt_ == nullptr) {
    return;
  }

  // Subscribe to cover commands: elero/+/cmd
  std::string cover_cmd = topic_prefix_ + "/+/cmd";
  mqtt_->subscribe(cover_cmd, [this](const std::string& topic, const std::string& payload) {
    // Extract address from topic: elero/0xADDRESS/cmd
    size_t start = topic.find('/') + 1;
    size_t end = topic.rfind('/');
    if (start != std::string::npos && end != std::string::npos && end > start) {
      std::string addr_str = topic.substr(start, end - start);
      uint32_t addr = 0;
      if (sscanf(addr_str.c_str(), "0x%x", &addr) == 1 || sscanf(addr_str.c_str(), "%x", &addr) == 1) {
        on_cover_command(addr, payload);
      }
    }
  });

  // Subscribe to cover position: elero/+/set_position
  std::string pos_topic = topic_prefix_ + "/+/set_position";
  mqtt_->subscribe(pos_topic, [this](const std::string& topic, const std::string& payload) {
    size_t start = topic.find('/') + 1;
    size_t end = topic.rfind('/');
    if (start != std::string::npos && end != std::string::npos && end > start) {
      std::string addr_str = topic.substr(start, end - start);
      uint32_t addr = 0;
      if (sscanf(addr_str.c_str(), "0x%x", &addr) == 1 || sscanf(addr_str.c_str(), "%x", &addr) == 1) {
        on_cover_position(addr, payload);
      }
    }
  });

  // Subscribe to light commands: elero/+/light/cmd
  std::string light_cmd = topic_prefix_ + "/+/light/cmd";
  mqtt_->subscribe(light_cmd, [this](const std::string& topic, const std::string& payload) {
    // Extract address: elero/0xADDRESS/light/cmd
    size_t start = topic.find('/') + 1;
    size_t end = topic.find('/', start);
    if (start != std::string::npos && end != std::string::npos) {
      std::string addr_str = topic.substr(start, end - start);
      uint32_t addr = 0;
      if (sscanf(addr_str.c_str(), "0x%x", &addr) == 1 || sscanf(addr_str.c_str(), "%x", &addr) == 1) {
        on_light_command(addr, payload);
      }
    }
  });

  ESP_LOGD(TAG, "Subscribed to command topics");
}

void MqttDeviceManager::on_cover_command(uint32_t addr, const std::string& payload) {
  auto* cover = find_cover(addr);
  if (cover == nullptr) {
    return;
  }

  ESP_LOGD(TAG, "Cover command 0x%06x: %s", addr, payload.c_str());

  if (payload == "open") {
    cover->command_open();
  } else if (payload == "close") {
    cover->command_close();
  } else if (payload == "stop") {
    cover->command_stop();
  }
}

void MqttDeviceManager::on_cover_position(uint32_t addr, const std::string& payload) {
  auto* cover = find_cover(addr);
  if (cover == nullptr) {
    return;
  }

  int pos = 0;
  if (sscanf(payload.c_str(), "%d", &pos) == 1) {
    cover->command_set_position(static_cast<float>(pos) / 100.0f);
  }
}

void MqttDeviceManager::on_light_command(uint32_t addr, const std::string& payload) {
  auto* light = find_light(addr);
  if (light == nullptr) {
    return;
  }

  ESP_LOGD(TAG, "Light command 0x%06x: %s", addr, payload.c_str());

  // Parse JSON payload (HA sends {"state":"ON"} or {"state":"OFF","brightness":50})
  if (payload.find("\"ON\"") != std::string::npos) {
    light->command_on();
  } else if (payload.find("\"OFF\"") != std::string::npos) {
    light->command_off();
  }

  // Handle brightness if present
  auto bri_pos = payload.find("\"brightness\":");
  if (bri_pos != std::string::npos) {
    int bri = 0;
    if (sscanf(payload.c_str() + bri_pos + 13, "%d", &bri) == 1) {
      light->set_brightness(static_cast<float>(bri) / 100.0f);
    }
  }
}

// ─── Topic Builders ───

std::string MqttDeviceManager::cover_cmd_topic(uint32_t addr) const {
  return topic_prefix_ + "/" + addr_to_hex(addr) + "/cmd";
}

std::string MqttDeviceManager::cover_state_topic(uint32_t addr) const {
  return topic_prefix_ + "/" + addr_to_hex(addr) + "/state";
}

std::string MqttDeviceManager::cover_position_topic(uint32_t addr) const {
  return topic_prefix_ + "/" + addr_to_hex(addr) + "/position";
}

std::string MqttDeviceManager::cover_set_position_topic(uint32_t addr) const {
  return topic_prefix_ + "/" + addr_to_hex(addr) + "/set_position";
}

std::string MqttDeviceManager::light_cmd_topic(uint32_t addr) const {
  return topic_prefix_ + "/" + addr_to_hex(addr) + "/light/cmd";
}

std::string MqttDeviceManager::light_state_topic(uint32_t addr) const {
  return topic_prefix_ + "/" + addr_to_hex(addr) + "/light/state";
}

std::string MqttDeviceManager::discovery_topic(const char* type, uint32_t addr) const {
  return discovery_prefix_ + "/" + type + "/elero/" + addr_to_hex(addr) + "/config";
}

std::string MqttDeviceManager::addr_to_hex(uint32_t addr) {
  char buf[12];
  snprintf(buf, sizeof(buf), "0x%06x", addr);
  return std::string(buf);
}

}  // namespace esphome::elero
