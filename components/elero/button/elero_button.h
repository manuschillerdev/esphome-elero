#pragma once

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "esphome/components/elero/elero.h"

namespace esphome {
namespace elero {

class EleroScanButton : public button::Button, public Component {
 public:
  void set_elero_parent(Elero *parent) { parent_ = parent; }
  void set_scan_start(bool start) { scan_start_ = start; }
  void dump_config() override;

 protected:
  void press_action() override;

  Elero *parent_{nullptr};
  bool scan_start_{true};
};

}  // namespace elero
}  // namespace esphome
