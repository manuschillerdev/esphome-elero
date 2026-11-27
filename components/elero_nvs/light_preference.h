#pragma once

#include "esphome/components/light/light_state.h"
#include "esphome/core/entity_base.h"

namespace esphome::elero {

// Earlier firmware registered native lights with a name and a zero object ID.
// Let ESPHome resolve that identity, including its fallback for an empty name.
class LegacyLightPreference : private EntityBase {
 public:
  static void migrate(const char *old_name, light::LightState &light) {
    light::LightStateRTCState settings{};
    auto current = light.make_entity_preference<light::LightStateRTCState>();
    if (current.load(&settings)) return;

    LegacyLightPreference old;
    old.configure_entity_(old_name, 0, 0);
    if (old.make_entity_preference<light::LightStateRTCState>().load(&settings)) {
      current.save(&settings);
    }
  }
};

}  // namespace esphome::elero
