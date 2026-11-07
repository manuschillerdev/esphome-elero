#pragma once

namespace esphome::button {
// Only the ESPHome press callback boundary is stubbed. Tests use the production
// EspCoverActionButton, DeviceRegistry, command queue, and packet builders.
class Button {
 public:
  virtual ~Button() = default;
  void press() { press_action(); }

 protected:
  virtual void press_action() = 0;
};
}  // namespace esphome::button
