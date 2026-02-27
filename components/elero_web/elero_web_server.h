#pragma once

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "../elero/elero.h"
#include <ESPAsyncWebServer.h>

namespace esphome {
namespace elero {

// SSE heartbeat interval (ms)
constexpr uint32_t ELERO_SSE_HEARTBEAT_INTERVAL = 15000;

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
  // SSE broadcast helper
  void broadcast_event(const char *event_type, const std::string &json);

  // HTTP response helpers
  void send_json_ok(AsyncWebServerRequest *request);
  void send_json_error(AsyncWebServerRequest *request, int code, const char *error);

  // HTTP GET handlers
  void handle_index(AsyncWebServerRequest *request);
  void handle_get_state(AsyncWebServerRequest *request);
  void handle_get_yaml(AsyncWebServerRequest *request);

  // HTTP POST handlers (no body)
  void handle_post_scan_start(AsyncWebServerRequest *request);
  void handle_post_scan_stop(AsyncWebServerRequest *request);
  void handle_post_log_start(AsyncWebServerRequest *request);
  void handle_post_log_stop(AsyncWebServerRequest *request);
  void handle_post_log_clear(AsyncWebServerRequest *request);
  void handle_post_dump_start(AsyncWebServerRequest *request);
  void handle_post_dump_stop(AsyncWebServerRequest *request);
  void handle_post_dump_clear(AsyncWebServerRequest *request);

  // HTTP POST handlers (with JSON body) - parse body in callback
  void handle_post_cover(AsyncWebServerRequest *request, uint8_t *data, size_t len);
  void handle_post_settings(AsyncWebServerRequest *request, uint8_t *data, size_t len);
  void handle_post_adopt(AsyncWebServerRequest *request, uint8_t *data, size_t len);
  void handle_post_runtime_remove(AsyncWebServerRequest *request, uint8_t *data, size_t len);
  void handle_post_frequency(AsyncWebServerRequest *request, uint8_t *data, size_t len);

  // JSON builders
  std::string build_full_state_json();
  std::string build_covers_json();
  std::string build_discovered_json();
  std::string build_logs_json(uint32_t since_ms = 0, size_t max_entries = 20);
  std::string build_packets_json();
  std::string build_yaml();

  Elero *parent_{nullptr};
  web_server_base::WebServerBase *base_{nullptr};
  AsyncEventSource events_{"/elero/events"};
  bool enabled_{true};

  // Timing for heartbeat
  uint32_t last_heartbeat_ms_{0};

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
