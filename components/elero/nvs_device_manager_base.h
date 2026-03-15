/// @file nvs_device_manager_base.h
/// @brief Base class for NVS-backed device managers (shared by MQTT and Native+NVS modes).
///
/// Extracts common slot management, NVS persistence, CRUD operations, and remote tracking
/// from MqttDeviceManager. Mode-specific behavior is delegated to virtual hooks.

#pragma once

#include "device_manager.h"
#include "nvs_config.h"
#include "elero.h"
#include "EleroDynamicCover.h"
#include "EleroDynamicLight.h"
#include "EleroRemoteControl.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace elero {

/// Base class for device managers that use NVS persistence and pre-allocated slots.
/// Provides common CRUD operations, slot management, and remote tracking.
/// Derived classes implement mode-specific publishing and registration hooks.
class NvsDeviceManagerBase : public IDeviceManager, public Component {
 public:
  // ─── Component lifecycle ───

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void dump_config() override;

  // ─── IDeviceManager ───

  void on_rf_packet(const RfPacketInfo &pkt) override;
  bool supports_crud() const override { return true; }

  // ─── Device CRUD ───

  [[nodiscard]] bool upsert_device(const NvsDeviceConfig &config) override;
  [[nodiscard]] bool remove_device(DeviceType type, uint32_t dst_address) override;

  // ─── CRUD event callback ───

  void set_crud_callback(CrudEventCallback cb) override { crud_callback_ = std::move(cb); }

  void for_each_active_remote(const RemoteVisitor &visitor) const override {
    for (size_t i = 0; i < max_remotes_; i++) {
      if (remote_slots_[i].is_active()) {
        visitor(remote_slots_[i].get_address(), remote_slots_[i].get_title(), remote_slots_[i].config().updated_at);
      }
    }
  }

  // ─── Configuration setters (from codegen) ───

  void set_hub(Elero *hub) { hub_ = hub; }
  void set_cover_slots(EleroDynamicCover *slots, size_t count) {
    cover_slots_ = slots;
    max_covers_ = count;
  }
  void set_light_slots(EleroDynamicLight *slots, size_t count) {
    light_slots_ = slots;
    max_lights_ = count;
  }
  void set_remote_slots(EleroRemoteControl *slots, size_t count) {
    remote_slots_ = slots;
    max_remotes_ = count;
  }

 protected:
  // ─── Virtual hooks for mode-specific behavior ───

  /// Called after a cover is activated (restore or CRUD). Publish discovery, subscribe, etc.
  virtual void on_cover_activated_(EleroDynamicCover *cover) = 0;
  /// Called after a light is activated (restore or CRUD).
  virtual void on_light_activated_(EleroDynamicLight *light) = 0;
  /// Called after a remote is activated (restore or auto-discover).
  virtual void on_remote_activated_(EleroRemoteControl *remote) = 0;

  /// Called before a cover is deactivated (CRUD remove). Remove discovery, etc.
  virtual void on_cover_deactivating_(EleroDynamicCover *cover) = 0;
  /// Called before a light is deactivated.
  virtual void on_light_deactivating_(EleroDynamicLight *light) = 0;
  /// Called before a remote is deactivated.
  virtual void on_remote_deactivating_(EleroRemoteControl *remote) = 0;

  /// Called when a cover config is updated. Remove old discovery, re-publish.
  virtual void on_cover_updated_(EleroDynamicCover *cover) = 0;
  /// Called when a light config is updated.
  virtual void on_light_updated_(EleroDynamicLight *light) = 0;
  /// Called when a remote config is updated.
  virtual void on_remote_updated_(EleroRemoteControl *remote) = 0;

  /// Create the state callback for a cover slot.
  virtual EleroDynamicCover::StateCallback make_cover_state_callback_() = 0;
  /// Create the state callback for a light slot.
  virtual EleroDynamicLight::StateCallback make_light_state_callback_() = 0;
  /// Create the state callback for a remote slot.
  virtual EleroRemoteControl::StateCallback make_remote_state_callback_() = 0;

  /// Per-loop hook for mode-specific logic (e.g., MQTT reconnect detection).
  virtual void loop_hook_() {}

  /// Log tag for this manager.
  virtual const char *manager_tag_() const = 0;

  // ─── Slot management ───

  EleroDynamicCover *find_free_cover_slot_();
  EleroDynamicLight *find_free_light_slot_();
  EleroRemoteControl *find_free_remote_slot_();
  EleroDynamicCover *find_active_cover_(uint32_t addr);
  EleroDynamicLight *find_active_light_(uint32_t addr);
  EleroRemoteControl *find_active_remote_(uint32_t addr);

  // ─── Remote control tracking ───

  void track_remote_(const RfPacketInfo &pkt);

  // ─── Preference initialization ───

  void init_slot_preferences_();

  // ─── CRUD event notification ───

  void notify_crud_(const char *event, const char *json_str);
  void notify_crud_(const char *event, uint32_t addr, const char *device_type);
  void notify_crud_upserted_(const NvsDeviceConfig &config);

  // ─── Members ───

  Elero *hub_{nullptr};

  EleroDynamicCover *cover_slots_{nullptr};
  size_t max_covers_{0};
  EleroDynamicLight *light_slots_{nullptr};
  size_t max_lights_{0};
  EleroRemoteControl *remote_slots_{nullptr};
  size_t max_remotes_{0};

  CrudEventCallback crud_callback_{};
};

}  // namespace elero
}  // namespace esphome
