#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "esphome/components/button/button.h"
#include "esphome/components/cover/cover.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace light {
class LightState;
}

class Application {
 public:
  struct RegisteredCover {
    std::string name;
    uint32_t key;
    std::unique_ptr<cover::Cover> entity;
  };
  std::vector<RegisteredCover> covers;
  std::vector<std::unique_ptr<button::Button>> buttons;

  std::string get_name() const { return "test-gateway"; }
  void register_cover(cover::Cover *entity, const char *name, uint32_t key, uint32_t) {
    if (key == 0) key = fnv1_hash_object_id(name, std::strlen(name));
    covers.push_back({name, key, std::unique_ptr<cover::Cover>(entity)});
  }
  cover::Cover *get_cover_by_key(uint32_t key) const {
    for (const auto &cover : covers) {
      if (cover.key == key) return cover.entity.get();
    }
    return nullptr;
  }
  void register_button(button::Button *entity, const char *, uint32_t, uint32_t) {
    buttons.emplace_back(entity);
  }
  void register_light(light::LightState *state, const char *name, uint32_t key, uint32_t fields) {
    state->configure_entity_(name, key, fields);
  }
};
inline Application App;
}  // namespace esphome
