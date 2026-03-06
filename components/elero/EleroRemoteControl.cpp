#include "EleroRemoteControl.h"
#include <cstring>

namespace esphome::elero {

void EleroRemoteControl::set_title(const char *title) {
  if (title == nullptr) {
    title_[0] = '\0';
    return;
  }
  strncpy(title_, title, sizeof(title_) - 1);
  title_[sizeof(title_) - 1] = '\0';
}

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

void EleroRemoteControl::deactivate() {
  address_ = 0;
  title_[0] = '\0';
  rssi_ = 0.0f;
  last_seen_ms_ = 0;
  last_channel_ = 0;
  last_command_ = 0;
  last_target_ = 0;
  state_callback_ = nullptr;
}

}  // namespace esphome::elero
