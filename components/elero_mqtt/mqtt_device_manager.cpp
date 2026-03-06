#include "mqtt_device_manager.h"
#include "../elero/elero_packet.h"
#include "../elero/elero_strings.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstdio>
#include <cstring>

namespace esphome::elero {

static constexpr const char *TAG = "elero.mqtt";

// ═══════════════════════════════════════════════════════════════════════════════
// Component Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::setup() {
  if (hub_ == nullptr) {
    ESP_LOGE(TAG, "Hub not set");
    this->mark_failed();
    return;
  }

  // Register ourselves as the hub's device manager
  hub_->set_device_manager(this);

  // Load devices from NVS and activate slots
  std::vector<NvsDeviceConfig> covers, lights, remotes;
  nvs_.load_devices(DeviceType::COVER, covers);
  nvs_.load_devices(DeviceType::LIGHT, lights);
  nvs_.load_devices(DeviceType::REMOTE, remotes);

  for (auto &cfg : covers) {
    auto *slot = find_free_cover_slot_();
    if (slot == nullptr) {
      ESP_LOGW(TAG, "No free cover slot for 0x%06x", cfg.dst_address);
      continue;
    }
    if (slot->activate(cfg, hub_)) {
      slot->set_state_callback([this](EleroDynamicCover *c) { publish_cover_state_(c); });
    }
  }

  for (auto &cfg : lights) {
    auto *slot = find_free_light_slot_();
    if (slot == nullptr) {
      ESP_LOGW(TAG, "No free light slot for 0x%06x", cfg.dst_address);
      continue;
    }
    if (slot->activate(cfg, hub_)) {
      slot->set_state_callback([this](EleroDynamicLight *l) { publish_light_state_(l); });
    }
  }

  for (auto &cfg : remotes) {
    auto *slot = find_free_remote_slot_();
    if (slot == nullptr) {
      ESP_LOGW(TAG, "No free remote slot for 0x%06x", cfg.dst_address);
      continue;
    }
    slot->set_address(cfg.dst_address);
    slot->set_title(cfg.name);
    slot->set_state_callback([this](EleroRemoteControl *r) { publish_remote_state_(r); });
    active_remotes_[cfg.dst_address] = slot;
  }

  ESP_LOGI(TAG, "MQTT mode: %d covers, %d lights, %d remotes loaded from NVS",
           covers.size(), lights.size(), remotes.size());
}

void MqttDeviceManager::loop() {
  uint32_t now = millis();

  // Detect MQTT (re)connection and publish discoveries
  bool connected = mqtt_.is_connected();
  if (connected && !mqtt_was_connected_) {
    ESP_LOGI(TAG, "MQTT connected, publishing discoveries");
    publish_all_discoveries_();
  }
  mqtt_was_connected_ = connected;

  // Loop active dynamic covers
  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; i++) {
      if (cover_slots_[i].is_active()) {
        cover_slots_[i].loop(now);
      }
    }
  }

  // Loop active dynamic lights
  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; i++) {
      if (light_slots_[i].is_active()) {
        light_slots_[i].loop(now);
      }
    }
  }
}

void MqttDeviceManager::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero MQTT Device Manager:");
  ESP_LOGCONFIG(TAG, "  Topic prefix: %s", topic_prefix_.c_str());
  ESP_LOGCONFIG(TAG, "  Discovery prefix: %s", discovery_prefix_.c_str());
  ESP_LOGCONFIG(TAG, "  Max covers: %d", max_covers_);
  ESP_LOGCONFIG(TAG, "  Max lights: %d", max_lights_);
  ESP_LOGCONFIG(TAG, "  Max remotes: %d", max_remotes_);
}

