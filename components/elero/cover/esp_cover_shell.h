/// @file esp_cover_shell.h
/// @brief Thin ESPHome cover::Cover adapter — delegates all state to the Device model.
///
/// Owns NO cover state. Reads position/operation from the cover state machine.
/// During setup(), registers the YAML-defined device with the DeviceRegistry.
/// During loop(), publishes state when the device's notify timestamp changes.

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/cover/cover.h"
#include "../device.h"
#include "../device_registry.h"
#include "../cover_sm.h"
#include "../elero_packet.h"

namespace esphome {
namespace elero {

class EspCoverShell : public cover::Cover, public Component {
 public:
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

  // ── Config setters (called by Python codegen) ──────────────
  void set_registry(DeviceRegistry *r) { registry_ = r; }
  void set_slot_index(int idx) { slot_index_ = idx; }  ///< NVS mode: bind to pre-restored slot
  void set_dst_address(uint32_t v) { cfg_.dst_address = v; }
  void set_src_address(uint32_t v) { cfg_.src_address = v; }
  void set_channel(uint8_t v) { cfg_.channel = v; }
  void set_hop(uint8_t v) { cfg_.hop = v; }
  void set_payload_1(uint8_t v) { cfg_.payload_1 = v; }
  void set_payload_2(uint8_t v) { cfg_.payload_2 = v; }
  void set_type(uint8_t v) { cfg_.type_byte = v; }
  void set_type2(uint8_t v) { cfg_.type2 = v; }
  void set_supports_tilt(bool v) { cfg_.supports_tilt = v ? 1 : 0; }
  void set_open_duration(uint32_t v) { cfg_.open_duration_ms = v; }
  void set_close_duration(uint32_t v) { cfg_.close_duration_ms = v; }
  void set_poll_interval(uint32_t v) { cfg_.poll_interval_ms = v; }

  // ── ESPHome Component lifecycle ────────────────────────────
  void setup() override {
    if (!registry_) return;
    if (slot_index_ >= 0) {
      // NVS mode: bind to pre-restored registry slot
      device_ = registry_->slot(static_cast<size_t>(slot_index_));
    } else {
      // Native mode: register device from YAML config
      cfg_.type = DeviceType::COVER;
      cfg_.set_name(this->get_name().c_str());
      device_ = registry_->register_device(cfg_);
    }
  }

  void loop() override {
    if (!device_ || !device_->active) return;
    // Publish when registry signals a state change (notify timestamp updated)
    if (device_->last_notify_ms > last_published_ms_) {
      sync_and_publish_();
      last_published_ms_ = device_->last_notify_ms;
    }
  }

  // ── ESPHome Cover interface ────────────────────────────────
  cover::CoverTraits get_traits() override {
    auto traits = cover::CoverTraits();
    // Use device config when bound (NVS mode), fall back to local cfg_ (native mode)
    const auto &cfg = (device_ && device_->active) ? device_->config : cfg_;
    auto ctx = cover_context(cfg);
    traits.set_supports_position(cover_sm::has_position_tracking(ctx));
    traits.set_supports_tilt(cfg.supports_tilt != 0);
    traits.set_supports_stop(true);
    traits.set_supports_toggle(true);
    traits.set_is_assumed_state(false);
    return traits;
  }

 protected:
  void control(const cover::CoverCall &call) override {
    if (!device_ || !device_->is_cover()) return;

    auto &cover = std::get<CoverDevice>(device_->logic);
    auto ctx = cover_context(device_->config);
    uint32_t now = esphome::millis();

    if (call.get_stop()) {
      device_->sender.clear_queue();
      (void) device_->sender.enqueue(packet::command::STOP);
      cover.state = cover_sm::on_command(cover.state, packet::command::STOP, now, ctx);
      cover.target_position = -1.0f;
      sync_and_publish_();
      return;
    }

    if (call.get_position().has_value()) {
      float target = *call.get_position();
      uint8_t cmd;
      if (target >= 1.0f) {
        cmd = packet::command::UP;
        cover.target_position = -1.0f;  // Blind handles endpoint
      } else if (target <= 0.0f) {
        cmd = packet::command::DOWN;
        cover.target_position = -1.0f;  // Blind handles endpoint
      } else {
        float current = cover_sm::position(cover.state, now, ctx);
        cmd = (target > current) ? packet::command::UP : packet::command::DOWN;
        cover.target_position = target;
      }
      (void) device_->sender.enqueue(cmd);
      cover.state = cover_sm::on_command(cover.state, cmd, now, ctx);
      if (cmd == packet::command::UP) cover.last_direction = cover_sm::Operation::OPENING;
      if (cmd == packet::command::DOWN) cover.last_direction = cover_sm::Operation::CLOSING;
      cover.poll.on_command_sent(now);
      sync_and_publish_();
      return;
    }

    if (call.get_tilt().has_value()) {
      (void) device_->sender.enqueue(packet::command::TILT);
      cover.state = cover_sm::on_command(cover.state, packet::command::TILT, now, ctx);
      cover.poll.on_command_sent(now);
      sync_and_publish_();
      return;
    }

    if (call.get_toggle().has_value()) {
      // Toggle: if moving → stop; if closed or was closing → open; else close
      uint8_t cmd;
      if (cover_sm::is_moving(cover.state)) {
        device_->sender.clear_queue();
        cmd = packet::command::STOP;
      } else {
        float pos = cover_sm::position(cover.state, now, ctx);
        bool was_closing = (cover.last_direction == cover_sm::Operation::CLOSING);
        cmd = (pos <= 0.0f || was_closing) ? packet::command::UP : packet::command::DOWN;
      }
      (void) device_->sender.enqueue(cmd);
      cover.state = cover_sm::on_command(cover.state, cmd, now, ctx);
      cover.poll.on_command_sent(now);
      sync_and_publish_();
    }
  }

 private:
  void sync_and_publish_() {
    if (!device_ || !device_->is_cover()) return;
    auto &cover = std::get<CoverDevice>(device_->logic);
    auto ctx = cover_context(device_->config);
    uint32_t now = esphome::millis();

    this->position = cover_sm::position(cover.state, now, ctx);
    auto op = cover_sm::operation(cover.state);
    switch (op) {
      case cover_sm::Operation::IDLE:
        this->current_operation = cover::COVER_OPERATION_IDLE; break;
      case cover_sm::Operation::OPENING:
        this->current_operation = cover::COVER_OPERATION_OPENING; break;
      case cover_sm::Operation::CLOSING:
        this->current_operation = cover::COVER_OPERATION_CLOSING; break;
    }
    this->publish_state();
  }

  NvsDeviceConfig cfg_{};
  DeviceRegistry *registry_{nullptr};
  Device *device_{nullptr};
  uint32_t last_published_ms_{0};
  int slot_index_{-1};  ///< -1 = native mode (use cfg_), >=0 = NVS mode (bind to registry slot)
};

}  // namespace elero
}  // namespace esphome
