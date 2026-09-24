#include "paper_mono.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::paper_mono {

static const char *const TAG = "paper_mono";
static constexpr uint8_t ELERO_IOE = 0x4F;
static constexpr uint8_t ELERO_PM1 = 0x6E;

bool PaperMono::write_register_(uint8_t address, uint8_t reg, uint8_t value) {
  LockGuard lock(bus_mutex_);
  const uint8_t data[] = {reg, value};
  if (bus_->write(address, data, sizeof(data)) != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "I2C write failed: address=0x%02X reg=0x%02X", address, reg);
    return false;
  }
  return true;
}

bool PaperMono::update_register_(uint8_t address, uint8_t reg, uint8_t mask, uint8_t value) {
  LockGuard lock(bus_mutex_);
  uint8_t current;
  // Use the ESPHome bus abstraction for both supported frameworks.
  if (bus_->write(address, &reg, 1) != i2c::ERROR_OK || bus_->read(address, &current, 1) != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "I2C read failed: address=0x%02X reg=0x%02X", address, reg);
    return false;
  }
  const uint8_t data[] = {reg, static_cast<uint8_t>((current & ~mask) | (value & mask))};
  if (bus_->write(address, data, sizeof(data)) != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "I2C update failed: address=0x%02X reg=0x%02X", address, reg);
    return false;
  }
  return true;
}

bool PaperMono::configure_output_(uint8_t pin, bool high) {
  // Board labels PYG1..PYG14 map to register bits 0..13 (not 1..14).
  const uint8_t index = pin - 1;
  const uint8_t bank = index / 8;
  const uint8_t bit = 1U << (index % 8);
  return update_register_(ELERO_IOE, 0x05 + bank, bit, high ? bit : 0) &&
         update_register_(ELERO_IOE, 0x13 + bank, bit, 0) && update_register_(ELERO_IOE, 0x03 + bank, bit, bit);
}

void PaperMono::setup() {
  delay(500);  // Factory firmware allows M5IOE1 to settle after power-up.
  // Register definitions from M5Stack M5IOE1 / M5PM1 implementations.
  // Disable bus idle sleep; isolate the charger; enable the radio rail.
  bool ok = write_register_(ELERO_IOE, 0x23, 0x00) && configure_output_(11, false) &&
            write_register_(ELERO_PM1, 0x09, 0x00) && write_register_(ELERO_PM1, 0x0A, 0x00) &&
            update_register_(ELERO_PM1, 0x16, 0x30, 0x00) && update_register_(ELERO_PM1, 0x14, 0x30, 0x00) &&
            update_register_(ELERO_PM1, 0x13, 0x04, 0x00) && update_register_(ELERO_PM1, 0x11, 0x04, 0x04) &&
            update_register_(ELERO_PM1, 0x10, 0x04, 0x04) &&
            configure_output_(2, true) &&  // Factory UserDemo holds ANT_SW high.
            configure_output_(10, true);
  if (!ok) {
    this->mark_failed();
    return;
  }
  ready_ = true;
  if (!write_radio_reset(false))
    return;
  delay(100);
  if (!write_radio_reset(true))
    return;
  delay(20);
  ESP_LOGI(TAG, "Radio power and expander reset ready; ANT_SW=HIGH, DIO2 switching");
}

bool PaperMono::write_radio_reset(bool high) {
  // Serialize with display reset and touch reads, including during RF recovery.
  if (!ready_ || !update_register_(ELERO_IOE, 0x06, 0x02, high ? 0x02 : 0)) {
    ESP_LOGE(TAG, "Radio reset control failed");
    ready_ = false;
    reset_failed_ = true;
    return false;
  }
  return true;
}

void PaperMono::loop() {
  // Radio reset may run on the RF task; component status belongs to the main loop.
  if (reset_failed_.exchange(false))
    mark_failed();
}

bool PaperMono::enable_display_touch() {
  if (!ready_)
    return false;
  if (!configure_output_(3, true) || !configure_output_(5, true) || !configure_output_(13, true) ||
      !configure_output_(6, false))
    return false;
  delay(10);  // Setup only; runtime panel waits are cooperative.
  if (!configure_output_(6, true))
    return false;
  delay(100);
  return true;
}

bool PaperMono::write_display_reset(bool high) {
  return ready_ && update_register_(ELERO_IOE, 0x05, 0x10, high ? 0x10 : 0);
}

bool PaperMono::read_touch(uint16_t &x, uint16_t &y, bool &pressed) {
  LockGuard lock(bus_mutex_);
  const uint8_t reg = 0x02;
  uint8_t data[5];
  if (bus_->write(0x38, &reg, 1) != i2c::ERROR_OK || bus_->read(0x38, data, sizeof(data)) != i2c::ERROR_OK) {
    if (!touch_fault_)
      ESP_LOGW(TAG, "Touch controller read failed");
    touch_fault_ = true;
    status_set_warning("Touch controller unavailable");
    return false;
  }
  if (touch_fault_) {
    ESP_LOGI(TAG, "Touch controller recovered");
    touch_fault_ = false;
    status_clear_warning();
  }
  pressed = (data[0] & 0x0F) > 0 && (data[0] & 0x0F) <= 2;
  x = ((data[1] & 0x0F) << 8) | data[2];
  y = ((data[3] & 0x0F) << 8) | data[4];
  return true;
}

void PaperMono::dump_config() {
  ESP_LOGCONFIG(TAG, "PaperMono board: power=%s, radio reset=PYG10", ready_ ? "ready" : "failed");
}

bool PaperMono::prepare_sd() {
  return ready_ && configure_output_(14, true) && update_register_(ELERO_IOE, 0x03, 0x01, 0x00) &&
         update_register_(ELERO_IOE, 0x09, 0x01, 0x01) && update_register_(ELERO_IOE, 0x0B, 0x01, 0x00);
}

std::optional<bool> PaperMono::sd_inserted() {
  LockGuard lock(bus_mutex_);
  uint8_t reg = 0x07, value = 0xFF;
  if (bus_->write(ELERO_IOE, &reg, 1) != i2c::ERROR_OK || bus_->read(ELERO_IOE, &value, 1) != i2c::ERROR_OK)
    return std::nullopt;
  return !(value & 0x01);
}

}  // namespace esphome::paper_mono