// ═══════════════════════════════════════════════════════════════════════════════
// RF Packet Handler
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::on_rf_packet(const RfPacketInfo &pkt) {
  // Track remote controls from command packets
  if (packet::is_command_packet(pkt.type)) {
    track_remote_(pkt);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Device CRUD
// ═══════════════════════════════════════════════════════════════════════════════

bool MqttDeviceManager::add_device(const NvsDeviceConfig &config) {
  if (config.is_cover()) {
    auto *slot = find_free_cover_slot_();
    if (slot == nullptr) {
      ESP_LOGE(TAG, "No free cover slot");
      return false;
    }
    if (!slot->activate(config, hub_)) {
      return false;
    }
    slot->set_state_callback([this](EleroDynamicCover *c) { publish_cover_state_(c); });
    if (!nvs_.save_device(config)) {
      slot->deactivate();
      return false;
    }
    publish_cover_discovery_(slot);
    subscribe_cover_commands_(slot);
    return true;
  }

  if (config.is_light()) {
    auto *slot = find_free_light_slot_();
    if (slot == nullptr) {
      ESP_LOGE(TAG, "No free light slot");
      return false;
    }
    if (!slot->activate(config, hub_)) {
      return false;
    }
    slot->set_state_callback([this](EleroDynamicLight *l) { publish_light_state_(l); });
    if (!nvs_.save_device(config)) {
      slot->deactivate();
      return false;
    }
    publish_light_discovery_(slot);
    subscribe_light_commands_(slot);
    return true;
  }

  if (config.is_remote()) {
    auto *slot = find_free_remote_slot_();
    if (slot == nullptr) {
      ESP_LOGE(TAG, "No free remote slot");
      return false;
    }
    slot->set_address(config.dst_address);
    slot->set_title(config.name);
    slot->set_state_callback([this](EleroRemoteControl *r) { publish_remote_state_(r); });
    active_remotes_[config.dst_address] = slot;
    if (!nvs_.save_device(config)) {
      slot->deactivate();
      active_remotes_.erase(config.dst_address);
      return false;
    }
    publish_remote_discovery_(slot);
    return true;
  }

  return false;
}

bool MqttDeviceManager::remove_device(DeviceType type, uint32_t dst_address) {
  switch (type) {
    case DeviceType::COVER: {
      auto *slot = find_active_cover_(dst_address);
      if (slot == nullptr) return false;
      remove_discovery_("cover", dst_address);
      slot->deactivate();
      break;
    }
    case DeviceType::LIGHT: {
      auto *slot = find_active_light_(dst_address);
      if (slot == nullptr) return false;
      remove_discovery_("light", dst_address);
      slot->deactivate();
      break;
    }
    case DeviceType::REMOTE: {
      auto *slot = find_active_remote_(dst_address);
      if (slot == nullptr) return false;
      remove_discovery_("sensor", dst_address);
      slot->deactivate();
      active_remotes_.erase(dst_address);
      break;
    }
  }
  return nvs_.remove_device(type, dst_address);
}

bool MqttDeviceManager::update_device(const NvsDeviceConfig &config) {
  // For simplicity: remove and re-add
  remove_device(config.type, config.dst_address);
  return add_device(config);
}

bool MqttDeviceManager::set_device_enabled(DeviceType type, uint32_t dst_address, bool enabled) {
  // Load, modify, save
  std::vector<NvsDeviceConfig> devices;
  nvs_.load_devices(type, devices);
  for (auto &d : devices) {
    if (d.dst_address == dst_address) {
      d.set_enabled(enabled);
      return nvs_.save_devices(type, devices);
    }
  }
  return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Remote Control Tracking
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::track_remote_(const RfPacketInfo &pkt) {
  uint32_t remote_addr = pkt.src;
  if (remote_addr == 0) return;

  // Check if already tracked
  auto *existing = find_active_remote_(remote_addr);
  if (existing != nullptr) {
    existing->update_from_packet(pkt.timestamp_ms, pkt.rssi, pkt.channel,
                                  pkt.command, pkt.dst);
    return;
  }

  // Auto-discover: create a new remote entry if we have a free slot
  auto *slot = find_free_remote_slot_();
  if (slot == nullptr) {
    ESP_LOGV(TAG, "No free remote slot for new remote 0x%06x", remote_addr);
    return;
  }

  // Auto-name with hex address
  char auto_name[24];
  snprintf(auto_name, sizeof(auto_name), "Remote 0x%06x", remote_addr);

  slot->set_address(remote_addr);
  slot->set_title(auto_name);
  slot->set_state_callback([this](EleroRemoteControl *r) { publish_remote_state_(r); });
  slot->update_from_packet(pkt.timestamp_ms, pkt.rssi, pkt.channel, pkt.command, pkt.dst);
  active_remotes_[remote_addr] = slot;

  // Save to NVS
  NvsDeviceConfig cfg{};
  cfg.type = DeviceType::REMOTE;
  cfg.dst_address = remote_addr;
  cfg.set_name(auto_name);
  nvs_.save_device(cfg);

  // Publish MQTT discovery
  publish_remote_discovery_(slot);

  ESP_LOGI(TAG, "Auto-discovered remote 0x%06x", remote_addr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Slot Management
// ═══════════════════════════════════════════════════════════════════════════════

EleroDynamicCover *MqttDeviceManager::find_free_cover_slot_() {
  if (cover_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_covers_; i++) {
    if (!cover_slots_[i].is_active()) return &cover_slots_[i];
  }
  return nullptr;
}

EleroDynamicLight *MqttDeviceManager::find_free_light_slot_() {
  if (light_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_lights_; i++) {
    if (!light_slots_[i].is_active()) return &light_slots_[i];
  }
  return nullptr;
}

EleroRemoteControl *MqttDeviceManager::find_free_remote_slot_() {
  if (remote_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_remotes_; i++) {
    if (!remote_slots_[i].is_active()) return &remote_slots_[i];
  }
  return nullptr;
}

EleroDynamicCover *MqttDeviceManager::find_active_cover_(uint32_t addr) {
  if (cover_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_covers_; i++) {
    if (cover_slots_[i].is_active() && cover_slots_[i].get_blind_address() == addr) {
      return &cover_slots_[i];
    }
  }
  return nullptr;
}

EleroDynamicLight *MqttDeviceManager::find_active_light_(uint32_t addr) {
  if (light_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_lights_; i++) {
    if (light_slots_[i].is_active() && light_slots_[i].get_blind_address() == addr) {
      return &light_slots_[i];
    }
  }
  return nullptr;
}

EleroRemoteControl *MqttDeviceManager::find_active_remote_(uint32_t addr) {
  auto it = active_remotes_.find(addr);
  return (it != active_remotes_.end()) ? it->second : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MQTT Publishing
// ═══════════════════════════════════════════════════════════════════════════════

std::string MqttDeviceManager::device_id_() const {
  return App.get_name();
}

std::string MqttDeviceManager::addr_hex_(uint32_t addr) const {
  char buf[12];
  snprintf(buf, sizeof(buf), "%06x", addr);
  return std::string(buf);
}

void MqttDeviceManager::publish_all_discoveries_() {
  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; i++) {
      if (cover_slots_[i].is_active()) {
        publish_cover_discovery_(&cover_slots_[i]);
        subscribe_cover_commands_(&cover_slots_[i]);
      }
    }
  }
  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; i++) {
      if (light_slots_[i].is_active()) {
        publish_light_discovery_(&light_slots_[i]);
        subscribe_light_commands_(&light_slots_[i]);
      }
    }
  }
  for (auto &[addr, remote] : active_remotes_) {
    publish_remote_discovery_(remote);
  }
}

void MqttDeviceManager::publish_cover_discovery_(EleroDynamicCover *cover) {
  auto hex = addr_hex_(cover->get_blind_address());
  auto dev_id = device_id_();

  // Discovery topic
  char topic[128];
  snprintf(topic, sizeof(topic), "%s/cover/%s_%s/config",
           discovery_prefix_.c_str(), dev_id.c_str(), hex.c_str());

  // State/command topics
  char state_topic[64], cmd_topic[64];
  snprintf(state_topic, sizeof(state_topic), "%s/cover/%s/state",
           topic_prefix_.c_str(), hex.c_str());
  snprintf(cmd_topic, sizeof(cmd_topic), "%s/cover/%s/set",
           topic_prefix_.c_str(), hex.c_str());

  // Build discovery payload
  char payload[512];
  snprintf(payload, sizeof(payload),
           "{"
           "\"name\":\"%s\","
           "\"unique_id\":\"%s_%s\","
           "\"command_topic\":\"%s\","
           "\"state_topic\":\"%s\","
           "\"position_topic\":\"%s\","
           "\"payload_open\":\"open\","
           "\"payload_close\":\"close\","
           "\"payload_stop\":\"stop\","
           "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"Elero\"}"
           "}",
           cover->get_blind_name().c_str(),
           dev_id.c_str(), hex.c_str(),
           cmd_topic, state_topic, state_topic,
           dev_id.c_str(), device_name_.c_str());

  mqtt_.publish(topic, payload, true);
  ESP_LOGD(TAG, "Published cover discovery for 0x%06x", cover->get_blind_address());
}

void MqttDeviceManager::publish_light_discovery_(EleroDynamicLight *light) {
  auto hex = addr_hex_(light->get_blind_address());
  auto dev_id = device_id_();

  char topic[128];
  snprintf(topic, sizeof(topic), "%s/light/%s_%s/config",
           discovery_prefix_.c_str(), dev_id.c_str(), hex.c_str());

  char state_topic[64], cmd_topic[64];
  snprintf(state_topic, sizeof(state_topic), "%s/light/%s/state",
           topic_prefix_.c_str(), hex.c_str());
  snprintf(cmd_topic, sizeof(cmd_topic), "%s/light/%s/set",
           topic_prefix_.c_str(), hex.c_str());

  char payload[512];
  snprintf(payload, sizeof(payload),
           "{"
           "\"name\":\"%s\","
           "\"unique_id\":\"%s_%s\","
           "\"command_topic\":\"%s\","
           "\"state_topic\":\"%s\","
           "\"payload_on\":\"on\","
           "\"payload_off\":\"off\","
           "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"Elero\"}"
           "}",
           light->get_light_name().c_str(),
           dev_id.c_str(), hex.c_str(),
           cmd_topic, state_topic,
           dev_id.c_str(), device_name_.c_str());

  mqtt_.publish(topic, payload, true);
  ESP_LOGD(TAG, "Published light discovery for 0x%06x", light->get_blind_address());
}

void MqttDeviceManager::publish_remote_discovery_(EleroRemoteControl *remote) {
  auto hex = addr_hex_(remote->get_address());
  auto dev_id = device_id_();

  // Publish as a sensor with RSSI, last command, and last target
  char topic[128];
  snprintf(topic, sizeof(topic), "%s/sensor/%s_remote_%s/config",
           discovery_prefix_.c_str(), dev_id.c_str(), hex.c_str());

  char state_topic[64];
  snprintf(state_topic, sizeof(state_topic), "%s/remote/%s/state",
           topic_prefix_.c_str(), hex.c_str());

  char payload[512];
  snprintf(payload, sizeof(payload),
           "{"
           "\"name\":\"%s\","
           "\"unique_id\":\"%s_remote_%s\","
           "\"state_topic\":\"%s\","
           "\"value_template\":\"{{ value_json.rssi }}\","
           "\"unit_of_measurement\":\"dBm\","
           "\"device_class\":\"signal_strength\","
           "\"json_attributes_topic\":\"%s\","
           "\"json_attributes_template\":\"{{ value_json | tojson }}\","
           "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"Elero\"}"
           "}",
           remote->get_title(),
           dev_id.c_str(), hex.c_str(),
           state_topic, state_topic,
           dev_id.c_str(), device_name_.c_str());

  mqtt_.publish(topic, payload, true);
  ESP_LOGD(TAG, "Published remote discovery for 0x%06x", remote->get_address());
}

void MqttDeviceManager::remove_discovery_(const char *component, uint32_t addr) {
  auto hex = addr_hex_(addr);
  auto dev_id = device_id_();

  char topic[128];
  snprintf(topic, sizeof(topic), "%s/%s/%s_%s/config",
           discovery_prefix_.c_str(), component, dev_id.c_str(), hex.c_str());

  // Empty payload removes discovery
  mqtt_.publish(topic, "", true);
}

void MqttDeviceManager::publish_cover_state_(EleroDynamicCover *cover) {
  if (!mqtt_.is_connected()) return;

  auto hex = addr_hex_(cover->get_blind_address());
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/cover/%s/state",
           topic_prefix_.c_str(), hex.c_str());

  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"state\":\"%s\",\"position\":%.0f,\"rssi\":%.1f}",
           cover->get_operation_str(),
           cover->get_cover_position() * 100.0f,
           cover->get_last_rssi());

  mqtt_.publish(topic, payload, false);
}

