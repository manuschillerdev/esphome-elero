#pragma once

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "../elero/elero.h"
#include <ESPAsyncWebServer.h>

namespace esphome {
namespace elero {

// Maximum WebSocket clients (ESPAsyncWebServer default)
constexpr uint8_t ELERO_WS_MAX_CLIENTS = 4;

// Broadcast intervals (ms)
constexpr uint32_t ELERO_WS_HEARTBEAT_INTERVAL = 5000;
constexpr uint32_t ELERO_WS_CLEANUP_INTERVAL = 10000;

class EleroWebServer : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

  void set_elero_parent(Elero *parent) { this->parent_ = parent; }
  void set_web_server(web_server_base::WebServerBase *base) { this->base_ = base; }

  // Enable / disable the web UI (used by the HA switch)
  void set_enabled(bool en) { this->enabled_ = en; }
  bool is_enabled() const { return this->enabled_; }

  // State change notifications (call from hub when state changes)
  void notify_covers_changed();
  void notify_discovered_changed();
  void notify_log_entry();
  void notify_packet_received();
  void notify_scan_status_changed();

 protected:
  // WebSocket event handler
  void on_ws_event(AsyncWebSocket *server, AsyncWebSocketClient *client,
                   AwsEventType type, void *arg, uint8_t *data, size_t len);

  // Message dispatcher
  void handle_ws_message(AsyncWebSocketClient *client, const std::string &msg);

  // Broadcast helpers
  void broadcast(const char *type, const std::string &json);
  void broadcast_full_state();
  void send_full_state(AsyncWebSocketClient *client);
  void send_result(AsyncWebSocketClient *client, const char *id, bool ok, const char *error = nullptr);
  void send_yaml(AsyncWebSocketClient *client, const char *id);

  // JSON builders
  std::string build_full_state_json();
  std::string build_covers_json();
  std::string build_discovered_json();
  std::string build_logs_json(uint32_t since_ms = 0, size_t max_entries = 20);
  std::string build_packets_json();
  std::string build_yaml();

  // HTTP handlers (minimal - just index and redirect)
  void handle_index(AsyncWebServerRequest *request);

  Elero *parent_{nullptr};
  web_server_base::WebServerBase *base_{nullptr};
  AsyncWebSocket *ws_{nullptr};
  bool enabled_{true};

  // Timing for heartbeat and cleanup
  uint32_t last_heartbeat_ms_{0};
  uint32_t last_cleanup_ms_{0};

  // Change tracking for incremental pushes
  bool covers_changed_{false};
  bool discovered_changed_{false};
  bool logs_changed_{false};
  bool packets_changed_{false};
  bool scan_status_changed_{false};
  uint32_t last_log_push_ts_{0};
};

}  // namespace elero
}  // namespace esphome
