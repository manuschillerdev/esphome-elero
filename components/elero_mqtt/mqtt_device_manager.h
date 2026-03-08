#pragma once

#include "../elero/nvs_device_manager_base.h"
#include "mqtt_context.h"
#include "mqtt_cover_handler.h"
#include "mqtt_light_handler.h"
#include "mqtt_remote_handler.h"
#include "esphome_mqtt_adapter.h"
#include <string>
#include <vector>

namespace esphome {
namespace elero {

/// MQTT device manager: extends NvsDeviceManagerBase with MQTT HA discovery and state publishing.
/// All device-type-specific MQTT logic is delegated to handler modules
/// (mqtt_cover_handler, mqtt_light_handler, mqtt_remote_handler).
class MqttDeviceManager : public NvsDeviceManagerBase {
 public:
  // ─── IDeviceManager ───

  HubMode mode() const override { return HubMode::MQTT; }

  // ─── Additional config dump ───

  void dump_config() override;

  // ─── Configuration setters (from codegen) ───

  void set_topic_prefix(const std::string &prefix) { ctx_.topic_prefix = prefix; }
  void set_discovery_prefix(const std::string &prefix) { ctx_.discovery_prefix = prefix; }
  void set_device_name(const std::string &name) { ctx_.device_name = name; }

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

 private:
  void ensure_context_();
  void start_stale_collection_();
  void finish_stale_cleanup_();
  void publish_all_discoveries_();
  void publish_gateway_discovery_();
  void publish_gateway_state_();

  // ─── Finder helpers (passed to handlers for command subscriptions) ───

  mqtt_cover::CoverFinder cover_finder_();
  mqtt_light::LightFinder light_finder_();

  // ─── Members ───

  EspHomeMqttAdapter mqtt_adapter_;
  MqttContext ctx_;
  bool mqtt_was_connected_{false};
  bool context_ready_{false};

  // ─── Stale discovery cleanup ───

  enum class CleanupState : uint8_t { IDLE, COLLECTING, DONE };
  CleanupState cleanup_state_{CleanupState::IDLE};
  uint32_t collect_start_ms_{0};
  std::vector<std::string> collected_topics_;
};

}  // namespace elero
}  // namespace esphome
