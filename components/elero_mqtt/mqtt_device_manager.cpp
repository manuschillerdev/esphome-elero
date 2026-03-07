#include "mqtt_device_manager.h"
#include "../elero/elero_packet.h"
#include "../elero/elero_strings.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/json/json_util.h"
#include <cstdio>
#include <cstring>

namespace esphome {
namespace elero {

static const char *const TAG = "elero.mqtt";

/// Format a uint32_t as "0x%06x" for MQTT topic construction (no "0x" prefix)
static std::string addr_hex(uint32_t addr) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%06x", addr);
  return buf;
}

/// Format as "0x%06x" for JSON values
static std::string hex_str(uint32_t addr) {
  char buf[12];
  snprintf(buf, sizeof(buf), "0x%06x", addr);
  return buf;
}

/// Format as "0x%02x" for JSON values
static std::string hex_str8(uint8_t val) {
  char buf[8];
  snprintf(buf, sizeof(buf), "0x%02x", val);
  return buf;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Component Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::setup() {
  if (hub_ == nullptr) {
    ESP_LOGE(TAG, "Hub not set");
    this->mark_failed();
    return;
  }

  hub_->set_device_manager(this);
  init_slot_preferences_();

  // Restore slots from preferences
  size_t covers = 0, lights = 0, remotes = 0;

  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; i++) {
      if (!cover_slots_[i].restore()) continue;
      // Set callback BEFORE activate to avoid missing state changes
      cover_slots_[i].set_state_callback([this](EleroDynamicCover *c) { publish_cover_state_(c); });
      if (cover_slots_[i].activate(cover_slots_[i].config(), hub_)) {
        covers++;
      }
    }
  }

  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; i++) {
      if (!light_slots_[i].restore()) continue;
      light_slots_[i].set_state_callback([this](EleroDynamicLight *l) { publish_light_state_(l); });
      if (light_slots_[i].activate(light_slots_[i].config(), hub_)) {
        lights++;
      }
    }
  }

  if (remote_slots_ != nullptr) {
    for (size_t i = 0; i < max_remotes_; i++) {
      if (!remote_slots_[i].restore()) continue;
      remote_slots_[i].set_state_callback([this](EleroRemoteControl *r) { publish_remote_state_(r); });
      if (remote_slots_[i].activate(remote_slots_[i].get_address(), remote_slots_[i].get_title())) {
        remotes++;
      }
    }
  }

  ESP_LOGI(TAG, "MQTT mode: %d covers, %d lights, %d remotes restored", covers, lights, remotes);
}

void MqttDeviceManager::init_slot_preferences_() {
  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; i++) {
      uint32_t hash = fnv1_hash("elero_cover") + i;
      cover_slots_[i].set_preference(global_preferences->make_preference<NvsDeviceConfig>(hash));
    }
  }
  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; i++) {
      uint32_t hash = fnv1_hash("elero_light") + i;
      light_slots_[i].set_preference(global_preferences->make_preference<NvsDeviceConfig>(hash));
    }
  }
  if (remote_slots_ != nullptr) {
    for (size_t i = 0; i < max_remotes_; i++) {
      uint32_t hash = fnv1_hash("elero_remote") + i;
      remote_slots_[i].set_preference(global_preferences->make_preference<NvsDeviceConfig>(hash));
    }
  }
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
    if (find_active_cover_(config.dst_address) != nullptr) {
      ESP_LOGW(TAG, "Cover 0x%06x already exists", config.dst_address);
      return false;
    }
    auto *slot = find_free_cover_slot_();
    if (slot == nullptr) {
      ESP_LOGE(TAG, "No free cover slot for 0x%06x", config.dst_address);
      return false;
    }
    // Set callback BEFORE activate to avoid missing state changes
    slot->set_state_callback([this](EleroDynamicCover *c) { publish_cover_state_(c); });
    if (!slot->activate(config, hub_)) {
      ESP_LOGW(TAG, "Failed to activate cover 0x%06x", config.dst_address);
      return false;
    }
    (void)slot->save_config();
    if (config.is_enabled()) {
      publish_cover_discovery_(slot);
      subscribe_cover_commands_(slot);
    }

    notify_crud_("device_added", config.dst_address, "cover");
    return true;
  }

  if (config.is_light()) {
    if (find_active_light_(config.dst_address) != nullptr) {
      ESP_LOGW(TAG, "Light 0x%06x already exists", config.dst_address);
      return false;
    }
    auto *slot = find_free_light_slot_();
    if (slot == nullptr) {
      ESP_LOGE(TAG, "No free light slot for 0x%06x", config.dst_address);
      return false;
    }
    slot->set_state_callback([this](EleroDynamicLight *l) { publish_light_state_(l); });
    if (!slot->activate(config, hub_)) {
      ESP_LOGW(TAG, "Failed to activate light 0x%06x", config.dst_address);
      return false;
    }
    (void)slot->save_config();
    if (config.is_enabled()) {
      publish_light_discovery_(slot);
      subscribe_light_commands_(slot);
    }

    notify_crud_("device_added", config.dst_address, "light");
    return true;
  }

  if (config.is_remote()) {
    if (find_active_remote_(config.dst_address) != nullptr) {
      ESP_LOGW(TAG, "Remote 0x%06x already exists", config.dst_address);
      return false;
    }
    auto *slot = find_free_remote_slot_();
    if (slot == nullptr) {
      ESP_LOGE(TAG, "No free remote slot for 0x%06x", config.dst_address);
      return false;
    }
    slot->set_state_callback([this](EleroRemoteControl *r) { publish_remote_state_(r); });
    if (!slot->activate(config.dst_address, config.name)) {
      ESP_LOGW(TAG, "Failed to activate remote 0x%06x", config.dst_address);
      return false;
    }
    (void)slot->save_config();
    publish_remote_discovery_(slot);

    notify_crud_("device_added", config.dst_address, "remote");
    return true;
  }

  ESP_LOGW(TAG, "add_device: unknown device type %d", static_cast<int>(config.type));
  return false;
}

