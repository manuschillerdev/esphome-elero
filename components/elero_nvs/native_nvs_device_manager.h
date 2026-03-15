#pragma once

#include "../elero/device_manager.h"
#include "../elero/nvs_config.h"
#include "../elero/elero.h"
#include "../elero/EleroRemoteControl.h"
#include "NativeNvsCover.h"
#include "NativeNvsLight.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/helpers.h"
#include <string>

namespace esphome {
namespace elero {

/// Native+NVS device manager: NVS persistence + ESPHome native API.
///
/// On setup(), restores slots from NVS and registers active ones as ESPHome
/// cover::Cover and light::LightOutput components. The native API discovers
/// them automatically.
///
/// Post-setup CRUD writes to NVS but changes only apply on reboot — ESPHome
/// can't register new entities with the native API after initial connection.
class NativeNvsDeviceManager : public IDeviceManager, public Component {
 public:
  // ─── Component lifecycle ───

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void dump_config() override;

  // ─── IDeviceManager ───

  void on_rf_packet(const RfPacketInfo &pkt) override;
  HubMode mode() const override { return HubMode::NATIVE_NVS; }
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

  void set_cover_slots(NativeNvsCover *slots, size_t count) {
    cover_slots_ = slots;
    max_covers_ = count;
  }
  void set_light_slots(NativeNvsLight *slots, size_t count) {
    light_slots_ = slots;
    max_lights_ = count;
  }
  void set_remote_slots(EleroRemoteControl *slots, size_t count) {
    remote_slots_ = slots;
    max_remotes_ = count;
  }

 private:
  // ─── Slot management ───

  NativeNvsCover *find_free_cover_slot_();
  NativeNvsLight *find_free_light_slot_();
  EleroRemoteControl *find_free_remote_slot_();
  NativeNvsCover *find_active_cover_(uint32_t addr);
  NativeNvsLight *find_active_light_(uint32_t addr);
  EleroRemoteControl *find_active_remote_(uint32_t addr);

  // ─── Remote tracking ───

  void track_remote_(const RfPacketInfo &pkt);

  // ─── Preference init ───

  void init_slot_preferences_();

  // ─── CRUD notification ───

  void notify_crud_(const char *event, const char *json_str);
  void notify_crud_(const char *event, uint32_t addr, const char *device_type);
  void notify_crud_upserted_(const NvsDeviceConfig &config);

  // ─── Members ───

  Elero *hub_{nullptr};
  bool setup_done_{false};

  NativeNvsCover *cover_slots_{nullptr};
  size_t max_covers_{0};
  NativeNvsLight *light_slots_{nullptr};
  size_t max_lights_{0};
  EleroRemoteControl *remote_slots_{nullptr};
  size_t max_remotes_{0};

  CrudEventCallback crud_callback_{};
};

}  // namespace elero
}  // namespace esphome
