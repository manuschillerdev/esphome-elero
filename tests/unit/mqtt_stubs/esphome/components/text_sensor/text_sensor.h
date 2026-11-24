#pragma once

#include <string>

namespace esphome::text_sensor {
class TextSensor {
 public:
  std::string state;
  void publish_state(const std::string &value) { state = value; }
};
}  // namespace esphome::text_sensor
