/// @file nvs_adapter.h
/// @brief Creates ESPHome cover/light entities from NVS-restored devices at boot.
///
/// In native_nvs mode (API + NVS), devices are managed via the web UI and
/// persisted in NVS. This adapter creates EspCoverShell / EspLightShell
/// instances from the restored registry slots during setup(), before the
/// API server enumerates entities.

#pragma once

#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "../elero/device_registry.h"
#include "../elero/device_type.h"
#include "../elero/esp_cover_shell.h"
#include "../elero/esp_light_shell.h"
#include "esphome/components/light/light_state.h"

namespace esphome {
namespace elero {

class NvsAdapter : public Component {
 public:
  float get_setup_priority() const override {
    // After hub (DATA=600) which restores NVS devices,
    // before API server (AFTER_WIFI=200) which enumerates entities.
    return setup_priority::DATA - 2.0f;
  }

  void set_registry(DeviceRegistry *r) { registry_ = r; }

  void setup() override {
    if (!registry_) return;

    size_t covers = 0, lights = 0;
    for (size_t i = 0; i < DeviceRegistry::max_devices(); ++i) {
      auto *dev = registry_->slot(i);
      if (!dev || !dev->active) continue;
      if (!dev->config.is_enabled()) continue;

      if (dev->is_cover()) {
        create_cover_(dev, i);
        ++covers;
      } else if (dev->is_light()) {
        create_light_(dev, i);
        ++lights;
      }
    }

    ESP_LOGI("nvs_adapter", "Created %zu cover(s) and %zu light(s) from NVS", covers, lights);
  }

 private:
  void create_cover_(Device *dev, size_t slot_index) {
    auto *shell = new EspCoverShell();  // NOLINT — owned by App
    shell->set_name(dev->config.name);
    shell->set_registry(registry_);
    shell->set_slot_index(static_cast<int>(slot_index));
    shell->set_open_duration(dev->config.open_duration_ms);
    shell->set_close_duration(dev->config.close_duration_ms);
    shell->set_supports_tilt(dev->config.supports_tilt);

    App.register_cover(shell);
    App.register_component(shell);
  }

  void create_light_(Device *dev, size_t slot_index) {
    auto *output = new EspLightShell();  // NOLINT — owned by App
    output->set_registry(registry_);
    output->set_slot_index(static_cast<int>(slot_index));
    output->set_dim_duration(dev->config.dim_duration_ms);

    auto *state = new light::LightState(output);  // NOLINT — owned by App
    state->set_name(dev->config.name);
    state->set_restore_mode(light::LIGHT_RESTORE_DEFAULT_OFF);

    output->set_light_state(state);

    App.register_light(state);
    App.register_component(state);
    App.register_component(output);
  }

  DeviceRegistry *registry_{nullptr};
};

}  // namespace elero
}  // namespace esphome
