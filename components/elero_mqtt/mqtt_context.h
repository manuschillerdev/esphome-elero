#pragma once

#include "mqtt_publisher.h"
#include "../elero/elero_strings.h"
#include "esphome/components/json/json_util.h"
#include <string>
#include <cstdio>

namespace esphome {
namespace elero {

/// Format a uint32_t as "%06x" for MQTT topic construction (no "0x" prefix).
inline std::string addr_hex(uint32_t addr) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%06x", addr);
  return buf;
}

/// Shared context for MQTT handlers.
/// Owned by MqttDeviceManager, passed by const reference to handler functions.
struct MqttContext {
  MqttPublisher *mqtt{nullptr};
  std::string topic_prefix{"elero"};
  std::string discovery_prefix{"homeassistant"};
  std::string device_name{"Elero Gateway"};
  std::string device_id;

  /// Add the standard HA device block to a discovery payload.
  void add_device_block(JsonObject dev) const {
    dev["identifiers"][0] = device_id;
    dev["name"] = device_name;
    dev["manufacturer"] = MANUFACTURER;
  }

  /// Add availability block so entities go unavailable when hub is offline.
  void add_availability(JsonObject root) const {
    root["availability_topic"] = topic_prefix + "/status";
    root["payload_available"] = "online";
    root["payload_not_available"] = "offline";
  }

  /// Publish birth message on connect.
  void publish_birth() const {
    mqtt->publish((topic_prefix + "/status").c_str(), "online", true);
  }

  /// Publish empty retained payload to remove a discovery config topic.
  void remove_discovery(const char *component, const std::string &object_id) const {
    std::string topic = discovery_prefix + "/" + component + "/" + object_id + "/config";
    mqtt->publish(topic.c_str(), "", true);
  }
};

}  // namespace elero
}  // namespace esphome
