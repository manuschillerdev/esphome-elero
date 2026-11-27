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
#include "nvs_group_light.h"
#include "light_preference.h"
#ifdef ELERO_RSSI_CLASS
#include "native_diagnostics.h"
#endif
#include "../elero/device_type.h"
#include "../elero/esp_cover_shell.h"
#include "../elero/esp_light_shell.h"
#include "../elero/esp_cover_action_button.h"
#include "../elero/cover_actions.h"
#include "../elero/output_adapter.h"
#include "esphome/components/light/light_state.h"
#include <array>
#include <string>

namespace esphome {
namespace elero {

static_assert(ELERO_COVER_ACTIONS.size() == ELERO_COVER_ACTION_BUTTON_COUNT,
              "Native button capacity must match the shared cover actions");

// Registered by Python codegen so LightState's loop enable/disable and scheduler
// participate in the normal ESPHome lifecycle. Only NVS-bound slots are entities.
class NvsLightState final : public light::LightState {
 public:
  NvsLightState() : light::LightState(nullptr) {}
  float get_setup_priority() const override { return setup_priority::DATA - 3.0f; }
  template<typename Output> void bind(Output *output) {
    this->output_ = output;
    this->output_component_ = output;
    set_default_transition_length(0);
  }
  void setup() override {
    if (output_ == nullptr) {
      disable_loop();
      return;
    }
    light::LightState::setup();
    output_component_->setup();
  }
  void dump_config() override {
    if (output_ != nullptr) light::LightState::dump_config();
  }

 private:
  Component *output_component_{nullptr};
};

class NvsGroupCover final : public cover::Cover {
 public:
  NvsGroupCover(DeviceRegistry &registry, const char *id) : registry_(registry), id_(id) {}
  const std::string &id() const { return id_; }
  cover::CoverTraits get_traits() override {
    cover::CoverTraits traits;
    const auto *group = registry_.find_group(id_.c_str());
    traits.set_supports_position(group != nullptr && registry_.group_snapshot(*group, millis()).supports_position);
    traits.set_supports_stop(true);
    traits.set_is_assumed_state(true);
    return traits;
  }
  void sync_and_publish(const GroupStateSnapshot &state) {
    if (!state.valid || state.type != DeviceType::COVER) return;
    position = static_cast<float>(state.position_pct) / PERCENT_SCALE;
    current_operation = state.operation == cover_sm::Operation::OPENING ? cover::COVER_OPERATION_OPENING
        : state.operation == cover_sm::Operation::CLOSING ? cover::COVER_OPERATION_CLOSING : cover::COVER_OPERATION_IDLE;
    publish_state();
  }

 protected:
  void control(const cover::CoverCall &call) override {
    const auto *group = registry_.find_group(id_.c_str());
    if (group == nullptr || registry_.group_snapshot(*group, millis()).type != DeviceType::COVER) return;
    if (call.get_stop()) {
      send_command_(packet::command::STOP);
    } else if (call.get_position().has_value()) {
      const float target = *call.get_position();
      if (target <= 0.0f || target >= 1.0f) {
        send_command_(target >= 1.0f ? packet::command::UP : packet::command::DOWN);
      } else {
        registry_.set_group_position(id_.c_str(), target);
      }
    }
  }

 private:
  void send_command_(uint8_t command) {
    std::string error;
    if (!registry_.command_saved_group(id_.c_str(), command, &error)) {
      ESP_LOGW("nvs_adapter", "Group '%s' command rejected: %s", id_.c_str(), error.c_str());
    }
  }
  DeviceRegistry &registry_;
  std::string id_;
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
  void on_group_state_changed(const NvsGroupConfig &group, const GroupStateSnapshot &state) override {
    for (auto *shell : group_shells_) {
      if (shell != nullptr && shell->id() == group.id) shell->sync_and_publish(state);
    }
    for (auto *shell : group_light_shells_) {
      if (shell != nullptr && shell->id() == group.id) shell->sync_and_publish(state);
    }
  }
  void on_state_changed(const Device &dev, uint16_t changes) override {
    size_t idx = registry_->slot_index(dev);
#ifdef ELERO_RSSI_CLASS
    if (diagnostics_[idx] != nullptr) diagnostics_[idx]->publish(dev, changes);
#endif
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

      names_[i] = display_name_(dev->config.name, device_id_(dev->config), dev, nullptr);
#ifdef ELERO_RSSI_CLASS
      diagnostics_[i] = new NativeDiagnostics(*dev, names_[i], device_id_(dev->config));
#endif
      if (dev->is_cover()) {
        create_cover_(dev, i);
        ++covers;
      } else if (dev->is_light()) {
        create_light_(dev, i);
        ++lights;
      }
    }

