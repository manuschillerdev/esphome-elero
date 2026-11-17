#pragma once

namespace esphome::light {

struct LightColorValues {
  bool on{false};
  float brightness{1.0f};
  bool is_on() const { return on; }
  void set_state(bool value) { on = value; }
  void set_brightness(float value) { brightness = value; }
};
struct LightState {
  LightColorValues current_values, remote_values;
  unsigned publications{0};
  void current_values_as_brightness(float *out) const { *out = current_values.brightness; }
  void publish_state() { ++publications; }
};

}  // namespace esphome::light
