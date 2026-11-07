// Minimal ESPHome cover interface for native shell command regression tests.
#pragma once

#include <optional>

namespace esphome::cover {

enum CoverOperation {
  COVER_OPERATION_IDLE,
  COVER_OPERATION_OPENING,
  COVER_OPERATION_CLOSING,
};

struct CoverTraits {
  void set_supports_position(bool) {}
  void set_supports_tilt(bool) {}
  void set_supports_stop(bool) {}
  void set_supports_toggle(bool) {}
  void set_is_assumed_state(bool) {}
};

struct CoverCall {
  std::optional<float> position;
  std::optional<float> tilt;
  std::optional<bool> toggle;
  bool stop{false};

  bool get_stop() const { return stop; }
  const auto &get_position() const { return position; }
  const auto &get_tilt() const { return tilt; }
  const auto &get_toggle() const { return toggle; }
};

class Cover {
 public:
  virtual ~Cover() = default;
  virtual CoverTraits get_traits() { return {}; }
  virtual void control(const CoverCall &) {}
  void publish_state() {}

  float position{0};
  float tilt{0};
  CoverOperation current_operation{COVER_OPERATION_IDLE};
};

}  // namespace esphome::cover
