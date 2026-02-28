#pragma once

// Include Arduino first to avoid INADDR_NONE macro conflict with mongoose/lwip
#ifdef USE_ARDUINO
#include <Arduino.h>
#endif

#include "esphome/core/component.h"
#ifdef USE_LOGGER
#include "esphome/components/logger/logger.h"
#endif
#include "mongoose.h"
#include "../elero/elero.h"
#include <string>
#include <vector>

namespace esphome {
namespace elero {

/// Simplified WebSocket server - acts as RF bridge
/// Server → Client: config (on connect), rf (packets), log (ESPHome logs)
/// Client → Server: cmd (blind commands)
#ifdef USE_LOGGER
class EleroWebServer : public Component, public logger::LogListener {
#else
class EleroWebServer : public Component {
#endif
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

#ifdef USE_LOGGER
  // LogListener interface (ESPHome 2025.12.0+)
  void on_log(uint8_t level, const char *tag, const char *message, size_t message_len) override;
#endif

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

  // WebSocket helpers
  void ws_send(struct mg_connection *c, const char *event, const std::string &data);
  void ws_broadcast(const char *event, const std::string &data);
  void ws_cleanup();

  // JSON builders
  std::string build_config_json();
  std::string build_rf_json(const RfPacketInfo &pkt);
};

}  // namespace elero
}  // namespace esphome