void MqttDeviceManager::publish_light_state_(EleroDynamicLight *light) {
  if (!mqtt_.is_connected()) return;

  auto hex = addr_hex_(light->get_blind_address());
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/light/%s/state",
           topic_prefix_.c_str(), hex.c_str());

  char payload[64];
  snprintf(payload, sizeof(payload),
           "{\"state\":\"%s\"}",
           light->get_is_on() ? "ON" : "OFF");

  mqtt_.publish(topic, payload, false);
}

void MqttDeviceManager::publish_remote_state_(EleroRemoteControl *remote) {
  if (!mqtt_.is_connected()) return;

  auto hex = addr_hex_(remote->get_address());
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/remote/%s/state",
           topic_prefix_.c_str(), hex.c_str());

  char payload[196];
  snprintf(payload, sizeof(payload),
           "{\"rssi\":%.1f,"
           "\"address\":\"0x%06x\","
           "\"title\":\"%s\","
           "\"last_seen\":%lu,"
           "\"last_channel\":%d,"
           "\"last_command\":\"0x%02x\","
           "\"last_target\":\"0x%06x\"}",
           remote->get_rssi(),
           remote->get_address(),
           remote->get_title(),
           (unsigned long) remote->get_last_seen_ms(),
           remote->get_last_channel(),
           remote->get_last_command(),
           remote->get_last_target());

  mqtt_.publish(topic, payload, false);
}

void MqttDeviceManager::subscribe_cover_commands_(EleroDynamicCover *cover) {
  auto hex = addr_hex_(cover->get_blind_address());
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/cover/%s/set",
           topic_prefix_.c_str(), hex.c_str());

  uint32_t addr = cover->get_blind_address();
  mqtt_.subscribe(topic, [this, addr](const char *, const char *payload) {
    auto *c = find_active_cover_(addr);
    if (c != nullptr) {
      c->perform_action(payload);
    }
  });
}

void MqttDeviceManager::subscribe_light_commands_(EleroDynamicLight *light) {
  auto hex = addr_hex_(light->get_blind_address());
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/light/%s/set",
           topic_prefix_.c_str(), hex.c_str());

  uint32_t addr = light->get_blind_address();
  mqtt_.subscribe(topic, [this, addr](const char *, const char *payload) {
    auto *l = find_active_light_(addr);
    if (l != nullptr) {
      l->perform_action(payload);
    }
  });
}

}  // namespace esphome::elero
