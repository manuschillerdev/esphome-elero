#pragma once

// Include Arduino first to avoid INADDR_NONE macro conflict with mongoose/lwip
#ifdef USE_ARDUINO
#include <Arduino.h>
#endif

#include "esphome/core/component.h"
#include "esphome/components/logger/logger.h"
#include "mongoose.h"
#include "../elero/elero.h"
#include "../elero/device.h"
#include "../elero/device_registry.h"
#include "../elero/output_adapter.h"
#include "../elero/state_snapshot.h"
#include "esphome/components/json/json_util.h"
#include <string>
#include <vector>

namespace esphome {
namespace elero {

/// WebSocket server - acts as RF bridge, log forwarder, and CRUD proxy
/// Server → Client: config (on connect), rf (packets), log (ESPHome logs), crud events
/// Client → Server: cmd (blind commands), raw (raw RF packets), upsert_device, remove_device
class EleroWebServer : public Component, public OutputAdapter, public logger::LogListener {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

  void set_elero_parent(Elero *parent) { this->parent_ = parent; }
  void set_port(uint16_t port) { this->port_ = port; }

  // Enable/disable web UI (used by HA switch)
  void set_enabled(bool en) { this->enabled_ = en; }
  bool is_enabled() const { return this->enabled_; }

  // ── OutputAdapter interface ──────────────────────────────
  // Component::setup()/loop() satisfy OutputAdapter::loop() (same signature).
  // OutputAdapter::setup(DeviceRegistry&) is a separate overload.
  void setup(DeviceRegistry &registry) override { registry_ = &registry; }
  void on_device_added(const Device &dev) override;
  void on_device_removed(const Device &dev) override;
  void on_state_changed(const Device &dev, uint16_t changes) override;
  void on_config_changed(const Device &dev) override;
  void on_rf_packet(const RfPacketInfo &pkt) override;
  void on_group_upserted(const NvsGroupConfig &group) override;
  void on_group_removed(const char *id) override;

  // LogListener interface - forward logs to WebSocket clients
  void on_log(uint8_t level, const char *tag, const char *message, size_t message_len) override;

 protected:
  Elero *parent_{nullptr};
  DeviceRegistry *registry_{nullptr};
  uint16_t port_{80};
  bool enabled_{true};

  // Mongoose state
  struct mg_mgr mgr_;
  struct mg_connection *listener_{nullptr};
  std::vector<struct mg_connection *> ws_clients_;

  // Mongoose event handler (static for C callback)
  static void event_handler(struct mg_connection *c, int ev, void *ev_data);

  // HTTP route handlers
  void handle_index(struct mg_connection *c);

  // WebSocket handlers
  void handle_ws_upgrade(struct mg_connection *c, struct mg_http_message *hm);
  void handle_ws_message(struct mg_connection *c, struct mg_ws_message *wm);

  // WebSocket helpers
  void ws_send(struct mg_connection *c, const char *event, const std::string &data);
  void ws_broadcast(const char *event, const std::string &data);
  void ws_cleanup();

  // JSON builders
  std::string build_config_json();
  std::string build_rf_json(const RfPacketInfo &pkt);
  std::string build_device_upserted_json_(const Device &dev);
  std::string build_group_json_(const NvsGroupConfig &group) const;
  std::string build_learn_in_state_json_() const;

  // Device CRUD handlers (MQTT mode)
  void handle_upsert_device_(struct mg_connection *c, JsonObject root);
  void handle_remove_device_(struct mg_connection *c, JsonObject root);
  void handle_upsert_group_(struct mg_connection *c, JsonObject root);
  void handle_remove_group_(struct mg_connection *c, JsonObject root);
  void handle_group_command_(struct mg_connection *c, JsonObject root);

  // Hub config handler (persisted display name override)
  void handle_set_hub_config_(struct mg_connection *c, JsonObject root);

  // Backup / restore handlers
  void handle_export_config_(struct mg_connection *c, JsonObject root);
  void handle_import_config_(struct mg_connection *c, JsonObject root);

  // Learn-in handlers
  void handle_learn_in_start_(struct mg_connection *c, JsonObject root);
  void handle_learn_in_confirm_up_(struct mg_connection *c);
  void handle_learn_in_confirm_down_(struct mg_connection *c);
  void handle_learn_in_cancel_(struct mg_connection *c);

  /// Serialize one device's NvsDeviceConfig into a `DeviceSnapshot` JSON object.
  void build_device_snapshot_(const NvsDeviceConfig &cfg, JsonObject out);
  /// Build the full `ConfigSnapshot` envelope for the current registry state.
  std::string build_config_snapshot_json_();

  /// Dispatch a command byte to a known device with proper FSM + follow-ups.
  /// This is the single low-level primitive — cmd handler calls into this.
  void dispatch_device_command_(Device &dev, uint8_t cmd_byte);

  // Parse NvsDeviceConfig / NvsGroupConfig from JSON objects
  bool parse_device_config_(JsonObject root, NvsDeviceConfig &config, std::string &error);
  bool parse_group_config_(JsonObject root, NvsGroupConfig &config, std::string &error);

  LearnInState last_learn_in_state_{LearnInState::IDLE};
  bool last_learn_in_active_{false};
  bool last_learn_in_busy_{false};
  uint32_t last_learn_in_src_{0};
  uint8_t last_learn_in_channel_{0};
  uint8_t last_learn_in_cmd_{packet::command::INVALID};
};

}  // namespace elero
}  // namespace esphome
