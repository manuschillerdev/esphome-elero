#pragma once

#include <string>

namespace esphome {
class Application {
 public:
    std::string get_name() const { return "test-gateway"; }
};
inline Application App;
}  // namespace esphome
