#include "mqtt_cover_handler.h"
#include "esphome/core/log.h"
#include <cstdio>

namespace esphome {
namespace elero {
namespace mqtt_cover {

static const char *const TAG = "elero.mqtt.cover";

// ─── Topic / ID helpers (private to this module) ─────────────────────────────

static std::string object_id(const MqttContext &ctx, uint32_t addr) {
  return ctx.device_id + "_" + addr_hex(addr);
}

static std::string rssi_object_id(const MqttContext &ctx, uint32_t addr) {
  return ctx.device_id + "_cover_" + addr_hex(addr) + "_rssi";
}

static std::string state_sensor_object_id(const MqttContext &ctx, uint32_t addr) {
  return ctx.device_id + "_cover_" + addr_hex(addr) + "_state";
}

static std::string base_topic(const MqttContext &ctx, uint32_t addr) {
  return ctx.topic_prefix + "/cover/" + addr_hex(addr);
}

// ─── Public API ──────────────────────────────────────────────────────────────

void publish_discovery(const MqttContext &ctx, EleroDynamicCover *cover) {
  auto addr = cover->get_blind_address();
  auto hex = addr_hex(addr);
  auto oid = object_id(ctx, addr);
  auto base = base_topic(ctx, addr);

  // Cover entity
  bool tilt = cover->get_supports_tilt();
  std::string payload = json::build_json([&](JsonObject root) {
    root["name"] = cover->get_blind_name();
    root["unique_id"] = ctx.device_id + "_cover_" + hex;
    root["command_topic"] = base + "/set";
    root["state_topic"] = base + "/state";
    root["position_topic"] = base + "/position";
    root["payload_open"] = action::OPEN;
    root["payload_close"] = action::CLOSE;
    root["payload_stop"] = action::STOP;
    root["position_open"] = 100;
    root["position_closed"] = 0;
    if (tilt) {
      root["tilt_command_topic"] = base + "/tilt";
    }
    ctx.add_availability(root);
    JsonObject dev = root["device"].to<JsonObject>();
    ctx.add_device_block(dev);
  });
  ctx.mqtt->publish(
    (ctx.discovery_prefix + "/cover/" + oid + "/config").c_str(),
    payload.c_str(), true);

  // RSSI sensor entity (grouped under same HA device)
  std::string rssi_payload = json::build_json([&](JsonObject root) {
    root["name"] = std::string(cover->get_blind_name()) + " RSSI";
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

  // Blind state sensor entity (e.g. top, bottom, moving_up, ...)
  std::string state_payload = json::build_json([&](JsonObject root) {
    root["name"] = std::string(cover->get_blind_name()) + " State";
    root["unique_id"] = state_sensor_object_id(ctx, addr);
    root["state_topic"] = base + "/blind_state";
    root["entity_category"] = "diagnostic";
    root["icon"] = "mdi:state-machine";
    ctx.add_availability(root);
    JsonObject dev = root["device"].to<JsonObject>();
    ctx.add_device_block(dev);
  });
  ctx.mqtt->publish(
    (ctx.discovery_prefix + "/sensor/" + state_sensor_object_id(ctx, addr) + "/config").c_str(),
    state_payload.c_str(), true);

  ESP_LOGD(TAG, "Published discovery for 0x%06x", addr);
}

void remove_discovery(const MqttContext &ctx, uint32_t addr) {
  ctx.remove_discovery("cover", object_id(ctx, addr));
  ctx.remove_discovery("sensor", rssi_object_id(ctx, addr));
  ctx.remove_discovery("sensor", state_sensor_object_id(ctx, addr));
}

void discovery_topics(const MqttContext &ctx, uint32_t addr, std::vector<std::string> &out) {
  out.push_back(ctx.discovery_prefix + "/cover/" + object_id(ctx, addr) + "/config");
  out.push_back(ctx.discovery_prefix + "/sensor/" + rssi_object_id(ctx, addr) + "/config");
  out.push_back(ctx.discovery_prefix + "/sensor/" + state_sensor_object_id(ctx, addr) + "/config");
}

void publish_state(const MqttContext &ctx, EleroDynamicCover *cover) {
  if (!ctx.mqtt->is_connected()) return;

  auto base = base_topic(ctx, cover->get_blind_address());

  ctx.mqtt->publish((base + "/state").c_str(), cover->get_operation_str(), false);

  char pos_buf[8];
  snprintf(pos_buf, sizeof(pos_buf), "%d",
           static_cast<int>(cover->get_cover_position() * PERCENT_SCALE));
  ctx.mqtt->publish((base + "/position").c_str(), pos_buf, false);

  char rssi_buf[12];
  snprintf(rssi_buf, sizeof(rssi_buf), "%.0f", round_rssi(cover->get_last_rssi()));
  ctx.mqtt->publish((base + "/rssi").c_str(), rssi_buf, false);

  ctx.mqtt->publish((base + "/blind_state").c_str(),
                    elero_state_to_string(cover->get_last_state_raw()), false);
}

void subscribe_commands(const MqttContext &ctx, uint32_t addr, CoverFinder finder) {
  std::string base = base_topic(ctx, addr);
  ctx.mqtt->subscribe((base + "/set").c_str(), [finder, addr](const char *, const char *payload) {
    auto *c = finder(addr);
    if (c != nullptr) {
      c->perform_action(payload);
    }
  });
  ctx.mqtt->subscribe((base + "/tilt").c_str(), [finder, addr](const char *, const char *) {
    auto *c = finder(addr);
    if (c != nullptr) {
      c->perform_action(action::TILT);
    }
  });
}

void activate(const MqttContext &ctx, EleroDynamicCover *cover, CoverFinder finder) {
  publish_discovery(ctx, cover);
  subscribe_commands(ctx, cover->get_blind_address(), std::move(finder));
  publish_state(ctx, cover);
}

}  // namespace mqtt_cover
}  // namespace elero
}  // namespace esphome
