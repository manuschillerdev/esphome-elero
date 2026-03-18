#pragma once

#include "mqtt_publisher.h"
#include "../elero/elero_strings.h"
#include "esphome/components/json/json_util.h"
#include <string>
#include <cstdio>
#include <functional>
#include <utility>

namespace esphome {
namespace elero {

/// Format a uint32_t as "%06x" for MQTT topic construction (no "0x" prefix).
[[nodiscard]] inline std::string addr_hex(uint32_t addr) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%06x", addr);
  return buf;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MQTT TOPIC SUFFIXES — single source of truth for all topic paths
// ═══════════════════════════════════════════════════════════════════════════════

namespace mqtt_topic {
inline constexpr const char *STATE = "/state";
inline constexpr const char *POSITION = "/position";
inline constexpr const char *RSSI = "/rssi";
inline constexpr const char *BLIND_STATE = "/blind_state";
inline constexpr const char *LIGHT_STATE = "/light_state";
inline constexpr const char *PROBLEM = "/problem";
inline constexpr const char *ATTRIBUTES = "/attributes";
inline constexpr const char *TILT_STATE = "/tilt_state";
inline constexpr const char *STATUS = "/status";
inline constexpr const char *SET = "/set";
inline constexpr const char *TILT = "/tilt";
inline constexpr const char *CONFIG = "/config";
}  // namespace mqtt_topic

// ═══════════════════════════════════════════════════════════════════════════════
// HA COMPONENT TYPES — for discovery topic construction
// ═══════════════════════════════════════════════════════════════════════════════

// ESPHome's MQTTComponent::component_type() returns raw const char* — no shared
// enum exists. These constants avoid magic strings in our discovery code.
namespace ha_discovery {
inline constexpr const char *COVER = "cover";
inline constexpr const char *LIGHT = "light";
inline constexpr const char *SENSOR = "sensor";
inline constexpr const char *BINARY_SENSOR = "binary_sensor";
}  // namespace ha_discovery

// ═══════════════════════════════════════════════════════════════════════════════
// MQTT CONTEXT — topic builder + publish/subscribe with zero call-site concat
// ═══════════════════════════════════════════════════════════════════════════════

struct MqttContext {
  MqttPublisher *mqtt{nullptr};
  std::string topic_prefix{"elero"};
  std::string discovery_prefix{"homeassistant"};
  std::string device_name{"Elero Gateway"};
  std::string device_id;

  // ── Topic builders ──────────────────────────────────────────

  /// Build device state topic: {prefix}/{device_type}/{addr_hex}{suffix}
  std::string topic(DeviceType type, uint32_t addr, const char *suffix) const {
    const char *type_str = device_type_str(type);
    auto hex = addr_hex(addr);
    std::string result;
    result.reserve(topic_prefix.size() + 1 + strlen(type_str) + 1 + hex.size() + strlen(suffix));
    result += topic_prefix;
    result += '/';
    result += type_str;
    result += '/';
    result += hex;
    result += suffix;
    return result;
  }

  /// Build object ID: {device_id}_{device_type}_{addr_hex}[_{suffix}]
  std::string object_id(DeviceType type, uint32_t addr, const char *suffix = nullptr) const {
    const char *type_str = device_type_str(type);
    auto hex = addr_hex(addr);
    std::string result;
    result.reserve(device_id.size() + 1 + strlen(type_str) + 1 + hex.size() +
                   (suffix != nullptr ? 1 + strlen(suffix) : 0));
    result += device_id;
    result += '_';
    result += type_str;
    result += '_';
    result += hex;
    if (suffix != nullptr) { result += '_'; result += suffix; }
    return result;
  }

  /// Build discovery config topic: {discovery_prefix}/{component}/{object_id}/config
  std::string discovery_topic(const char *component, const std::string &oid) const {
    std::string result;
    result.reserve(discovery_prefix.size() + 1 + strlen(component) + 1 + oid.size() +
                   strlen(mqtt_topic::CONFIG));
    result += discovery_prefix;
    result += '/';
    result += component;
    result += '/';
    result += oid;
    result += mqtt_topic::CONFIG;
    return result;
  }

  // ── Publish helpers (no concatenation at call sites) ────────

  /// Publish to a device topic: {prefix}/{device_type}/{addr}{suffix}
  void publish(DeviceType type, uint32_t addr, const char *suffix,
               const char *payload, bool retained) const {
    mqtt->publish(topic(type, addr, suffix).c_str(), payload, retained);
  }

  /// Publish a std::string payload to a device topic.
  void publish(DeviceType type, uint32_t addr, const char *suffix,
               const std::string &payload, bool retained) const {
    publish(type, addr, suffix, payload.c_str(), retained);
  }

  /// Publish a discovery config (retained).
  void publish_discovery(const char *component, const std::string &oid,
                          const std::string &payload) const {
    mqtt->publish(discovery_topic(component, oid).c_str(), payload.c_str(), true);
  }

  /// Remove a discovery config (empty retained payload).
  void remove_discovery(const char *component, const std::string &oid) const {
    mqtt->publish(discovery_topic(component, oid).c_str(), "", true);
  }

  // ── Subscribe helpers ───────────────────────────────────────

  /// Subscribe to a device topic.
  void subscribe(DeviceType type, uint32_t addr, const char *suffix,
                  std::function<void(const char *, const char *)> cb) const {
    mqtt->subscribe(topic(type, addr, suffix).c_str(), std::move(cb));
  }

  /// Unsubscribe from a device topic.
  void unsubscribe(DeviceType type, uint32_t addr, const char *suffix) const {
    mqtt->unsubscribe(topic(type, addr, suffix).c_str());
  }

  // ── HA device/availability blocks ───────────────────────────

  /// Add the standard HA device block to a discovery payload.
  void add_device_block(JsonObject dev) const {
    dev["identifiers"][0] = device_id;
    dev["name"] = device_name;
    dev["manufacturer"] = MANUFACTURER;
  }

  /// Add availability block so entities go unavailable when hub is offline.
  void add_availability(JsonObject root) const {
    root["availability_topic"] = topic_prefix + mqtt_topic::STATUS;
    root["payload_available"] = "online";
    root["payload_not_available"] = "offline";
  }

  /// Publish birth message on connect.
  void publish_birth() const {
    mqtt->publish((topic_prefix + mqtt_topic::STATUS).c_str(), "online", true);
  }
};

}  // namespace elero
}  // namespace esphome
