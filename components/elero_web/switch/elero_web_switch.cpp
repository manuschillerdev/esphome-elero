#include "elero_web_switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

static const char *const TAG = "elero.web_switch";

void EleroWebSwitch::setup() {
  // Restore previous switch state
  auto restore = this->get_initial_state_with_restore_mode();
  if (restore.has_value()) {
    this->write_state(restore.value());
  } else {
    this->write_state(true);  // default: enabled
  }
}

void EleroWebSwitch::write_state(bool state) {
  if (this->server_ != nullptr)
    this->server_->set_enabled(state);
  this->publish_state(state);
  ESP_LOGI(TAG, "Elero Web UI %s", state ? "enabled" : "disabled");
}

void EleroWebSwitch::dump_config() {
  LOG_SWITCH("", "Elero Web UI Switch", this);
}

}  // namespace elero
}  // namespace esphome
