// Stub for unit tests
#pragma once
#include <optional>

namespace esphome {

template<typename T>
using optional = std::optional<T>;

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void mark_failed() {}
};

}  // namespace esphome
