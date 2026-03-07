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

/// Format a uint32_t as "%06x" for MQTT topic construction (no "0x" prefix)
static std::string addr_hex(uint32_t addr) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%06x", addr);
  return buf;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Config
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::dump_config() {
  NvsDeviceManagerBase::dump_config();
  ESP_LOGCONFIG(TAG, "  Topic prefix: %s", topic_prefix_.c_str());
  ESP_LOGCONFIG(TAG, "  Discovery prefix: %s", discovery_prefix_.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════════
// NvsDeviceManagerBase Hooks
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::on_cover_activated_(EleroDynamicCover *cover) {
  if (cover->config().is_enabled() && mqtt_.is_connected()) {
    publish_cover_discovery_(cover);
    subscribe_cover_commands_(cover);
  }
}

void MqttDeviceManager::on_light_activated_(EleroDynamicLight *light) {
  if (light->config().is_enabled() && mqtt_.is_connected()) {
    publish_light_discovery_(light);
    subscribe_light_commands_(light);
  }
}

void MqttDeviceManager::on_remote_activated_(EleroRemoteControl *remote) {
  if (mqtt_.is_connected()) {
    publish_remote_discovery_(remote);
  }
}

void MqttDeviceManager::on_cover_deactivating_(EleroDynamicCover *cover) {
  remove_discovery_(HA_COVER, cover->get_blind_address());
}

void MqttDeviceManager::on_light_deactivating_(EleroDynamicLight *light) {
  remove_discovery_(HA_LIGHT, light->get_blind_address());
}

void MqttDeviceManager::on_remote_deactivating_(EleroRemoteControl *remote) {
  remove_discovery_(HA_SENSOR, remote->get_address());
}

void MqttDeviceManager::on_cover_updated_(EleroDynamicCover *cover) {
  remove_discovery_(HA_COVER, cover->get_blind_address());
}

void MqttDeviceManager::on_light_updated_(EleroDynamicLight *light) {
  remove_discovery_(HA_LIGHT, light->get_blind_address());
}

void MqttDeviceManager::on_remote_updated_(EleroRemoteControl *remote) {
  remove_discovery_(HA_SENSOR, remote->get_address());
  publish_remote_discovery_(remote);
}

EleroDynamicCover::StateCallback MqttDeviceManager::make_cover_state_callback_() {
  return [this](EleroDynamicCover *c) { publish_cover_state_(c); };
}

EleroDynamicLight::StateCallback MqttDeviceManager::make_light_state_callback_() {
  return [this](EleroDynamicLight *l) { publish_light_state_(l); };
}

EleroRemoteControl::StateCallback MqttDeviceManager::make_remote_state_callback_() {
  return [this](EleroRemoteControl *r) { publish_remote_state_(r); };
}

void MqttDeviceManager::loop_hook_() {
  bool connected = mqtt_.is_connected();
  if (connected && !mqtt_was_connected_) {
    ESP_LOGI(TAG, "MQTT connected, publishing discoveries");
    publish_all_discoveries_();
  }
  mqtt_was_connected_ = connected;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MQTT Publishing
// ═══════════════════════════════════════════════════════════════════════════════

std::string MqttDeviceManager::device_id_() const {
  return App.get_name();
}

void MqttDeviceManager::publish_all_discoveries_() {
  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; ++i) {
      if (cover_slots_[i].is_active() && cover_slots_[i].config().is_enabled()) {
        publish_cover_discovery_(&cover_slots_[i]);
        subscribe_cover_commands_(&cover_slots_[i]);
      }
    }
  }
  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; ++i) {
      if (light_slots_[i].is_active() && light_slots_[i].config().is_enabled()) {
        publish_light_discovery_(&light_slots_[i]);
        subscribe_light_commands_(&light_slots_[i]);
      }
    }
  }
  if (remote_slots_ != nullptr) {
    for (size_t i = 0; i < max_remotes_; ++i) {
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
    root["payload_open"] = action::OPEN;
    root["payload_close"] = action::CLOSE;
    root["payload_stop"] = action::STOP;
    JsonObject dev = root["device"].to<JsonObject>();
    dev["identifiers"][0] = dev_id;
    dev["name"] = device_name_;
    dev["manufacturer"] = MANUFACTURER;
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
  bool has_brightness = light->get_dim_duration_ms() > 0;

  std::string payload = json::build_json([&](JsonObject root) {
    root["name"] = light->get_light_name();
    root["unique_id"] = dev_id + "_" + hex;
    root["schema"] = "json";
    root["command_topic"] = cmd_topic;
    root["state_topic"] = state_topic;
    if (has_brightness) {
      root["brightness"] = true;
      root["brightness_scale"] = static_cast<int>(PERCENT_SCALE);
    }
    JsonObject dev = root["device"].to<JsonObject>();
    dev["identifiers"][0] = dev_id;
    dev["name"] = device_name_;
    dev["manufacturer"] = MANUFACTURER;
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
    dev["manufacturer"] = MANUFACTURER;
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
    root["position"] = static_cast<int>(cover->get_cover_position() * PERCENT_SCALE);
    root["rssi"] = round_rssi(cover->get_last_rssi());
  });

  mqtt_.publish(topic.c_str(), payload.c_str(), false);
}

void MqttDeviceManager::publish_light_state_(EleroDynamicLight *light) {
  if (!mqtt_.is_connected()) return;

  auto hex = addr_hex(light->get_blind_address());
  std::string topic = topic_prefix_ + "/light/" + hex + "/state";

  std::string payload = json::build_json([&](JsonObject root) {
    root["state"] = light->get_is_on() ? ha_state::ON : ha_state::OFF;
    if (light->get_dim_duration_ms() > 0) {
      root["brightness"] = static_cast<int>(light->get_brightness() * PERCENT_SCALE);
    }
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
    if (l == nullptr) return;

    // Try JSON schema first (from HA with brightness support)
    bool handled = json::parse_json(payload, [&](JsonObject root) -> bool {
      if (root["state"].is<const char *>()) {
        const char *state = root["state"];
        if (strcmp(state, ha_state::OFF) == 0) {
          l->perform_action(action::OFF);
          return true;
        }
      }
      if (root["brightness"].is<int>()) {
        // HA sends brightness on scale 0–100 (brightness_scale in discovery)
        float brightness = static_cast<float>(root["brightness"].as<int>()) / PERCENT_SCALE;
        l->set_brightness(brightness, millis());
      } else if (root["state"].is<const char *>()) {
        // ON without brightness = full on
        l->perform_action(action::ON);
      }
      return true;
    });

    // Fall back to simple string action
    if (!handled) {
      l->perform_action(payload);
    }
  });
}

}  // namespace elero
}  // namespace esphome
