#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include "nvs_config.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace elero {

/// Tracks a physical Elero remote control seen via RF.
/// Remotes are identified by their src_address in command packets (0x6a/0x69).
/// This is a passive entity — we observe remotes, we don't send commands to them.
///
/// Lifecycle matches EleroDynamicCover/EleroDynamicLight:
///   set_preference() → restore() → activate() → [update_from_packet()...] → deactivate()
///
/// Unlike covers/lights, remotes don't inherit DynamicEntityBase because they
/// don't need CommandSender, hub registration, or the registered_ flag.
/// They share the same NvsDeviceConfig + ESPPreferenceObject persistence pattern.
class EleroRemoteControl {
 public:
  using StateCallback = std::function<void(EleroRemoteControl *)>;

  // ─── Persistence ───

  void set_preference(ESPPreferenceObject pref) { pref_ = pref; }
  bool restore();
  [[nodiscard]] bool save_config();
  void clear_config();

  // ─── Activation ───

  /// Activate with address and name. Returns true on success.
  bool activate(uint32_t address, const char *name);
  void deactivate();
  bool is_active() const { return active_; }

  // ─── Accessors ───

  uint32_t get_address() const { return config_.dst_address; }
  const char *get_title() const { return config_.name; }
  float get_rssi() const { return rssi_; }
  uint32_t get_last_seen_ms() const { return last_seen_ms_; }
  uint8_t get_last_channel() const { return last_channel_; }
  uint8_t get_last_command() const { return last_command_; }
  uint32_t get_last_target() const { return last_target_; }

  /// Update from an observed RF command packet
  void update_from_packet(uint32_t timestamp_ms, float rssi, uint8_t channel,
                           uint8_t command, uint32_t target_addr);

  void set_state_callback(StateCallback cb) { state_callback_ = std::move(cb); }

 private:
  bool active_{false};
  NvsDeviceConfig config_{};
  float rssi_{0.0f};
  uint32_t last_seen_ms_{0};
  uint8_t last_channel_{0};
  uint8_t last_command_{0};
  uint32_t last_target_{0};  ///< Last blind/light this remote targeted
  StateCallback state_callback_{};
  ESPPreferenceObject pref_{};
};

}  // namespace elero
}  // namespace esphome
