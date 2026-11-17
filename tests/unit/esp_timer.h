#pragma once

#include "elero/time_provider.h"

inline int64_t esp_timer_get_time() {
  return static_cast<int64_t>(esphome::elero::get_time_provider().millis()) * 1000;
}
