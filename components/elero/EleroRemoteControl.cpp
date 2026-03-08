#include "EleroRemoteControl.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

static constexpr const char *TAG = "elero.remote";

// ─── Persistence ───

bool EleroRemoteControl::restore() {
  NvsDeviceConfig cfg{};
  if (!pref_.load(&cfg) || !cfg.is_valid()) {
    return false;
  }
  config_ = cfg;
  return true;
}

bool EleroRemoteControl::save_config() {
  config_.type = DeviceType::REMOTE;
  config_.updated_at = millis();
  if (!pref_.save(&config_)) {
    ESP_LOGE(TAG, "NVS save failed for remote 0x%06x", config_.dst_address);
    return false;
  }
  return true;
}

void EleroRemoteControl::clear_config() {
  NvsDeviceConfig empty{};
  pref_.save(&empty);
}

// ─── Activation ───

bool EleroRemoteControl::activate(const NvsDeviceConfig &config) {
  if (active_) return false;
  if (!config.is_valid() || !config.is_remote()) return false;

  config_ = config;
  active_ = true;
  return true;
}

void EleroRemoteControl::update_config(const NvsDeviceConfig &config) {
  if (!active_) return;
  config_ = config;
  (void)save_config();
}

void EleroRemoteControl::deactivate() {
  if (!active_) return;
  clear_config();
  active_ = false;
  config_ = NvsDeviceConfig{};
  rssi_ = 0.0f;
  last_seen_ms_ = 0;
  last_channel_ = 0;
  last_command_ = 0;
  last_target_ = 0;
  state_callback_ = nullptr;
}

// ─── State ───

void EleroRemoteControl::update_from_packet(uint32_t timestamp_ms, float rssi, uint8_t channel,
                                             uint8_t command, uint32_t target_addr) {
  rssi_ = rssi;
  last_seen_ms_ = timestamp_ms;
  last_channel_ = channel;
  last_command_ = command;
  last_target_ = target_addr;

  if (state_callback_) {
    state_callback_(this);
  }
}

}  // namespace elero
}  // namespace esphome
