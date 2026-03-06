#pragma once

#include <cstdint>
#include <functional>

namespace esphome::elero {

/// Abstract MQTT publish interface (testable without ESPHome)
class MqttPublisher {
 public:
  virtual ~MqttPublisher() = default;

  virtual bool publish(const char *topic, const char *payload, bool retain) = 0;
  virtual bool subscribe(const char *topic, std::function<void(const char *topic, const char *payload)> cb) = 0;
  virtual bool is_connected() const = 0;
};

}  // namespace esphome::elero
