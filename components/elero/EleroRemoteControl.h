#pragma once

#include <cstdint>
#include <string>
#include <functional>

namespace esphome::elero {

/// Tracks a physical Elero remote control seen via RF.
/// Remotes are identified by their src_address in command packets (0x6a/0x69).
/// This is a passive entity — we observe remotes, we don't send commands to them.
class EleroRemoteControl {
 public:
  using StateCallback = std::function<void(EleroRemoteControl *)>;

  uint32_t get_address() const { return address_; }
  const char *get_title() const { return title_; }
  float get_rssi() const { return rssi_; }
  uint32_t get_last_seen_ms() const { return last_seen_ms_; }
  uint8_t get_last_channel() const { return last_channel_; }
  uint8_t get_last_command() const { return last_command_; }
  uint32_t get_last_target() const { return last_target_; }

  void set_address(uint32_t addr) { address_ = addr; }
  void set_title(const char *title);

  /// Update from an observed RF command packet
  void update_from_packet(uint32_t timestamp_ms, float rssi, uint8_t channel,
                          uint8_t command, uint32_t target_addr);

  void set_state_callback(StateCallback cb) { state_callback_ = std::move(cb); }

  // For NVS persistence
  bool is_active() const { return address_ != 0; }
  void deactivate();

 private:
  uint32_t address_{0};
  char title_[24]{};
  float rssi_{0.0f};
  uint32_t last_seen_ms_{0};
  uint8_t last_channel_{0};
  uint8_t last_command_{0};
  uint32_t last_target_{0};  ///< Last blind/light this remote targeted
  StateCallback state_callback_{};
};

}  // namespace esphome::elero