bool MqttDeviceManager::remove_device(DeviceType type, uint32_t dst_address) {
  bool removed = false;

  switch (type) {
    case DeviceType::COVER: {
      auto *slot = find_active_cover_(dst_address);
      if (slot == nullptr) return false;
      remove_discovery_("cover", dst_address);
      slot->deactivate();
      removed = true;
      break;
    }
    case DeviceType::LIGHT: {
      auto *slot = find_active_light_(dst_address);
      if (slot == nullptr) return false;
      remove_discovery_("light", dst_address);
      slot->deactivate();
      removed = true;
      break;
    }
    case DeviceType::REMOTE: {
      auto *slot = find_active_remote_(dst_address);
      if (slot == nullptr) return false;
      remove_discovery_("sensor", dst_address);
      slot->deactivate();
      removed = true;
      break;
    }
  }

  if (removed) {
    std::string resp = json::build_json([&](JsonObject root) {
      root["address"] = hex_str(dst_address);
    });
    notify_crud_("device_removed", resp.c_str());
  }
  return removed;
}

bool MqttDeviceManager::update_device(const NvsDeviceConfig &config) {
  // NOTE: update_device() can only change config fields (name, RF params, timing).
  // It cannot change address or device type — use remove_device() + add_device() for that.
  if (config.is_cover()) {
    auto *slot = find_active_cover_(config.dst_address);
    if (slot == nullptr) {
      ESP_LOGW(TAG, "update_device: no active cover at 0x%06x", config.dst_address);
      return false;
    }

    remove_discovery_("cover", config.dst_address);
    slot->update_config(config);

    if (config.is_enabled()) {
      publish_cover_discovery_(slot);
      subscribe_cover_commands_(slot);
    }
  } else if (config.is_light()) {
    auto *slot = find_active_light_(config.dst_address);
    if (slot == nullptr) {
      ESP_LOGW(TAG, "update_device: no active light at 0x%06x", config.dst_address);
      return false;
    }

    remove_discovery_("light", config.dst_address);
    slot->update_config(config);

    if (config.is_enabled()) {
      publish_light_discovery_(slot);
      subscribe_light_commands_(slot);
    }
  } else {
    ESP_LOGW(TAG, "update_device: unsupported type %d", static_cast<int>(config.type));
    return false;
  }

  notify_crud_("device_updated", config.dst_address, config.is_cover() ? "cover" : "light");
  return true;
}

