#pragma once

#include "nvs_config.h"
#include "command_sender.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace elero {

class Elero;  // Forward declaration

/// Shared base for dynamic (MQTT-mode) entities.
/// Provides NVS persistence, activation lifecycle, and CommandSender integration.
/// Cover and Light derive from this AND their respective EleroBlindBase/EleroLightBase.
///
/// Derived classes must implement:
///   entity_tag_()       — log tag, e.g. "elero.dyn_cover"
///   entity_type_str_()  — human-readable type, e.g. "cover"
///   is_matching_type_() — NvsDeviceConfig type check
///   do_register_()      — register with hub (e.g. parent_->register_cover(this))
///   do_unregister_()    — unregister from hub
///   reset_entity_state_() — reset entity-specific state on deactivate
class DynamicEntityBase {
 public:
  virtual ~DynamicEntityBase() = default;

  // ─── Persistence ───

  void set_preference(ESPPreferenceObject pref) { pref_ = pref; }
  bool restore();
  [[nodiscard]] bool save_config();
  void clear_config();

  // ─── Activation ───

  bool activate(const NvsDeviceConfig &config, Elero *parent);
  void deactivate();
  void update_config(const NvsDeviceConfig &config);
  bool is_active() const { return active_; }
  bool is_registered() const { return registered_; }

  void register_with_hub();
  void unregister_from_hub();

  // ─── Accessors ───

  const NvsDeviceConfig &config() const { return config_; }
  void set_config_enabled(bool enabled) { config_.set_enabled(enabled); }

  // ─── Common RX metadata ───

  void notify_rx_meta(uint32_t ms, float rssi) { last_seen_ms_ = ms; last_rssi_ = rssi; }
  uint32_t get_last_seen_ms() const { return last_seen_ms_; }
  float get_last_rssi() const { return last_rssi_; }
  uint8_t get_last_state_raw() const { return last_state_raw_; }

 protected:
  virtual const char *entity_tag_() const = 0;
  virtual const char *entity_type_str_() const = 0;
  virtual bool is_matching_type_(const NvsDeviceConfig &config) const = 0;
  virtual void do_register_() = 0;
  virtual void do_unregister_() = 0;
  virtual void reset_entity_state_() = 0;

  void configure_sender_();

  bool active_{false};
  bool registered_{false};
  NvsDeviceConfig config_{};
  Elero *parent_{nullptr};
  CommandSender sender_;
  ESPPreferenceObject pref_{};
  uint32_t last_seen_ms_{0};
  float last_rssi_{0.0f};
  uint8_t last_state_raw_{0};
};

}  // namespace elero
}  // namespace esphome
