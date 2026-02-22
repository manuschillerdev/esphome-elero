#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "../elero_web_server.h"

namespace esphome {
namespace elero {

class EleroWebSwitch : public switch_::Switch, public Component {
 public:
  void set_web_server(EleroWebServer *server) { this->server_ = server; }
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  void write_state(bool state) override;
  EleroWebServer *server_{nullptr};
};

}  // namespace elero
}  // namespace esphome
