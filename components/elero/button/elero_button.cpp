#include "elero_button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

static const char *const TAG = "elero.button";

void EleroScanButton::dump_config() {
  LOG_BUTTON("", "Elero Scan Button", this);
  ESP_LOGCONFIG(TAG, "  Action: %s", this->scan_start_ ? "start_scan" : "stop_scan");
}

void EleroScanButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "Elero parent not configured");
    return;
  }
  if (this->scan_start_) {
    ESP_LOGI(TAG, "Starting Elero RF scan...");
    this->parent_->clear_discovered();
    this->parent_->start_scan();
  } else {
    this->parent_->stop_scan();
    ESP_LOGI(TAG, "Stopped Elero RF scan. Discovered %d device(s).", this->parent_->get_discovered_count());
    for (const auto &blind : this->parent_->get_discovered_blinds()) {
      ESP_LOGI(TAG, "  addr=0x%06x remote=0x%06x ch=%d rssi=%.1f state=%s seen=%d",
               blind.blind_address, blind.remote_address, blind.channel,
               blind.rssi, elero_state_to_string(blind.last_state), blind.times_seen);
    }
  }
}

}  // namespace elero
}  // namespace esphome
