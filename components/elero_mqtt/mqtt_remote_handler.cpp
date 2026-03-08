#include "mqtt_remote_handler.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {
namespace mqtt_remote {

static const char *const TAG = "elero.mqtt.remote";

// ─── Topic / ID helpers (private to this module) ─────────────────────────────

static std::string object_id(const MqttContext &ctx, uint32_t addr) {
  return ctx.device_id + "_remote_" + addr_hex(addr);
}

static std::string state_topic(const MqttContext &ctx, uint32_t addr) {
  return ctx.topic_prefix + "/remote/" + addr_hex(addr) + "/state";
}

// ─── Public API ──────────────────────────────────────────────────────────────

void publish_discovery(const MqttContext &ctx, EleroRemoteControl *remote) {
  auto addr = remote->get_address();
  auto oid = object_id(ctx, addr);
  auto st = state_topic(ctx, addr);

  std::string payload = json::build_json([&](JsonObject root) {
    root["name"] = remote->get_title();
    root["unique_id"] = oid;
    root["state_topic"] = st;
    root["value_template"] = "{{ value_json.rssi }}";
    root["unit_of_measurement"] = "dBm";
    root["device_class"] = "signal_strength";
    root["json_attributes_topic"] = st;
    root["json_attributes_template"] = "{{ value_json | tojson }}";
    ctx.add_availability(root);
    JsonObject dev = root["device"].to<JsonObject>();
    ctx.add_device_block(dev);
  });

  ctx.mqtt->publish(
    (ctx.discovery_prefix + "/sensor/" + oid + "/config").c_str(),
    payload.c_str(), true);
  ESP_LOGD(TAG, "Published discovery for 0x%06x", addr);
}

void remove_discovery(const MqttContext &ctx, uint32_t addr) {
  ctx.remove_discovery("sensor", object_id(ctx, addr));
}

void discovery_topics(const MqttContext &ctx, uint32_t addr, std::vector<std::string> &out) {
  out.push_back(ctx.discovery_prefix + "/sensor/" + object_id(ctx, addr) + "/config");
}

void publish_state(const MqttContext &ctx, EleroRemoteControl *remote) {
  if (!ctx.mqtt->is_connected()) return;

  auto addr = remote->get_address();
  auto topic = state_topic(ctx, addr);

  std::string payload = json::build_json([&](JsonObject root) {
    root["rssi"] = remote->get_rssi();
    root["address"] = hex_str(addr);
    root["title"] = remote->get_title();
    root["last_seen"] = remote->get_last_seen_ms();
    root["last_channel"] = remote->get_last_channel();
    root["last_command"] = hex_str8(remote->get_last_command());
    root["last_target"] = hex_str(remote->get_last_target());
  });

  ctx.mqtt->publish(topic.c_str(), payload.c_str(), false);
}

}  // namespace mqtt_remote
}  // namespace elero
}  // namespace esphome
