#include "mqtt_device_manager.h"
#include "../elero/elero.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/json/json_util.h"
#include <cstdio>

namespace esphome {
namespace elero {

static const char *const TAG = "elero.mqtt";

// Discovery topic domains we publish to
static const char *const DISCOVERY_DOMAINS[] = {"cover", "light", "sensor"};
static constexpr size_t DISCOVERY_DOMAIN_COUNT = 3;

// Time to wait for retained messages after subscribing (ms)
static constexpr uint32_t STALE_COLLECT_DELAY_MS = 500;

// ═══════════════════════════════════════════════════════════════════════════════
// Context
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::ensure_context_() {
  if (!context_ready_) {
    ctx_.mqtt = &mqtt_adapter_;
    ctx_.device_id = App.get_name();
    context_ready_ = true;
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Config
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::dump_config() {
  NvsDeviceManagerBase::dump_config();
  ESP_LOGCONFIG(TAG, "  Topic prefix: %s", ctx_.topic_prefix.c_str());
  ESP_LOGCONFIG(TAG, "  Discovery prefix: %s", ctx_.discovery_prefix.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Finder helpers
// ═══════════════════════════════════════════════════════════════════════════════

mqtt_cover::CoverFinder MqttDeviceManager::cover_finder_() {
  return [this](uint32_t addr) { return find_active_cover_(addr); };
}

mqtt_light::LightFinder MqttDeviceManager::light_finder_() {
  return [this](uint32_t addr) { return find_active_light_(addr); };
}

// ═══════════════════════════════════════════════════════════════════════════════
// NvsDeviceManagerBase Hooks
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::on_cover_activated_(EleroDynamicCover *cover) {
  if (cover->config().is_enabled() && ctx_.mqtt->is_connected()) {
    mqtt_cover::activate(ctx_, cover, cover_finder_());
    publish_gateway_state_();
  }
}

void MqttDeviceManager::on_light_activated_(EleroDynamicLight *light) {
  if (light->config().is_enabled() && ctx_.mqtt->is_connected()) {
    mqtt_light::activate(ctx_, light, light_finder_());
    publish_gateway_state_();
  }
}

void MqttDeviceManager::on_remote_activated_(EleroRemoteControl *remote) {
  if (ctx_.mqtt->is_connected()) {
    mqtt_remote::publish_discovery(ctx_, remote);
    publish_gateway_state_();
  }
}

void MqttDeviceManager::on_cover_deactivating_(EleroDynamicCover *cover) {
  mqtt_cover::remove_discovery(ctx_, cover->get_blind_address());
  publish_gateway_state_();
}

void MqttDeviceManager::on_light_deactivating_(EleroDynamicLight *light) {
  mqtt_light::remove_discovery(ctx_, light->get_blind_address());
  publish_gateway_state_();
}

void MqttDeviceManager::on_remote_deactivating_(EleroRemoteControl *remote) {
  mqtt_remote::remove_discovery(ctx_, remote->get_address());
  publish_gateway_state_();
}

void MqttDeviceManager::on_cover_updated_(EleroDynamicCover *cover) {
  mqtt_cover::remove_discovery(ctx_, cover->get_blind_address());
  if (cover->config().is_enabled() && ctx_.mqtt->is_connected()) {
    mqtt_cover::activate(ctx_, cover, cover_finder_());
  }
}

void MqttDeviceManager::on_light_updated_(EleroDynamicLight *light) {
  mqtt_light::remove_discovery(ctx_, light->get_blind_address());
  if (light->config().is_enabled() && ctx_.mqtt->is_connected()) {
    mqtt_light::activate(ctx_, light, light_finder_());
  }
}

void MqttDeviceManager::on_remote_updated_(EleroRemoteControl *remote) {
  mqtt_remote::remove_discovery(ctx_, remote->get_address());
  mqtt_remote::publish_discovery(ctx_, remote);
}

EleroDynamicCover::StateCallback MqttDeviceManager::make_cover_state_callback_() {
  return [this](EleroDynamicCover *c) { mqtt_cover::publish_state(ctx_, c); };
}

EleroDynamicLight::StateCallback MqttDeviceManager::make_light_state_callback_() {
  return [this](EleroDynamicLight *l) { mqtt_light::publish_state(ctx_, l); };
}

EleroRemoteControl::StateCallback MqttDeviceManager::make_remote_state_callback_() {
  return [this](EleroRemoteControl *r) { mqtt_remote::publish_state(ctx_, r); };
}

// ═══════════════════════════════════════════════════════════════════════════════
// Loop hook — state machine for stale cleanup
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::loop_hook_() {
  ensure_context_();
  bool connected = ctx_.mqtt->is_connected();

  if (connected && !mqtt_was_connected_) {
    ESP_LOGI(TAG, "MQTT connected, starting stale discovery cleanup");
    start_stale_collection_();
  }

  if (!connected) {
    mqtt_was_connected_ = false;
    cleanup_state_ = CleanupState::IDLE;
    collected_topics_.clear();
    return;
  }

  mqtt_was_connected_ = connected;

  if (cleanup_state_ == CleanupState::COLLECTING &&
      millis() - collect_start_ms_ > STALE_COLLECT_DELAY_MS) {
    finish_stale_cleanup_();
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Stale discovery cleanup via MQTT retained message query
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::start_stale_collection_() {
  collected_topics_.clear();
  std::string prefix = ctx_.device_id + "_";

  for (size_t i = 0; i < DISCOVERY_DOMAIN_COUNT; ++i) {
    // Subscribe to e.g. homeassistant/cover/+/config
    std::string topic = ctx_.discovery_prefix + "/" + DISCOVERY_DOMAINS[i] + "/+/config";
    ctx_.mqtt->subscribe(topic.c_str(),
        [this](const char *topic, const char *payload) {
      if (cleanup_state_ != CleanupState::COLLECTING) return;
      if (payload == nullptr || payload[0] == '\0') return;
      // Parse the discovery JSON and check device.identifiers[0] matches our device_id.
      // This is the definitive ownership check — no other integration shares our device identifier.
      std::string device_id = ctx_.device_id;
      bool is_ours = json::parse_json(payload, [&](JsonObject root) -> bool {
        JsonObject dev = root["device"];
        if (dev.isNull()) return false;
        JsonArray ids = dev["identifiers"];
        if (ids.isNull() || ids.size() == 0) return false;
        const char *id = ids[0];
        return id != nullptr && device_id == id;
      });
      if (is_ours) {
        collected_topics_.emplace_back(topic);
      }
    });
  }

  cleanup_state_ = CleanupState::COLLECTING;
  collect_start_ms_ = millis();
  mqtt_was_connected_ = true;
}

void MqttDeviceManager::finish_stale_cleanup_() {
  // Unsubscribe from discovery topics before we publish our own
  for (size_t i = 0; i < DISCOVERY_DOMAIN_COUNT; ++i) {
    std::string topic = ctx_.discovery_prefix + "/" + DISCOVERY_DOMAINS[i] + "/+/config";
    ctx_.mqtt->unsubscribe(topic.c_str());
  }

  // Build set of discovery topics we expect to publish (delegated to handlers)
  std::vector<std::string> expected;

  // Gateway sensor is always published
  expected.push_back(ctx_.discovery_prefix + "/sensor/" + ctx_.device_id + "_gateway/config");

  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; ++i) {
      if (cover_slots_[i].is_active() && cover_slots_[i].config().is_enabled())
        mqtt_cover::discovery_topics(ctx_, cover_slots_[i].get_blind_address(), expected);
    }
  }
  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; ++i) {
      if (light_slots_[i].is_active() && light_slots_[i].config().is_enabled())
        mqtt_light::discovery_topics(ctx_, light_slots_[i].get_blind_address(), expected);
    }
  }
  if (remote_slots_ != nullptr) {
    for (size_t i = 0; i < max_remotes_; ++i) {
      if (remote_slots_[i].is_active())
        mqtt_remote::discovery_topics(ctx_, remote_slots_[i].get_address(), expected);
    }
  }

  // Remove any collected topic not in the expected set
  for (auto &topic : collected_topics_) {
    bool is_expected = false;
    for (auto &e : expected) {
      if (topic == e) { is_expected = true; break; }
    }
    if (!is_expected) {
      ESP_LOGI(TAG, "Removing stale discovery: %s", topic.c_str());
      ctx_.mqtt->publish(topic.c_str(), "", true);
    }
  }

  collected_topics_.clear();
  collected_topics_.shrink_to_fit();

  // Publish gateway sensor first (keeps HA device alive even with 0 child entities)
  publish_gateway_discovery_();
  publish_gateway_state_();

  // Then birth + device discoveries
  ctx_.publish_birth();
  publish_all_discoveries_();

  cleanup_state_ = CleanupState::DONE;
  ESP_LOGI(TAG, "Stale cleanup complete, published %zu active discoveries", expected.size());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Gateway sensor — always published, keeps HA device alive
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::publish_gateway_discovery_() {
  auto oid = ctx_.device_id + "_gateway";
  auto st = ctx_.topic_prefix + "/gateway/state";

  std::string payload = json::build_json([&](JsonObject root) {
    root["name"] = "Gateway";
    root["unique_id"] = oid;
    root["object_id"] = oid;
    root["state_topic"] = st;
    root["value_template"] = "{{ value_json.active_devices }}";
    root["unit_of_measurement"] = "devices";
    root["icon"] = "mdi:radio-tower";
    root["entity_category"] = "diagnostic";
    root["json_attributes_topic"] = st;
    root["json_attributes_template"] = "{{ value_json | tojson }}";
    ctx_.add_availability(root);
    JsonObject dev = root["device"].to<JsonObject>();
    ctx_.add_device_block(dev);
    dev["sw_version"] = hub_->get_version();
  });

  ctx_.mqtt->publish(
      (ctx_.discovery_prefix + "/sensor/" + oid + "/config").c_str(),
      payload.c_str(), true);
}

void MqttDeviceManager::publish_gateway_state_() {
  if (!ctx_.mqtt->is_connected()) return;

  // Count active devices
  int covers = 0, lights = 0, remotes = 0;
  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; ++i)
      if (cover_slots_[i].is_active() && cover_slots_[i].config().is_enabled()) ++covers;
  }
  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; ++i)
      if (light_slots_[i].is_active() && light_slots_[i].config().is_enabled()) ++lights;
  }
  if (remote_slots_ != nullptr) {
    for (size_t i = 0; i < max_remotes_; ++i)
      if (remote_slots_[i].is_active()) ++remotes;
  }

