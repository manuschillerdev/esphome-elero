#pragma once

#include "mqtt_publisher.h"

#ifdef USE_MQTT
#include "esphome/components/mqtt/mqtt_client.h"
#endif

namespace esphome {
namespace elero {

/// Adapts ESPHome's global MQTT client to our MqttPublisher interface.
class EspHomeMqttAdapter : public MqttPublisher {
 public:
  bool publish(const char *topic, const char *payload, bool retain) override {
#ifdef USE_MQTT
    if (mqtt::global_mqtt_client == nullptr) return false;
    return mqtt::global_mqtt_client->publish(topic, payload, strlen(payload),
                                              0 /* qos */, retain);
#else
    return false;
#endif
  }

  bool subscribe(const char *topic, std::function<void(const char *topic, const char *payload)> cb) override {
#ifdef USE_MQTT
    if (mqtt::global_mqtt_client == nullptr) return false;
    mqtt::global_mqtt_client->subscribe(
        topic,
        [cb](const std::string &t, const std::string &p) {
          cb(t.c_str(), p.c_str());
        },
        0 /* qos */);
    return true;
#else
    return false;
#endif
  }

  bool is_connected() const override {
#ifdef USE_MQTT
    if (mqtt::global_mqtt_client == nullptr) return false;
    return mqtt::global_mqtt_client->is_connected();
#else
    return false;
#endif
  }
};

}  // namespace elero
}  // namespace esphome
