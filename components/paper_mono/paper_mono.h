#pragma once

// M5Stack PaperMono board support. Radio reset, display reset, and touch share a bus mutex.
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include <optional>
#include <atomic>

namespace esphome::paper_mono {

class PaperMono : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::IO; }
  bool write_radio_reset(bool high);
  bool enable_display_touch();
  bool write_display_reset(bool high);
  bool read_touch(uint16_t &x, uint16_t &y, bool &pressed);
  bool prepare_sd();
  std::optional<bool> sd_inserted();

 private:
  bool update_register_(uint8_t address, uint8_t reg, uint8_t mask, uint8_t value);
  bool write_register_(uint8_t address, uint8_t reg, uint8_t value);
  bool configure_output_(uint8_t pin, bool high);
  std::atomic<bool> ready_{false};
  std::atomic<bool> reset_failed_{false};
  bool touch_fault_{false};
  Mutex bus_mutex_;
};

class RadioResetPin : public GPIOPin {
 public:
  void set_parent(PaperMono *parent) { parent_ = parent; }
  void setup() override {}
  void pin_mode(gpio::Flags flags) override {}
  gpio::Flags get_flags() const override { return gpio::FLAG_OUTPUT; }
  bool digital_read() override { return high_; }
  void digital_write(bool high) override {
    if (parent_->write_radio_reset(high))
      high_ = high;
  }
  size_t dump_summary(char *buffer, size_t len) const override {
    return snprintf(buffer, len, "PaperMono M5IOE1 PYG10 (radio reset)");
  }

 private:
  PaperMono *parent_{nullptr};
  std::atomic<bool> high_{true};
};

}  // namespace esphome::paper_mono
