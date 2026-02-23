#pragma once

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "../elero.h"

namespace esphome {
namespace elero {

class EleroScanButton : public button::Button, public Component {
 public:
  void set_elero_parent(Elero *parent) { parent_ = parent; }
  void set_scan_start(bool start) { scan_start_ = start; }
  void set_light(EleroLightBase *light) { light_ = light; }
  void set_command_byte(uint8_t cmd) { command_byte_ = cmd; }
  void dump_config() override;

 protected:
  void press_action() override;

  Elero *parent_{nullptr};
  bool scan_start_{true};
  EleroLightBase *light_{nullptr};
  uint8_t command_byte_{0x44};
};

}  // namespace elero
}  // namespace esphome
