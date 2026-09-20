/// @file nvs_adapter.h
/// @brief Creates ESPHome cover/light entities from NVS-restored devices at boot.
///
/// In native_nvs mode (API + NVS), devices are managed via the web UI and
/// persisted in NVS. This adapter creates cover shells and binds pre-registered
/// light-state slots during setup(), before the API server enumerates entities.

#pragma once

#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "../elero/device_registry.h"
#include "../elero/device_type.h"
#include "../elero/esp_cover_shell.h"
#include "../elero/esp_light_shell.h"
#include "../elero/output_adapter.h"
#include "esphome/components/light/light_state.h"
#include <array>
#include <string>

namespace esphome {
namespace elero {

// Registered by Python codegen so LightState's loop enable/disable and scheduler
// participate in the normal ESPHome lifecycle. Only NVS-bound slots are entities.
class NvsLightState final : public light::LightState {
 public:
  NvsLightState() : light::LightState(&output_) {}
  float get_setup_priority() const override { return setup_priority::DATA - 3.0f; }
  EspLightShell *output() { return &output_; }
  void setup() override {
    if (output_.device_ == nullptr) {
      disable_loop();
      return;
    }
    light::LightState::setup();
    output_.setup();
  }
  void dump_config() override {
    if (output_.device_ != nullptr) light::LightState::dump_config();
  }

 private:
  EspLightShell output_;
};

class NvsAdapter : public Component, public OutputAdapter {
 public:
  float get_setup_priority() const override {
    // After hub (DATA=600) which restores NVS devices,
    // before API server (AFTER_WIFI=200) which enumerates entities.
    return setup_priority::DATA - 2.0f;
  }

  void set_registry(DeviceRegistry *r) { registry_ = r; }
  // Codegen supplies every registry slot before App.setup().
  void set_light_slot(size_t index, NvsLightState *state) { light_states_[index] = state; }

  void setup(DeviceRegistry &registry) override { registry_ = &registry; }
  void loop() override {}
  void on_device_added(const Device &) override {}
  void on_device_removed(const Device &) override {}
  void on_state_changed(const Device &dev, uint16_t changes) override {
    size_t idx = registry_->slot_index(dev);
    if (dev.is_cover() && cover_shells_[idx] != nullptr) {
      cover_shells_[idx]->sync_and_publish(changes);
    } else if (dev.is_light() && light_shells_[idx] != nullptr) {
      light_shells_[idx]->sync_and_publish(changes);
    }
  }

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
    auto *shell = new EspCoverShell();  // Firmware-lifetime entity
    shell->set_registry(registry_);
    shell->set_device(dev);

    cover_shells_[slot_index] = shell;
    // EntityBase retains a StringRef: keep the boot name alive and immutable
    // even if the registry config is edited before the required reboot.
    names_[slot_index] = dev->config.name;
    App.register_cover(shell, names_[slot_index].c_str(), 0, 0);
    // Covers have no periodic work; initialize directly after binding.
    shell->setup();
  }

  void create_light_(Device *dev, size_t slot_index) {
    auto *state = light_states_[slot_index];
    auto *output = state->output();
    output->set_registry(registry_);
    output->set_device(dev);

    state->set_restore_mode(light::LIGHT_RESTORE_DEFAULT_OFF);

    output->set_light_state(state);

    light_shells_[slot_index] = output;
    names_[slot_index] = dev->config.name;
    App.register_light(state, names_[slot_index].c_str(), 0, 0);
  }

  DeviceRegistry *registry_{nullptr};
  std::array<EspCoverShell *, DeviceRegistry::MAX_DEVICES> cover_shells_{};
  std::array<EspLightShell *, DeviceRegistry::MAX_DEVICES> light_shells_{};
  std::array<NvsLightState *, DeviceRegistry::MAX_DEVICES> light_states_{};
  std::array<std::string, DeviceRegistry::MAX_DEVICES> names_{};
};

}  // namespace elero
}  // namespace esphome