bool MqttDeviceManager::set_device_enabled(DeviceType type, uint32_t dst_address, bool enabled) {
  switch (type) {
    case DeviceType::COVER: {
      auto *slot = find_active_cover_(dst_address);
      if (slot == nullptr) return false;

      slot->set_config_enabled(enabled);
      (void)slot->save_config();

      if (enabled && !slot->is_registered()) {
        slot->register_with_hub();
        publish_cover_discovery_(slot);
        subscribe_cover_commands_(slot);
      } else if (!enabled && slot->is_registered()) {
        slot->unregister_from_hub();
        remove_discovery_("cover", dst_address);
      }

      notify_crud_enabled_("device_enabled", dst_address, enabled);
      return true;
    }
    case DeviceType::LIGHT: {
      auto *slot = find_active_light_(dst_address);
      if (slot == nullptr) return false;

      slot->set_config_enabled(enabled);
      (void)slot->save_config();

      if (enabled && !slot->is_registered()) {
        slot->register_with_hub();
        publish_light_discovery_(slot);
        subscribe_light_commands_(slot);
      } else if (!enabled && slot->is_registered()) {
        slot->unregister_from_hub();
        remove_discovery_("light", dst_address);
      }

      notify_crud_enabled_("device_enabled", dst_address, enabled);
      return true;
    }
    default:
      return false;
  }
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

  char auto_name[NVS_NAME_MAX];
  snprintf(auto_name, sizeof(auto_name), "Remote 0x%06x", remote_addr);

  slot->set_state_callback([this](EleroRemoteControl *r) { publish_remote_state_(r); });
  slot->activate(remote_addr, auto_name);
  slot->update_from_packet(pkt.timestamp_ms, pkt.rssi, pkt.channel, pkt.command, pkt.dst);

  (void)slot->save_config();
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
  if (remote_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_remotes_; i++) {
    if (remote_slots_[i].is_active() && remote_slots_[i].get_address() == addr) {
      return &remote_slots_[i];
    }
  }
  return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MQTT Publishing (using ArduinoJson via ESPHome json component)
// ═══════════════════════════════════════════════════════════════════════════════

std::string MqttDeviceManager::device_id_() const {
  return App.get_name();
}

void MqttDeviceManager::publish_all_discoveries_() {
  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; i++) {
      if (cover_slots_[i].is_active() && cover_slots_[i].config().is_enabled()) {
        publish_cover_discovery_(&cover_slots_[i]);
        subscribe_cover_commands_(&cover_slots_[i]);
      }
    }
  }
  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; i++) {
      if (light_slots_[i].is_active() && light_slots_[i].config().is_enabled()) {
        publish_light_discovery_(&light_slots_[i]);
        subscribe_light_commands_(&light_slots_[i]);
      }
    }
  }
  if (remote_slots_ != nullptr) {
    for (size_t i = 0; i < max_remotes_; i++) {
      if (remote_slots_[i].is_active()) {
        publish_remote_discovery_(&remote_slots_[i]);
      }
    }
  }
}

void MqttDeviceManager::publish_cover_discovery_(EleroDynamicCover *cover) {
  auto hex = addr_hex(cover->get_blind_address());
  auto dev_id = device_id_();

  std::string topic = discovery_prefix_ + "/cover/" + dev_id + "_" + hex + "/config";
  std::string state_topic = topic_prefix_ + "/cover/" + hex + "/state";
  std::string cmd_topic = topic_prefix_ + "/cover/" + hex + "/set";

  std::string payload = json::build_json([&](JsonObject root) {
    root["name"] = cover->get_blind_name();
    root["unique_id"] = dev_id + "_" + hex;
    root["command_topic"] = cmd_topic;
    root["state_topic"] = state_topic;
    root["position_topic"] = state_topic;
    root["payload_open"] = "open";
    root["payload_close"] = "close";
    root["payload_stop"] = "stop";
    JsonObject dev = root["device"].to<JsonObject>();
    dev["identifiers"][0] = dev_id;
    dev["name"] = device_name_;
    dev["manufacturer"] = "Elero";
  });

  mqtt_.publish(topic.c_str(), payload.c_str(), true);
  ESP_LOGD(TAG, "Published cover discovery for 0x%06x", cover->get_blind_address());
}

void MqttDeviceManager::publish_light_discovery_(EleroDynamicLight *light) {
  auto hex = addr_hex(light->get_blind_address());
  auto dev_id = device_id_();

  std::string topic = discovery_prefix_ + "/light/" + dev_id + "_" + hex + "/config";
  std::string state_topic = topic_prefix_ + "/light/" + hex + "/state";
  std::string cmd_topic = topic_prefix_ + "/light/" + hex + "/set";

  std::string payload = json::build_json([&](JsonObject root) {
    root["name"] = light->get_light_name();
    root["unique_id"] = dev_id + "_" + hex;
    root["command_topic"] = cmd_topic;
    root["state_topic"] = state_topic;
    root["payload_on"] = "on";
    root["payload_off"] = "off";
    JsonObject dev = root["device"].to<JsonObject>();
    dev["identifiers"][0] = dev_id;
    dev["name"] = device_name_;
    dev["manufacturer"] = "Elero";
  });

  mqtt_.publish(topic.c_str(), payload.c_str(), true);
  ESP_LOGD(TAG, "Published light discovery for 0x%06x", light->get_blind_address());
}

