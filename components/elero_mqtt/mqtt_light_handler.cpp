#include "mqtt_light_handler.h"
#include "esphome/core/log.h"
#include <cstdio>
#include <cstring>

namespace esphome {
namespace elero {
namespace mqtt_light {

static const char *const TAG = "elero.mqtt.light";

// ─── Topic / ID helpers (private to this module) ─────────────────────────────

static std::string object_id(const MqttContext &ctx, uint32_t addr) {
  return ctx.device_id + "_" + addr_hex(addr);
}

static std::string rssi_object_id(const MqttContext &ctx, uint32_t addr) {
  return ctx.device_id + "_light_" + addr_hex(addr) + "_rssi";
}

static std::string base_topic(const MqttContext &ctx, uint32_t addr) {
  return ctx.topic_prefix + "/light/" + addr_hex(addr);
}

// ─── Public API ──────────────────────────────────────────────────────────────

void publish_discovery(const MqttContext &ctx, EleroDynamicLight *light) {
  auto addr = light->get_blind_address();
  auto hex = addr_hex(addr);
  auto oid = object_id(ctx, addr);
  auto base = base_topic(ctx, addr);
  bool has_brightness = light->get_dim_duration_ms() > 0;

  // Light entity (JSON schema for HA brightness support)
  std::string payload = json::build_json([&](JsonObject root) {
    root["name"] = light->get_light_name();
    root["unique_id"] = ctx.device_id + "_light_" + hex;
    root["schema"] = "json";
    root["command_topic"] = base + "/set";
    root["state_topic"] = base + "/state";
    if (has_brightness) {
      root["brightness"] = true;
      root["brightness_scale"] = static_cast<int>(PERCENT_SCALE);
    }
    ctx.add_availability(root);
    JsonObject dev = root["device"].to<JsonObject>();
    ctx.add_device_block(dev);
  });
  ctx.mqtt->publish(
    (ctx.discovery_prefix + "/light/" + oid + "/config").c_str(),
    payload.c_str(), true);

  // RSSI sensor entity (grouped under same HA device)
  std::string rssi_payload = json::build_json([&](JsonObject root) {
    root["name"] = std::string(light->get_light_name()) + " RSSI";
    root["unique_id"] = rssi_object_id(ctx, addr);
    root["state_topic"] = base + "/rssi";
    root["unit_of_measurement"] = "dBm";
    root["device_class"] = "signal_strength";
    root["entity_category"] = "diagnostic";
    ctx.add_availability(root);
    JsonObject dev = root["device"].to<JsonObject>();
    ctx.add_device_block(dev);
  });
  ctx.mqtt->publish(
    (ctx.discovery_prefix + "/sensor/" + rssi_object_id(ctx, addr) + "/config").c_str(),
    rssi_payload.c_str(), true);

  ESP_LOGD(TAG, "Published discovery for 0x%06x", addr);
}

void remove_discovery(const MqttContext &ctx, uint32_t addr) {
  ctx.remove_discovery("light", object_id(ctx, addr));
  ctx.remove_discovery("sensor", rssi_object_id(ctx, addr));
}

void discovery_topics(const MqttContext &ctx, uint32_t addr, std::vector<std::string> &out) {
  out.push_back(ctx.discovery_prefix + "/light/" + object_id(ctx, addr) + "/config");
  out.push_back(ctx.discovery_prefix + "/sensor/" + rssi_object_id(ctx, addr) + "/config");
}

void publish_state(const MqttContext &ctx, EleroDynamicLight *light) {
  if (!ctx.mqtt->is_connected()) return;

  auto base = base_topic(ctx, light->get_blind_address());

  // JSON schema state (HA expects {"state":"ON"} or {"state":"ON","brightness":80})
  std::string payload = json::build_json([&](JsonObject root) {
    root["state"] = light->get_is_on() ? ha_state::ON : ha_state::OFF;
    if (light->get_dim_duration_ms() > 0) {
      root["brightness"] = static_cast<int>(light->get_brightness() * PERCENT_SCALE);
    }
  });
  ctx.mqtt->publish((base + "/state").c_str(), payload.c_str(), false);

  // RSSI on separate topic
  char rssi_buf[12];
  snprintf(rssi_buf, sizeof(rssi_buf), "%.0f", round_rssi(light->get_last_rssi()));
  ctx.mqtt->publish((base + "/rssi").c_str(), rssi_buf, false);
}

void subscribe_commands(const MqttContext &ctx, uint32_t addr, LightFinder finder) {
  std::string topic = base_topic(ctx, addr) + "/set";
  ctx.mqtt->subscribe(topic.c_str(), [finder, addr](const char *, const char *payload) {
    auto *l = finder(addr);
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
        // HA sends brightness on scale 0-100 (brightness_scale in discovery)
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

void activate(const MqttContext &ctx, EleroDynamicLight *light, LightFinder finder) {
  publish_discovery(ctx, light);
  subscribe_commands(ctx, light->get_blind_address(), std::move(finder));
  publish_state(ctx, light);
}

}  // namespace mqtt_light
}  // namespace elero
}  // namespace esphome
