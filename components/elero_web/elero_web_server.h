#pragma once

// Include Arduino first to avoid INADDR_NONE macro conflict with mongoose/lwip
#ifdef USE_ARDUINO
#include <Arduino.h>
#endif

#include "esphome/core/component.h"
#include "esphome/components/logger/logger.h"
#include "mongoose.h"
#include "../elero/elero.h"
#include "../elero/device_manager.h"
#include <string>
#include <vector>
#include <functional>

namespace esphome {
namespace elero {

/// Simplified WebSocket server - acts as RF bridge
///
/// Operates in two modes based on hub configuration:
/// - **Native mode**: RF discovery only, no device CRUD
/// - **MQTT mode**: Full device CRUD via device manager
///
/// Server → Client events:
/// - `config`: On connect, sends device info, mode, and configured devices
/// - `rf`: Every decoded RF packet
/// - `device_saved`: When a device is added/updated (MQTT mode)
/// - `device_removed`: When a device is removed (MQTT mode)
///
/// Client → Server messages:
/// - `cmd`: Send command to blind/light
/// - `save_device`: Add/update a device (MQTT mode only)
/// - `remove_device`: Remove a device (MQTT mode only)
///
/// Note: Log forwarding requires ESPHome 2025.12.0+ with LogListener support
class EleroWebServer : public Component {
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

  // Called from hub when RF packet is received
  void on_rf_packet(const RfPacketInfo &pkt);

 protected:
  Elero *parent_{nullptr};
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

  // Config management handlers (delegate to device manager)
  void handle_save_device(struct mg_connection *c, const std::string &msg);
  void handle_remove_device(struct mg_connection *c, const std::string &msg);

  // WebSocket helpers
  void ws_send(struct mg_connection *c, const char *event, const std::string &data);
  void ws_broadcast(const char *event, const std::string &data);
  void ws_cleanup();

  // JSON builders
  std::string build_config_json();
  std::string build_rf_json(const RfPacketInfo &pkt);

  // Mode helpers
  [[nodiscard]] bool supports_crud() const {
    auto *mgr = parent_ ? parent_->get_device_manager() : nullptr;
    return mgr && mgr->supports_crud();
  }

  [[nodiscard]] EleroMode get_mode() const {
    return parent_ ? parent_->get_mode() : EleroMode::NATIVE;
  }
};

}  // namespace elero
}  // namespace esphome
