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

  // Device CRUD handlers (MQTT mode)
  void handle_upsert_device_(struct mg_connection *c, JsonObject root);
  void handle_remove_device_(struct mg_connection *c, JsonObject root);

  /// Dispatch a command byte to a known device with proper FSM + follow-ups.
  /// This is the single low-level primitive — cmd handler calls into this.
  void dispatch_device_command_(Device &dev, uint8_t cmd_byte);

  // Parse NvsDeviceConfig from JSON object
  bool parse_device_config_(JsonObject root, NvsDeviceConfig &config, std::string &error);
};

}  // namespace elero
}  // namespace esphome
