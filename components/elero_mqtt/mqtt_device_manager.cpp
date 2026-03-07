#include "mqtt_device_manager.h"
#include "../elero/elero_packet.h"
#include "../elero/elero_strings.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstdio>
#include <cstring>

namespace esphome {
namespace elero {

static const char *const TAG = "elero.mqtt";

/// Escape a string for safe embedding in JSON values.
static std::string json_escape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
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
    (void)slot->save_config();  // Best-effort; save_config logs on failure
    if (config.is_enabled()) {
      publish_cover_discovery_(slot);
      subscribe_cover_commands_(slot);
    }

    char resp[96];
    snprintf(resp, sizeof(resp), "{\"address\":\"0x%06x\",\"device_type\":\"cover\"}", config.dst_address);
    notify_crud_("device_added", resp);
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
    (void)slot->save_config();  // Best-effort; save_config logs on failure
    if (config.is_enabled()) {
      publish_light_discovery_(slot);
      subscribe_light_commands_(slot);
    }

    char resp[96];
    snprintf(resp, sizeof(resp), "{\"address\":\"0x%06x\",\"device_type\":\"light\"}", config.dst_address);
    notify_crud_("device_added", resp);
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
    (void)slot->save_config();  // Best-effort; save_config logs on failure
    publish_remote_discovery_(slot);

    char resp[96];
    snprintf(resp, sizeof(resp), "{\"address\":\"0x%06x\",\"device_type\":\"remote\"}", config.dst_address);
    notify_crud_("device_added", resp);
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
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"address\":\"0x%06x\"}", dst_address);
    notify_crud_("device_removed", resp);
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

    // Remove old MQTT discovery (name/config may have changed)
    remove_discovery_("cover", config.dst_address);

    // Non-destructive in-place update (handles unregister/re-register internally)
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

  char resp[96];
  snprintf(resp, sizeof(resp), "{\"address\":\"0x%06x\",\"device_type\":\"%s\"}",
           config.dst_address, config.is_cover() ? "cover" : "light");
  notify_crud_("device_updated", resp);
  return true;
}

