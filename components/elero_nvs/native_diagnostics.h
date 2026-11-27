#pragma once

#include "../elero/device.h"
#include "../elero/state_snapshot.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/application.h"

namespace esphome::elero {

// Allocate diagnostics only for enabled, restored slots. Each entity keeps its
// boot name alive because ESPHome retains a reference to the supplied string.
class NativeDiagnostics {
 public:
  NativeDiagnostics(const Device &dev, const std::string &name, const std::string &id)
      : address_(dev.config.dst_address), type_(dev.config.type), rssi_name_(name + " RSSI"),
        state_name_(name + (dev.is_remote() ? " Last command" : " State")) {
    constexpr uint32_t diagnostic = ENTITY_CATEGORY_DIAGNOSTIC << ENTITY_FIELD_ENTITY_CATEGORY_SHIFT;
    constexpr uint32_t rssi_fields = diagnostic | (ELERO_RSSI_CLASS << ENTITY_FIELD_DC_SHIFT) |
                                     (ELERO_DBM_UNIT << ENTITY_FIELD_UOM_SHIFT);
    App.register_sensor(&rssi_, rssi_name_.c_str(), fnv1_hash(id + "_rssi"), rssi_fields);
    rssi_.set_accuracy_decimals(0);
    rssi_.set_state_class(sensor::STATE_CLASS_MEASUREMENT);
    App.register_text_sensor(&state_, state_name_.c_str(), fnv1_hash(id + "_state"), diagnostic);
    if (!dev.is_remote()) {
      problem_ = new binary_sensor::BinarySensor();
      problem_name_ = name + " Problem";
      App.register_binary_sensor(problem_, problem_name_.c_str(), fnv1_hash(id + "_problem"),
                                 diagnostic | (ELERO_PROBLEM_CLASS << ENTITY_FIELD_DC_SHIFT));
    }
  }

  void publish(const Device &dev, uint16_t changes) {
    if (dev.config.dst_address != address_ || dev.config.type != type_ || !dev.config.is_enabled() ||
        dev.rf.last_seen_ms == 0) return;
    if (changes & state_change::RSSI) rssi_.publish_state(dev.rf.last_rssi);
    if (dev.is_remote()) {
      if (changes & state_change::REMOTE_ACTIVITY) {
        const auto &remote = std::get<RemoteDevice>(dev.logic);
        state_.publish_state(hex_str8(remote.last_command) + " / channel " + std::to_string(remote.last_channel) +
                             " / " + hex_str(remote.last_target));
      }
    } else if (dev.is_cover()) {
      publish_device_(std::get<CoverDevice>(dev.logic).published, changes);
    } else {
      publish_device_(std::get<LightDevice>(dev.logic).published, changes);
    }
  }

 private:
  template<typename Published> void publish_device_(const Published &pub, uint16_t changes) {
    if (changes & state_change::STATE_STRING) state_.publish_state(pub.state_string);
    if (changes & state_change::PROBLEM) problem_->publish_state(pub.is_problem);
  }

  uint32_t address_;
  DeviceType type_;
  std::string rssi_name_, state_name_, problem_name_;
  sensor::Sensor rssi_;
  text_sensor::TextSensor state_;
  binary_sensor::BinarySensor *problem_{nullptr};
};

}  // namespace esphome::elero