  std::string payload = json::build_json([&](JsonObject root) {
    root["active_devices"] = covers + lights + remotes;
    root["covers"] = covers;
    root["lights"] = lights;
    root["remotes"] = remotes;
    root["max_covers"] = static_cast<int>(max_covers_);
    root["max_lights"] = static_cast<int>(max_lights_);
    root["max_remotes"] = static_cast<int>(max_remotes_);
    root["version"] = hub_->get_version();
    char freq[20];
    snprintf(freq, sizeof(freq), "0x%02x%02x%02x",
             hub_->get_freq2(), hub_->get_freq1(), hub_->get_freq0());
    root["frequency"] = freq;
  });

  ctx_.mqtt->publish((ctx_.topic_prefix + "/gateway/state").c_str(),
                     payload.c_str(), false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Bulk discovery publish
// ═══════════════════════════════════════════════════════════════════════════════

void MqttDeviceManager::publish_all_discoveries_() {
  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; ++i) {
      if (cover_slots_[i].is_active() && cover_slots_[i].config().is_enabled()) {
        mqtt_cover::activate(ctx_, &cover_slots_[i], cover_finder_());
      }
    }
  }
  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; ++i) {
      if (light_slots_[i].is_active() && light_slots_[i].config().is_enabled()) {
        mqtt_light::activate(ctx_, &light_slots_[i], light_finder_());
      }
    }
  }
  if (remote_slots_ != nullptr) {
    for (size_t i = 0; i < max_remotes_; ++i) {
      if (remote_slots_[i].is_active()) {
        mqtt_remote::publish_discovery(ctx_, &remote_slots_[i]);
      }
    }
  }
}

}  // namespace elero
}  // namespace esphome
