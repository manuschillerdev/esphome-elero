#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "sse_server.h"
#include "../elero/elero.h"

#ifdef USE_ARDUINO
#include <ESPAsyncWebServer.h>
#endif

#ifdef USE_ESP_IDF
#include <esp_http_server.h>
#endif

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
  // SSE broadcast helper (delegates to sse_)
  void broadcast_event(const char *event_type, const std::string &json);

  // JSON builders
  std::string build_full_state_json();
  std::string build_covers_json();
  std::string build_discovered_json();
  std::string build_logs_json(uint32_t since_ms = 0, size_t max_entries = 20);
  std::string build_packets_json();
  std::string build_yaml();

  Elero *parent_{nullptr};
  web_server_base::WebServerBase *base_{nullptr};
  bool enabled_{true};

  // SSE server abstraction (handles Arduino/IDF differences internally)
  SSEServer sse_;

  // Timing for heartbeat
  uint32_t last_heartbeat_ms_{0};

  // Change tracking for incremental pushes
  bool covers_changed_{false};
  bool discovered_changed_{false};
  bool logs_changed_{false};
  bool packets_changed_{false};
  bool scan_status_changed_{false};
  uint32_t last_log_push_ts_{0};

#ifdef USE_ARDUINO
  // HTTP response helpers (Arduino)
  void send_json_ok(AsyncWebServerRequest *request);
  void send_json_error(AsyncWebServerRequest *request, int code, const char *error);

  // HTTP GET handlers (Arduino)
  void handle_index(AsyncWebServerRequest *request);
  void handle_get_state(AsyncWebServerRequest *request);
  void handle_get_yaml(AsyncWebServerRequest *request);

  // HTTP POST handlers (no body) (Arduino)
  void handle_post_scan_start(AsyncWebServerRequest *request);
  void handle_post_scan_stop(AsyncWebServerRequest *request);
  void handle_post_log_start(AsyncWebServerRequest *request);
  void handle_post_log_stop(AsyncWebServerRequest *request);
  void handle_post_log_clear(AsyncWebServerRequest *request);
  void handle_post_dump_start(AsyncWebServerRequest *request);
  void handle_post_dump_stop(AsyncWebServerRequest *request);
  void handle_post_dump_clear(AsyncWebServerRequest *request);

  // HTTP POST handlers (with JSON body) (Arduino)
  void handle_post_cover(AsyncWebServerRequest *request, uint8_t *data, size_t len);
  void handle_post_settings(AsyncWebServerRequest *request, uint8_t *data, size_t len);
  void handle_post_adopt(AsyncWebServerRequest *request, uint8_t *data, size_t len);
  void handle_post_runtime_remove(AsyncWebServerRequest *request, uint8_t *data, size_t len);
  void handle_post_frequency(AsyncWebServerRequest *request, uint8_t *data, size_t len);
#endif

#ifdef USE_ESP_IDF
  // IDF HTTP handlers (static for esp_http_server)
  static esp_err_t handle_index_idf_(httpd_req_t *req);
  static esp_err_t handle_get_state_idf_(httpd_req_t *req);
  static esp_err_t handle_get_yaml_idf_(httpd_req_t *req);
  static esp_err_t handle_post_scan_start_idf_(httpd_req_t *req);
  static esp_err_t handle_post_scan_stop_idf_(httpd_req_t *req);
  static esp_err_t handle_post_log_start_idf_(httpd_req_t *req);
  static esp_err_t handle_post_log_stop_idf_(httpd_req_t *req);
  static esp_err_t handle_post_log_clear_idf_(httpd_req_t *req);
  static esp_err_t handle_post_dump_start_idf_(httpd_req_t *req);
  static esp_err_t handle_post_dump_stop_idf_(httpd_req_t *req);
  static esp_err_t handle_post_dump_clear_idf_(httpd_req_t *req);
  static esp_err_t handle_post_cover_idf_(httpd_req_t *req);
  static esp_err_t handle_post_settings_idf_(httpd_req_t *req);
  static esp_err_t handle_post_adopt_idf_(httpd_req_t *req);
  static esp_err_t handle_post_runtime_remove_idf_(httpd_req_t *req);
  static esp_err_t handle_post_frequency_idf_(httpd_req_t *req);

  // IDF helper to send JSON responses
  static esp_err_t send_json_ok_idf_(httpd_req_t *req);
  static esp_err_t send_json_error_idf_(httpd_req_t *req, int code, const char *error);
#endif
};

}  // namespace elero
}  // namespace esphome
