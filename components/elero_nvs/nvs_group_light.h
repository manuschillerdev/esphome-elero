#pragma once

#include "../elero/device_registry.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/component.h"

namespace esphome::elero {

class NvsGroupLight final : public light::LightOutput, public Component {
 public:
  NvsGroupLight(DeviceRegistry &registry, const char *id, light::LightState &state)
      : registry_(registry), id_(id), state_(state) {}

  const std::string &id() const { return id_; }

  void setup() override {
    initialized_ = true;
    const auto *group = registry_.find_group(id_.c_str());
    if (group != nullptr) sync_and_publish(registry_.group_snapshot(*group, millis()));
  }

  light::LightTraits get_traits() override {
    light::LightTraits traits;
    traits.set_supported_color_modes({light::ColorMode::ON_OFF});
    return traits;
  }

  void update_state(light::LightState *state) override {
    if (!initialized_) return;  // Ignore LightState's initial restore.
    const auto *group = registry_.find_group(id_.c_str());
    if (group == nullptr || registry_.group_snapshot(*group, millis()).type != DeviceType::LIGHT) return;
    const auto command = state->current_values.is_on() ? packet::command::UP : packet::command::DOWN;
    registry_.command_saved_group(id_.c_str(), command);
  }

  void write_state(light::LightState *) override {}

  void sync_and_publish(const GroupStateSnapshot &snapshot) {
    if (!initialized_ || !snapshot.valid || snapshot.type != DeviceType::LIGHT) return;
    state_.current_values.set_state(snapshot.is_on);
    state_.remote_values.set_state(snapshot.is_on);
    state_.publish_state();
  }

 private:
  DeviceRegistry &registry_;
  std::string id_;
  light::LightState &state_;
  bool initialized_{false};
};

}  // namespace esphome::elero