    ESP_LOGI("nvs_adapter", "Created %zu cover(s) and %zu light(s) from NVS", covers, lights);
    size_t group_index = 0;
    registry_->for_each_group([&](const NvsGroupConfig &group) {
      const auto snapshot = registry_->group_snapshot(group, millis());
      if (!snapshot.valid) return;
      const auto object_id = std::string("elero_group_") + group.id;
      group_names_[group_index] = display_name_(group.name, object_id, nullptr, &group);
      if (snapshot.type == DeviceType::LIGHT) {
        auto *state = light_states_[DeviceRegistry::MAX_DEVICES + group_index];
        auto *shell = new NvsGroupLight(*registry_, group.id, *state);
        state->bind(shell);
        group_light_shells_[group_index] = shell;
        App.register_light(state, group_names_[group_index].c_str(), fnv1_hash(object_id), 0);
      } else {
        auto *shell = new NvsGroupCover(*registry_, group.id);
        group_shells_[group_index] = shell;
        App.register_cover(shell, group_names_[group_index].c_str(), fnv1_hash(object_id), 0);
        shell->sync_and_publish(snapshot);
      }
      ++group_index;
    });
    ESP_LOGI("nvs_adapter", "Created %zu native group(s) from NVS", group_index);
  }

 private:
  static std::string device_id_(const NvsDeviceConfig &cfg) {
    return std::string("elero_") + device_type_str(cfg.type) + "_" + hex_str(cfg.dst_address);
  }

  std::string display_name_(const char *name, const std::string &id, const Device *self,
                            const NvsGroupConfig *self_group) const {
    if (*name == '\0') return id;
    // Home Assistant also uses the advertised name for entity identity.
    const auto key = fnv1_hash_object_id(name, strlen(name));
    const auto matches = [key](const char *other) { return fnv1_hash_object_id(other, strlen(other)) == key; };
    bool duplicate = false;
    registry_->for_each_active([&](const Device &other) {
      if (&other != self && other.config.is_enabled() && matches(other.config.name)) duplicate = true;
    });
    registry_->for_each_group([&](const NvsGroupConfig &other) {
      if (&other != self_group && matches(other.name)) duplicate = true;
    });
    if (!duplicate) return name;
    const auto suffix = self != nullptr ? std::string(device_type_str(self->config.type)) + " " + hex_str(self->config.dst_address)
                                        : std::string("group ") + self_group->id;
    return std::string(name) + " (" + suffix + ")";
  }

  void create_cover_(Device *dev, size_t slot_index) {
    auto *shell = new EspCoverShell();  // Firmware-lifetime entity
    shell->set_registry(registry_);
    shell->set_device(dev);

    cover_shells_[slot_index] = shell;
    // EntityBase retains a StringRef: keep the boot name alive and immutable
    // even if the registry config is edited before the required reboot.
    App.register_cover(shell, names_[slot_index].c_str(), fnv1_hash(device_id_(dev->config)), 0);
    // Covers have no periodic work; initialize directly after binding.
    shell->setup();

    for (size_t i = 0; i < ELERO_COVER_ACTIONS.size(); ++i) {
      const auto &action = ELERO_COVER_ACTIONS[i];
      auto *button = new EspCoverActionButton(*registry_, dev->config.dst_address, action.action);
      button_names_[slot_index][i] = names_[slot_index] + " " + action.name;
      const auto object_id = std::string("elero_cover_") + hex_str(dev->config.dst_address) + "_" + action.action;
      App.register_button(button, button_names_[slot_index][i].c_str(), fnv1_hash(object_id), 0);
    }
  }

  void create_light_(Device *dev, size_t slot_index) {
    auto *state = light_states_[slot_index];
    auto *output = new EspLightShell();
    state->bind(output);
    output->set_registry(registry_);
    output->set_device(dev);

    // Restore saved values, or use LightStateRTCState's off state on first boot.
    state->set_save_enabled(true);

    output->set_light_state(state);

    light_shells_[slot_index] = output;
    App.register_light(state, names_[slot_index].c_str(), fnv1_hash(device_id_(dev->config)), 0);
    LegacyLightPreference::migrate(dev->config.name, *state);
  }

#ifdef ELERO_RSSI_CLASS
  std::array<NativeDiagnostics *, DeviceRegistry::MAX_DEVICES> diagnostics_{};
#endif
  DeviceRegistry *registry_{nullptr};
  std::array<EspCoverShell *, DeviceRegistry::MAX_DEVICES> cover_shells_{};
  std::array<EspLightShell *, DeviceRegistry::MAX_DEVICES> light_shells_{};
  std::array<NvsLightState *, DeviceRegistry::MAX_DEVICES + DeviceRegistry::MAX_GROUPS> light_states_{};
  std::array<std::string, DeviceRegistry::MAX_DEVICES> names_{};
  // EntityBase retains StringRef names; keep each boot name alive and immutable.
  std::array<std::array<std::string, ELERO_COVER_ACTIONS.size()>, DeviceRegistry::MAX_DEVICES> button_names_{};
  std::array<NvsGroupLight *, DeviceRegistry::MAX_GROUPS> group_light_shells_{};
  std::array<NvsGroupCover *, DeviceRegistry::MAX_GROUPS> group_shells_{};
  std::array<std::string, DeviceRegistry::MAX_GROUPS> group_names_{};
};

}  // namespace elero
}  // namespace esphome
