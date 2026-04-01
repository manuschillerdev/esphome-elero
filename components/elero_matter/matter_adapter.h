/// @file matter_adapter.h
/// @brief Matter output adapter stub — proves the OutputAdapter architecture supports Matter.
///
/// This is a structural stub. Actual Matter SDK integration depends on esp-matter
/// availability and ESP-IDF 5.x. The adapter registers Window Covering and
/// On/Off + Level Control endpoints for covers and lights respectively.

#pragma once

#include "../elero/output_adapter.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

class MatterAdapter : public OutputAdapter {
 public:
  void setup(DeviceRegistry &registry) override {
    registry_ = &registry;
    ESP_LOGI("elero.matter", "Matter adapter initialized (stub — no Matter SDK linked)");
  }

  void loop() override {
    // Matter stack event loop would go here
  }

  void on_device_added(const Device &dev) override {
    if (dev.is_cover()) {
      ESP_LOGI("elero.matter", "Would create Window Covering endpoint for 0x%06x",
               dev.config.dst_address);
    } else if (dev.is_light()) {
      ESP_LOGI("elero.matter", "Would create On/Off + Level Control endpoint for 0x%06x",
               dev.config.dst_address);
    }
  }

  void on_device_removed(const Device &dev) override {
    ESP_LOGI("elero.matter", "Would remove endpoint for 0x%06x",
             dev.config.dst_address);
  }

  void on_state_changed(const Device &dev, uint16_t /*changes*/) override {
    // Would update Matter attribute values from device state
  }

 private:
  DeviceRegistry *registry_{nullptr};
};

}  // namespace elero
}  // namespace esphome
