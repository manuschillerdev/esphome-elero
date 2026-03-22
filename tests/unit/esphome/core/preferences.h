// Stub for unit tests
#pragma once
#include <cstdint>
#include <string>

namespace esphome {

class ESPPreferenceObject {
 public:
  template<typename T> bool save(const T *) { return true; }
  template<typename T> bool load(T *) { return false; }
};

class ESPPreferences {
 public:
  template<typename T>
  ESPPreferenceObject make_preference(uint32_t) { return {}; }
};

extern ESPPreferences *global_preferences;

}  // namespace esphome
