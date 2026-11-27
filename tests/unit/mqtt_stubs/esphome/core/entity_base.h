#pragma once

#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include <cstring>

namespace esphome {
class EntityBase {
 public:
  template<typename T> ESPPreferenceObject make_entity_preference() {
    return global_preferences->make_preference<T>(key_);
  }

 protected:
  friend class Application;
  void configure_entity_(const char *name, uint32_t key, uint32_t) {
    key_ = key != 0 ? key : fnv1_hash_object_id(name, std::strlen(name));
  }

 private:
  uint32_t key_{};
};
}  // namespace esphome
