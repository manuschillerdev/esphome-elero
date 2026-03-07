#include "EleroDynamicLight.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

// ─── State handling ───

void EleroDynamicLight::set_rx_state(uint8_t state) {
  last_state_raw_ = state;
  if (state == packet::state::LIGHT_ON) {
    is_on_ = true;
  } else if (state == packet::state::LIGHT_OFF || state == packet::state::BOTTOM_TILT) {
    is_on_ = false;
  }
  publish_state_();
}

void EleroDynamicLight::schedule_immediate_poll() {
  (void)sender_.enqueue(packet::command::CHECK);
}

bool EleroDynamicLight::perform_action(const char *action) {
  if (action == nullptr) return false;
  if (strcmp(action, "on") == 0 || strcmp(action, "up") == 0) {
    (void)sender_.enqueue(packet::command::UP);
    return true;
  }
  if (strcmp(action, "off") == 0 || strcmp(action, "down") == 0) {
    (void)sender_.enqueue(packet::command::DOWN);
    return true;
  }
  if (strcmp(action, "stop") == 0) {
    (void)sender_.enqueue(packet::command::STOP);
    return true;
  }
  if (strcmp(action, "check") == 0) {
    (void)sender_.enqueue(packet::command::CHECK);
    return true;
  }
  return false;
}

// ─── Loop ───

void EleroDynamicLight::loop(uint32_t now) {
  if (!active_ || parent_ == nullptr || !registered_) return;
  sender_.process_queue(now, parent_, entity_tag_());
}

void EleroDynamicLight::publish_state_() {
  if (state_callback_) {
    state_callback_(this);
  }
}

}  // namespace elero
}  // namespace esphome
