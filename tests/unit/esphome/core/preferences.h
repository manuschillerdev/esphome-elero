// Host preference storage. Handles for the same key share the saved bytes.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace esphome {

class ESPPreferenceObject {
 public:
  ESPPreferenceObject() = default;
  explicit ESPPreferenceObject(std::vector<uint8_t> &data) : data_(&data) {}

  template<typename T> bool save(const T *value) {
    if (!data_) return false;
    data_->resize(sizeof(T));
    std::memcpy(data_->data(), value, sizeof(T));
    return true;
  }

  template<typename T> bool load(T *value) const {
    if (!data_ || data_->size() != sizeof(T)) return false;
    std::memcpy(value, data_->data(), sizeof(T));
    return true;
  }

 private:
  std::vector<uint8_t> *data_{nullptr};
};

class ESPPreferences {
 public:
  template<typename T>
  ESPPreferenceObject make_preference(uint32_t key) { return ESPPreferenceObject(records_[key]); }

 private:
  std::map<uint32_t, std::vector<uint8_t>> records_;
};

extern ESPPreferences *global_preferences;

}  // namespace esphome
