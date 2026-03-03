#pragma once

/// @file esphome_mqtt_adapter.h
/// @brief ESPHome MQTT client adapter implementing MqttPublisher interface.
///
/// This is the production implementation that wraps ESPHome's global_mqtt_client.

#include "mqtt_publisher.h"

#ifdef USE_MQTT
#include "esphome/components/mqtt/mqtt_client.h"
#endif

namespace esphome::elero {

#ifdef USE_MQTT

/// Production MQTT publisher that wraps ESPHome's global_mqtt_client.
class EspHomeMqttAdapter : public MqttPublisher {
 public:
  EspHomeMqttAdapter() = default;

  [[nodiscard]] bool publish(const std::string& topic, const std::string& payload,
                             bool retain = false) override {
    if (mqtt::global_mqtt_client == nullptr) {
      return false;
    }
    return mqtt::global_mqtt_client->publish(topic, payload, 0, retain);
  }

  [[nodiscard]] bool subscribe(const std::string& topic,
                               std::function<void(const std::string&, const std::string&)> callback) override {
    if (mqtt::global_mqtt_client == nullptr) {
      return false;
    }
    mqtt::global_mqtt_client->subscribe(
        topic,
        [callback](const std::string& t, const std::string& p) {
          callback(t, p);
        },
        0);
    return true;
  }

  [[nodiscard]] bool unsubscribe(const std::string& topic) override {
    if (mqtt::global_mqtt_client == nullptr) {
      return false;
    }
    mqtt::global_mqtt_client->unsubscribe(topic);
    return true;
  }

  [[nodiscard]] bool is_connected() const override {
    if (mqtt::global_mqtt_client == nullptr) {
      return false;
    }
    return mqtt::global_mqtt_client->is_connected();
  }
};

#else  // !USE_MQTT

/// Stub implementation when MQTT is not enabled.
class EspHomeMqttAdapter : public MqttPublisher {
 public:
  [[nodiscard]] bool publish(const std::string& /*topic*/, const std::string& /*payload*/,
                             bool /*retain*/ = false) override {
    return false;
  }

  [[nodiscard]] bool subscribe(const std::string& /*topic*/,
                               std::function<void(const std::string&, const std::string&)> /*callback*/) override {
    return false;
  }

  [[nodiscard]] bool unsubscribe(const std::string& /*topic*/) override {
    return false;
  }

  [[nodiscard]] bool is_connected() const override {
    return false;
  }
};

#endif  // USE_MQTT

}  // namespace esphome::elero