void MqttDeviceManager::publish_remote_discovery_(EleroRemoteControl *remote) {
  auto hex = addr_hex(remote->get_address());
  auto dev_id = device_id_();

  std::string topic = discovery_prefix_ + "/sensor/" + dev_id + "_remote_" + hex + "/config";
  std::string state_topic = topic_prefix_ + "/remote/" + hex + "/state";

  std::string payload = json::build_json([&](JsonObject root) {
    root["name"] = remote->get_title();
    root["unique_id"] = dev_id + "_remote_" + hex;
    root["state_topic"] = state_topic;
    root["value_template"] = "{{ value_json.rssi }}";
    root["unit_of_measurement"] = "dBm";
    root["device_class"] = "signal_strength";
    root["json_attributes_topic"] = state_topic;
    root["json_attributes_template"] = "{{ value_json | tojson }}";
    JsonObject dev = root["device"].to<JsonObject>();
    dev["identifiers"][0] = dev_id;
    dev["name"] = device_name_;
    dev["manufacturer"] = "Elero";
  });

  mqtt_.publish(topic.c_str(), payload.c_str(), true);
  ESP_LOGD(TAG, "Published remote discovery for 0x%06x", remote->get_address());
}

void MqttDeviceManager::remove_discovery_(const char *component, uint32_t addr) {
  auto hex = addr_hex(addr);
  auto dev_id = device_id_();

  std::string topic = discovery_prefix_ + "/" + component + "/" + dev_id + "_" + hex + "/config";
  mqtt_.publish(topic.c_str(), "", true);
}

void MqttDeviceManager::publish_cover_state_(EleroDynamicCover *cover) {
  if (!mqtt_.is_connected()) return;

  auto hex = addr_hex(cover->get_blind_address());
  std::string topic = topic_prefix_ + "/cover/" + hex + "/state";

  std::string payload = json::build_json([&](JsonObject root) {
    root["state"] = cover->get_operation_str();
    root["position"] = static_cast<int>(cover->get_cover_position() * 100.0f);
    root["rssi"] = static_cast<float>(static_cast<int>(cover->get_last_rssi() * 10)) / 10.0f;
  });

  mqtt_.publish(topic.c_str(), payload.c_str(), false);
}

void MqttDeviceManager::publish_light_state_(EleroDynamicLight *light) {
  if (!mqtt_.is_connected()) return;

  auto hex = addr_hex(light->get_blind_address());
  std::string topic = topic_prefix_ + "/light/" + hex + "/state";

  std::string payload = json::build_json([&](JsonObject root) {
    root["state"] = light->get_is_on() ? "ON" : "OFF";
  });

  mqtt_.publish(topic.c_str(), payload.c_str(), false);
}

void MqttDeviceManager::publish_remote_state_(EleroRemoteControl *remote) {
  if (!mqtt_.is_connected()) return;

  auto hex = addr_hex(remote->get_address());
  std::string topic = topic_prefix_ + "/remote/" + hex + "/state";

  std::string payload = json::build_json([&](JsonObject root) {
    root["rssi"] = remote->get_rssi();
    root["address"] = hex_str(remote->get_address());
    root["title"] = remote->get_title();
    root["last_seen"] = remote->get_last_seen_ms();
    root["last_channel"] = remote->get_last_channel();
    root["last_command"] = hex_str8(remote->get_last_command());
    root["last_target"] = hex_str(remote->get_last_target());
  });

  mqtt_.publish(topic.c_str(), payload.c_str(), false);
}

void MqttDeviceManager::subscribe_cover_commands_(EleroDynamicCover *cover) {
  auto hex = addr_hex(cover->get_blind_address());
  std::string topic = topic_prefix_ + "/cover/" + hex + "/set";

  uint32_t addr = cover->get_blind_address();
  mqtt_.subscribe(topic.c_str(), [this, addr](const char *, const char *payload) {
    auto *c = find_active_cover_(addr);
    if (c != nullptr) {
      c->perform_action(payload);
    }
  });
}

void MqttDeviceManager::subscribe_light_commands_(EleroDynamicLight *light) {
  auto hex = addr_hex(light->get_blind_address());
  std::string topic = topic_prefix_ + "/light/" + hex + "/set";

  uint32_t addr = light->get_blind_address();
  mqtt_.subscribe(topic.c_str(), [this, addr](const char *, const char *payload) {
    auto *l = find_active_light_(addr);
    if (l != nullptr) {
      l->perform_action(payload);
    }
  });
}

// ═══════════════════════════════════════════════════════════════════════════════
// CRUD Event Notification
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::notify_crud_(const char *event, const char *json_str) {
  if (crud_callback_) {
    crud_callback_(event, json_str);
  }
}

void MqttDeviceManager::notify_crud_(const char *event, uint32_t addr, const char *device_type) {
  std::string resp = json::build_json([&](JsonObject root) {
    root["address"] = hex_str(addr);
    root["device_type"] = device_type;
  });
  notify_crud_(event, resp.c_str());
}

void MqttDeviceManager::notify_crud_enabled_(const char *event, uint32_t addr, bool enabled) {
  std::string resp = json::build_json([&](JsonObject root) {
    root["address"] = hex_str(addr);
    root["enabled"] = enabled;
  });
  notify_crud_(event, resp.c_str());
}

}  // namespace elero
}  // namespace esphome
