#pragma once

#include <cstdint>
#include "esphome/core/component.h"

namespace esphome::light {

struct LightColorValues {
  bool on{false};
  float brightness{1.0f};
  bool is_on() const { return on; }
  void set_state(bool value) { on = value; }
  void set_brightness(float value) { brightness = value; }
};
class LightOutput;
struct LightState : Component {
  explicit LightState(LightOutput *output = nullptr) : output_(output) {}
  void disable_loop() {}
  void set_save_enabled(bool) {}
  void set_default_transition_length(uint32_t) {}
  LightColorValues current_values, remote_values;
  unsigned publications{0};
  void current_values_as_brightness(float *out) const { *out = current_values.brightness; }
  void publish_state() { ++publications; }

 protected:
  LightOutput *output_;
};

}  // namespace esphome::light
