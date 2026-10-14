/// @file refresh_button.h
/// @brief Per-device "Refresh" button — sends a CHECK packet to query blind state.
///
/// Thin shell: holds Device* + DeviceRegistry*, calls request_check() on press.
/// Wired up by an output adapter (e.g. NvsAdapter) after it constructs the
/// device's cover/light shell. Currently not surfaced in NVS mode — RFC-002
/// removed the YAML codegen path that used to instantiate these alongside the
/// diagnostic sensors. See docs/STATE_REPORTING.md ("Known gaps").

#pragma once

#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#include "device.h"
#include "device_registry.h"

namespace esphome {
namespace elero {

class RefreshButton : public button::Button {
 public:
  void set_registry(DeviceRegistry *r) { registry_ = r; }
  void set_device(Device *d) { device_ = d; }

 protected:
  void press_action() override {
    if (registry_ && device_) {
      registry_->request_check(*device_);
    }
  }

 private:
  DeviceRegistry *registry_{nullptr};
  Device *device_{nullptr};
};

}  // namespace elero
}  // namespace esphome

#endif
