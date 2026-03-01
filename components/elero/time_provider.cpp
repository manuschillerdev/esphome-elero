/// @file time_provider.cpp
/// @brief Implementation of time provider abstraction.

#include "time_provider.h"

#ifdef UNIT_TEST
// In unit tests, provide a stub
static uint32_t stub_millis_value = 0;
static uint32_t test_millis() { return stub_millis_value; }
#else
// In production, use ESPHome's millis() which handles both Arduino and ESP-IDF
#include "esphome/core/hal.h"
#endif

namespace esphome::elero {

// Static instance of the default time provider
static SystemTimeProvider default_provider;

// Pointer to the active time provider (defaults to system provider)
static TimeProvider* active_provider = &default_provider;

uint32_t SystemTimeProvider::millis() const {
#ifdef UNIT_TEST
  return test_millis();
#else
  return esphome::millis();
#endif
}

TimeProvider& get_time_provider() {
  return *active_provider;
}

void set_time_provider(TimeProvider* provider) {
  if (provider != nullptr) {
    active_provider = provider;
  } else {
    active_provider = &default_provider;
  }
}

}  // namespace esphome::elero
