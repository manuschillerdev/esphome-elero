#pragma once

#include "../elero/nvs_device_manager_base.h"
#include "mqtt_publisher.h"
#include "esphome_mqtt_adapter.h"
#include <string>

namespace esphome {
namespace elero {

/// MQTT device manager: extends NvsDeviceManagerBase with MQTT HA discovery and state publishing.
class MqttDeviceManager : public NvsDeviceManagerBase {
 public:
  // ─── IDeviceManager ───

  HubMode mode() const override { return HubMode::MQTT; }

  // ─── Additional config dump ───

  void dump_config() override;

  // ─── Configuration setters (from codegen) ───

  void set_topic_prefix(const std::string &prefix) { topic_prefix_ = prefix; }
  void set_discovery_prefix(const std::string &prefix) { discovery_prefix_ = prefix; }
  void set_device_name(const std::string &name) { device_name_ = name; }

 protected:
  // ─── NvsDeviceManagerBase hooks ───

  void on_cover_activated_(EleroDynamicCover *cover) override;
  void on_light_activated_(EleroDynamicLight *light) override;
  void on_remote_activated_(EleroRemoteControl *remote) override;

  void on_cover_deactivating_(EleroDynamicCover *cover) override;
  void on_light_deactivating_(EleroDynamicLight *light) override;
  void on_remote_deactivating_(EleroRemoteControl *remote) override;

  void on_cover_updated_(EleroDynamicCover *cover) override;
  void on_light_updated_(EleroDynamicLight *light) override;
  void on_remote_updated_(EleroRemoteControl *remote) override;

  EleroDynamicCover::StateCallback make_cover_state_callback_() override;
  EleroDynamicLight::StateCallback make_light_state_callback_() override;
  EleroRemoteControl::StateCallback make_remote_state_callback_() override;

  void loop_hook_() override;
  const char *manager_tag_() const override { return "elero.mqtt"; }

  // ─── HA MQTT discovery component types ───

  static constexpr const char *HA_COVER = "cover";
  static constexpr const char *HA_LIGHT = "light";
  static constexpr const char *HA_SENSOR = "sensor";

 private:
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

  // ─── Members ───

  EspHomeMqttAdapter mqtt_;
  bool mqtt_was_connected_{false};

  std::string topic_prefix_{"elero"};
  std::string discovery_prefix_{"homeassistant"};
  std::string device_name_{"Elero Gateway"};
};

}  // namespace elero
}  // namespace esphome
