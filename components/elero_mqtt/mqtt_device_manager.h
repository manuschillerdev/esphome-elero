#pragma once

#include "../elero/device_manager.h"
#include "../elero/nvs_config.h"
#include "../elero/elero.h"
#include "../elero/EleroDynamicCover.h"
#include "../elero/EleroDynamicLight.h"
#include "../elero/EleroRemoteControl.h"
#include "mqtt_publisher.h"
#include "esphome_mqtt_adapter.h"
#include "esphome/core/preferences.h"
#include "esphome/core/helpers.h"
#include <string>

namespace esphome {
namespace elero {

/// MQTT device manager: NVS persistence + MQTT HA discovery + dynamic entities.
class MqttDeviceManager : public IDeviceManager, public Component {
 public:
  // ─── Component lifecycle ───

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void dump_config() override;

  // ─── IDeviceManager ───

  void on_rf_packet(const RfPacketInfo &pkt) override;
  HubMode mode() const override { return HubMode::MQTT; }
  bool supports_crud() const override { return true; }

  // ─── Device CRUD ───

  [[nodiscard]] bool add_device(const NvsDeviceConfig &config) override;
  [[nodiscard]] bool remove_device(DeviceType type, uint32_t dst_address) override;
  [[nodiscard]] bool update_device(const NvsDeviceConfig &config) override;
  [[nodiscard]] bool set_device_enabled(DeviceType type, uint32_t dst_address, bool enabled) override;

  // ─── CRUD event callback (for WS broadcast) ───

  void set_crud_callback(CrudEventCallback cb) override { crud_callback_ = std::move(cb); }

  // ─── Configuration setters (from codegen) ───

  void set_hub(Elero *hub) { hub_ = hub; }
  void set_topic_prefix(const std::string &prefix) { topic_prefix_ = prefix; }
  void set_discovery_prefix(const std::string &prefix) { discovery_prefix_ = prefix; }
  void set_device_name(const std::string &name) { device_name_ = name; }

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

 private:
  // ─── Slot management ───

  EleroDynamicCover *find_free_cover_slot_();
  EleroDynamicLight *find_free_light_slot_();
  EleroRemoteControl *find_free_remote_slot_();
  EleroDynamicCover *find_active_cover_(uint32_t addr);
  EleroDynamicLight *find_active_light_(uint32_t addr);
  EleroRemoteControl *find_active_remote_(uint32_t addr);

  // ─── MQTT publishing ───

  void publish_cover_discovery_(EleroDynamicCover *cover);
  void publish_light_discovery_(EleroDynamicLight *light);
  void publish_remote_discovery_(EleroRemoteControl *remote);
  void remove_discovery_(const char *component, uint32_t addr);

  void publish_cover_state_(EleroDynamicCover *cover);
  void publish_light_state_(EleroDynamicLight *light);
  void publish_remote_state_(EleroRemoteControl *remote);

  void subscribe_cover_commands_(EleroDynamicCover *cover);
  void subscribe_light_commands_(EleroDynamicLight *light);

  void publish_all_discoveries_();
  std::string device_id_() const;
  std::string addr_hex_(uint32_t addr) const;

  // ─── Remote control tracking ───

  void track_remote_(const RfPacketInfo &pkt);

  void init_slot_preferences_();

  // ─── CRUD event notification ───

  void notify_crud_(const char *event, const char *json);

  // ─── Members ───

  Elero *hub_{nullptr};
  EspHomeMqttAdapter mqtt_;
  bool mqtt_was_connected_{false};

  std::string topic_prefix_{"elero"};
  std::string discovery_prefix_{"homeassistant"};
  std::string device_name_{"Elero Gateway"};

  EleroDynamicCover *cover_slots_{nullptr};
  size_t max_covers_{0};
  EleroDynamicLight *light_slots_{nullptr};
  size_t max_lights_{0};
  EleroRemoteControl *remote_slots_{nullptr};
  size_t max_remotes_{0};

  // CRUD event callback (optional, set by web server)
  CrudEventCallback crud_callback_{};
};

}  // namespace elero
}  // namespace esphome