bool MqttDeviceManager::set_device_enabled(DeviceType type, uint32_t dst_address, bool enabled) {
  switch (type) {
    case DeviceType::COVER: {
      auto *slot = find_active_cover_(dst_address);
      if (slot == nullptr) return false;

      slot->set_config_enabled(enabled);
      (void)slot->save_config();  // Best-effort; save_config logs on failure

      if (enabled && !slot->is_registered()) {
        slot->register_with_hub();
        publish_cover_discovery_(slot);
        subscribe_cover_commands_(slot);
      } else if (!enabled && slot->is_registered()) {
        slot->unregister_from_hub();
        remove_discovery_("cover", dst_address);
      }

      char resp[96];
      snprintf(resp, sizeof(resp), "{\"address\":\"0x%06x\",\"enabled\":%s}",
               dst_address, enabled ? "true" : "false");
      notify_crud_("device_enabled", resp);
      return true;
    }
    case DeviceType::LIGHT: {
      auto *slot = find_active_light_(dst_address);
      if (slot == nullptr) return false;

      slot->set_config_enabled(enabled);
      (void)slot->save_config();  // Best-effort; save_config logs on failure

      if (enabled && !slot->is_registered()) {
        slot->register_with_hub();
        publish_light_discovery_(slot);
        subscribe_light_commands_(slot);
      } else if (!enabled && slot->is_registered()) {
        slot->unregister_from_hub();
        remove_discovery_("light", dst_address);
      }

      char resp[96];
      snprintf(resp, sizeof(resp), "{\"address\":\"0x%06x\",\"enabled\":%s}",
               dst_address, enabled ? "true" : "false");
      notify_crud_("device_enabled", resp);
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

  (void)slot->save_config();  // Best-effort; save_config logs on failure
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
// MQTT Publishing (using std::string for safe JSON construction)
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
  auto hex = addr_hex_(cover->get_blind_address());
  auto dev_id = device_id_();

  // Discovery topic
  std::string topic = discovery_prefix_ + "/cover/" + dev_id + "_" + hex + "/config";

  // State/command topics
  std::string state_topic = topic_prefix_ + "/cover/" + hex + "/state";
  std::string cmd_topic = topic_prefix_ + "/cover/" + hex + "/set";

  // Build discovery payload using string concatenation (safe for any name length)
  std::string payload = "{";
  payload += "\"name\":\"" + json_escape(cover->get_blind_name()) + "\",";
  payload += "\"unique_id\":\"" + dev_id + "_" + hex + "\",";
  payload += "\"command_topic\":\"" + cmd_topic + "\",";
  payload += "\"state_topic\":\"" + state_topic + "\",";
  payload += "\"position_topic\":\"" + state_topic + "\",";
  payload += "\"payload_open\":\"open\",";
  payload += "\"payload_close\":\"close\",";
  payload += "\"payload_stop\":\"stop\",";
  payload += "\"device\":{\"identifiers\":[\"" + dev_id + "\"],";
  payload += "\"name\":\"" + json_escape(device_name_) + "\",\"manufacturer\":\"Elero\"}}";

  mqtt_.publish(topic.c_str(), payload.c_str(), true);
  ESP_LOGD(TAG, "Published cover discovery for 0x%06x", cover->get_blind_address());
}

void MqttDeviceManager::publish_light_discovery_(EleroDynamicLight *light) {
  auto hex = addr_hex_(light->get_blind_address());
  auto dev_id = device_id_();

  std::string topic = discovery_prefix_ + "/light/" + dev_id + "_" + hex + "/config";
  std::string state_topic = topic_prefix_ + "/light/" + hex + "/state";
  std::string cmd_topic = topic_prefix_ + "/light/" + hex + "/set";

  std::string payload = "{";
  payload += "\"name\":\"" + json_escape(light->get_light_name()) + "\",";
  payload += "\"unique_id\":\"" + dev_id + "_" + hex + "\",";
  payload += "\"command_topic\":\"" + cmd_topic + "\",";
  payload += "\"state_topic\":\"" + state_topic + "\",";
  payload += "\"payload_on\":\"on\",";
  payload += "\"payload_off\":\"off\",";
  payload += "\"device\":{\"identifiers\":[\"" + dev_id + "\"],";
  payload += "\"name\":\"" + json_escape(device_name_) + "\",\"manufacturer\":\"Elero\"}}";

  mqtt_.publish(topic.c_str(), payload.c_str(), true);
  ESP_LOGD(TAG, "Published light discovery for 0x%06x", light->get_blind_address());
}

void MqttDeviceManager::publish_remote_discovery_(EleroRemoteControl *remote) {
  auto hex = addr_hex_(remote->get_address());
  auto dev_id = device_id_();

  std::string topic = discovery_prefix_ + "/sensor/" + dev_id + "_remote_" + hex + "/config";
  std::string state_topic = topic_prefix_ + "/remote/" + hex + "/state";

  std::string payload = "{";
  payload += "\"name\":\"" + json_escape(std::string(remote->get_title())) + "\",";
  payload += "\"unique_id\":\"" + dev_id + "_remote_" + hex + "\",";
  payload += "\"state_topic\":\"" + state_topic + "\",";
  payload += "\"value_template\":\"{{ value_json.rssi }}\",";
  payload += "\"unit_of_measurement\":\"dBm\",";
  payload += "\"device_class\":\"signal_strength\",";
  payload += "\"json_attributes_topic\":\"" + state_topic + "\",";
  payload += "\"json_attributes_template\":\"{{ value_json | tojson }}\",";
  payload += "\"device\":{\"identifiers\":[\"" + dev_id + "\"],";
  payload += "\"name\":\"" + json_escape(device_name_) + "\",\"manufacturer\":\"Elero\"}}";

  mqtt_.publish(topic.c_str(), payload.c_str(), true);
  ESP_LOGD(TAG, "Published remote discovery for 0x%06x", remote->get_address());
}

void MqttDeviceManager::remove_discovery_(const char *component, uint32_t addr) {
  auto hex = addr_hex_(addr);
  auto dev_id = device_id_();

  std::string topic = discovery_prefix_ + "/" + component + "/" + dev_id + "_" + hex + "/config";

  // Empty payload removes discovery
  mqtt_.publish(topic.c_str(), "", true);
}

void MqttDeviceManager::publish_cover_state_(EleroDynamicCover *cover) {
  if (!mqtt_.is_connected()) return;

  auto hex = addr_hex_(cover->get_blind_address());
  std::string topic = topic_prefix_ + "/cover/" + hex + "/state";

  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"state\":\"%s\",\"position\":%.0f,\"rssi\":%.1f}",
           cover->get_operation_str(),
           cover->get_cover_position() * 100.0f,
           cover->get_last_rssi());

  mqtt_.publish(topic.c_str(), payload, false);
}

void MqttDeviceManager::publish_light_state_(EleroDynamicLight *light) {
  if (!mqtt_.is_connected()) return;

  auto hex = addr_hex_(light->get_blind_address());
  std::string topic = topic_prefix_ + "/light/" + hex + "/state";

  char payload[64];
  snprintf(payload, sizeof(payload),
           "{\"state\":\"%s\"}",
           light->get_is_on() ? "ON" : "OFF");

  mqtt_.publish(topic.c_str(), payload, false);
}

void MqttDeviceManager::publish_remote_state_(EleroRemoteControl *remote) {
  if (!mqtt_.is_connected()) return;

  auto hex = addr_hex_(remote->get_address());
  std::string topic = topic_prefix_ + "/remote/" + hex + "/state";

  char buf[196];
  snprintf(buf, sizeof(buf),
           "{\"rssi\":%.1f,"
           "\"address\":\"0x%06x\","
           "\"title\":\"%s\","
           "\"last_seen\":%lu,"
           "\"last_channel\":%d,"
           "\"last_command\":\"0x%02x\","
           "\"last_target\":\"0x%06x\"}",
           remote->get_rssi(),
           remote->get_address(),
           json_escape(std::string(remote->get_title())).c_str(),
           (unsigned long) remote->get_last_seen_ms(),
           remote->get_last_channel(),
           remote->get_last_command(),
           remote->get_last_target());

  mqtt_.publish(topic.c_str(), buf, false);
}

void MqttDeviceManager::subscribe_cover_commands_(EleroDynamicCover *cover) {
  auto hex = addr_hex_(cover->get_blind_address());
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
  auto hex = addr_hex_(light->get_blind_address());
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

void MqttDeviceManager::notify_crud_(const char *event, const char *json) {
  if (crud_callback_) {
    crud_callback_(event, json);
  }
}

}  // namespace elero
}  // namespace esphome
