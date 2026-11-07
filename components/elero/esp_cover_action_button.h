#pragma once

#ifdef USE_BUTTON

#include "device_registry.h"
#include "esphome/components/button/button.h"
#include "esphome/core/log.h"

namespace esphome::elero {

/// Native API formatter: keep only a stable device ID and action, then resolve
/// the current device through the registry on each press. No RF or state logic.
class EspCoverActionButton final : public button::Button {
 public:
  EspCoverActionButton(DeviceRegistry &registry, uint32_t address, const char *action)
      : registry_(registry), address_(address), action_(action) {}

 protected:
  void press_action() override {
    if (!registry_.command_cover_action(address_, action_)) {
      ESP_LOGW("elero.nvs", "Cover action rejected: %s", action_);
    }
  }

 private:
  DeviceRegistry &registry_;
  uint32_t address_;
  const char *action_;
};

}  // namespace esphome::elero

#endif  // USE_BUTTON
