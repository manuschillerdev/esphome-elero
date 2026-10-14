/// @file esp_cover_shell.h
/// @brief Thin ESPHome cover::Cover adapter — delegates all state to the Device model.
///
/// Owns NO cover state. Reads position/operation from the cover state machine.
/// Constructed by `NvsAdapter` at boot from a pre-restored DeviceRegistry slot.
/// Devices are no longer YAML-defined (RFC-002) — they live in NVS.

#pragma once

#ifdef USE_COVER
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/cover/cover.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#include "device.h"
#include "device_registry.h"
#include "cover_sm.h"
#include "state_snapshot.h"  // state_change:: flags
#include "elero_strings.h"   // PERCENT_SCALE
#include "elero_packet.h"
#ifdef USE_BUTTON
#include "refresh_button.h"
#endif

namespace esphome {
namespace elero {

class EspCoverShell : public cover::Cover, public Component {
 public:
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

  // ── Binding (called by NvsAdapter at construction time) ──
  void set_registry(DeviceRegistry *r) { registry_ = r; }
  void set_device(Device *d) { device_ = d; }

  // ── Sensor setters (published from sync_and_publish via snapshot) ──
#ifdef USE_SENSOR
  void set_rssi_sensor(sensor::Sensor *s) { rssi_sensor_ = s; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_status_sensor(text_sensor::TextSensor *s) { status_sensor_ = s; }
  void set_problem_type_sensor(text_sensor::TextSensor *s) { problem_type_sensor_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_problem_sensor(binary_sensor::BinarySensor *s) { problem_sensor_ = s; }
#endif
#ifdef USE_BUTTON
  void set_refresh_button(RefreshButton *b) { refresh_button_ = b; }
#endif

  // ── ESPHome Component lifecycle ────────────────────────────
  void setup() override {
#ifdef USE_BUTTON
    if (refresh_button_ && device_) {
      refresh_button_->set_device(device_);
      refresh_button_->set_registry(registry_);
    }
#endif
    if (device_ && device_->active) {
      sync_and_publish(state_change::ALL);
    }
  }

  // ── ESPHome Cover interface ────────────────────────────────
  cover::CoverTraits get_traits() override {
    auto traits = cover::CoverTraits();
    const auto &cfg = device_->config;
    auto ctx = cover_context(cfg);
    traits.set_supports_position(cover_sm::has_position_tracking(ctx));
    traits.set_supports_tilt(cfg.supports_tilt != 0);
    traits.set_supports_stop(true);
    traits.set_supports_toggle(true);
    traits.set_is_assumed_state(true);
    return traits;
  }

 protected:
  void control(const cover::CoverCall &call) override {
    if (!device_ || !device_->is_cover() || !registry_) return;

    if (call.get_stop()) {
      registry_->command_cover(*device_, packet::command::STOP);
      return;
    }

    if (call.get_position().has_value()) {
      float target = *call.get_position();
      auto ctx = cover_context(device_->config);
      if (cover_sm::has_position_tracking(ctx) &&
          target > cover_sm::POSITION_CLOSED && target < cover_sm::POSITION_OPEN) {
        registry_->set_cover_position(*device_, target);
      } else {
        uint8_t cmd = (target >= cover_sm::POSITION_OPEN) ? packet::command::UP : packet::command::DOWN;
        registry_->command_cover(*device_, cmd);
      }
      return;
    }

    if (call.get_tilt().has_value()) {
      registry_->command_cover_tilt(*device_);
      return;
    }

    if (call.get_toggle().has_value()) {
      auto &cover = std::get<CoverDevice>(device_->logic);
      uint8_t cmd;
      if (cover_sm::is_moving(cover.state)) {
        cmd = packet::command::STOP;
      } else {
        auto ctx = cover_context(device_->config);
        float pos = cover_sm::position(cover.state, millis(), ctx);
        bool was_closing = (cover.last_direction == cover_sm::Operation::CLOSING);
        cmd = (pos <= cover_sm::POSITION_CLOSED || was_closing) ? packet::command::UP : packet::command::DOWN;
      }
      registry_->command_cover(*device_, cmd);
    }
  }

 public:
  void sync_and_publish(uint16_t changes) {
    if (!device_ || !device_->is_cover()) return;
    const auto &pub = std::get<CoverDevice>(device_->logic).published;

    if (changes & (state_change::POSITION | state_change::HA_STATE |
                   state_change::OPERATION | state_change::TILT)) {
      this->position = static_cast<float>(pub.position_pct) / PERCENT_SCALE;
      if (device_->config.supports_tilt != 0) {
        this->tilt = pub.tilted ? cover_sm::POSITION_OPEN : cover_sm::POSITION_CLOSED;
      }
      switch (pub.operation) {
        case cover_sm::Operation::IDLE:
          this->current_operation = cover::COVER_OPERATION_IDLE; break;
        case cover_sm::Operation::OPENING:
          this->current_operation = cover::COVER_OPERATION_OPENING; break;
        case cover_sm::Operation::CLOSING:
          this->current_operation = cover::COVER_OPERATION_CLOSING; break;
      }
      this->publish_state();
    }

#ifdef USE_SENSOR
    if ((changes & state_change::RSSI) && rssi_sensor_ != nullptr)
      rssi_sensor_->publish_state(static_cast<float>(pub.rssi_rounded));
#endif
#ifdef USE_TEXT_SENSOR
    if ((changes & state_change::STATE_STRING) && status_sensor_ != nullptr)
      status_sensor_->publish_state(pub.state_string);
    if ((changes & state_change::PROBLEM) && problem_type_sensor_ != nullptr)
      problem_type_sensor_->publish_state(pub.problem_type);
#endif
#ifdef USE_BINARY_SENSOR
    if ((changes & state_change::PROBLEM) && problem_sensor_ != nullptr)
      problem_sensor_->publish_state(pub.is_problem);
#endif
  }

  DeviceRegistry *registry_{nullptr};
  Device *device_{nullptr};

#ifdef USE_SENSOR
  sensor::Sensor *rssi_sensor_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *status_sensor_{nullptr};
  text_sensor::TextSensor *problem_type_sensor_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *problem_sensor_{nullptr};
#endif
#ifdef USE_BUTTON
  RefreshButton *refresh_button_{nullptr};
#endif
};

}  // namespace elero
}  // namespace esphome

#endif  // USE_COVER
