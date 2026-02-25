#pragma once

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "../elero/elero.h"

namespace esphome {
namespace elero {

class EleroWebServer : public Component, public AsyncWebHandler {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

  void set_elero_parent(Elero *parent) { this->parent_ = parent; }
  void set_web_server(web_server_base::WebServerBase *base) { this->base_ = base; }

  // Enable / disable the web UI (used by the HA switch)
  void set_enabled(bool en) { this->enabled_ = en; }
  bool is_enabled() const { return this->enabled_; }

  // AsyncWebHandler interface - note: signature varies by ESPHome/AsyncWebServer version
  bool canHandle(AsyncWebServerRequest *request) override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  // ── Existing handlers ──────────────────────────────────────────────────
  void handle_index(AsyncWebServerRequest *request);
  void handle_scan_start(AsyncWebServerRequest *request);
  void handle_scan_stop(AsyncWebServerRequest *request);
  void handle_get_discovered(AsyncWebServerRequest *request);
  void handle_get_configured(AsyncWebServerRequest *request);
  void handle_get_yaml(AsyncWebServerRequest *request);
  void handle_packet_dump_start(AsyncWebServerRequest *request);
  void handle_packet_dump_stop(AsyncWebServerRequest *request);
  void handle_get_packets(AsyncWebServerRequest *request);
  void handle_clear_packets(AsyncWebServerRequest *request);
  void handle_get_frequency(AsyncWebServerRequest *request);
  void handle_set_frequency(AsyncWebServerRequest *request);

  // ── New handlers ───────────────────────────────────────────────────────
  // Cover control & settings
  void handle_cover_command(AsyncWebServerRequest *request, uint32_t addr);
  void handle_cover_settings(AsyncWebServerRequest *request, uint32_t addr);
  // Runtime adopted blinds
  void handle_adopt_discovered(AsyncWebServerRequest *request, uint32_t addr);
  void handle_get_runtime(AsyncWebServerRequest *request);
  void handle_runtime_command(AsyncWebServerRequest *request, uint32_t addr);
  void handle_runtime_remove(AsyncWebServerRequest *request, uint32_t addr);
  void handle_runtime_settings(AsyncWebServerRequest *request, uint32_t addr);
  // Logs
  void handle_get_logs(AsyncWebServerRequest *request);
  void handle_clear_logs(AsyncWebServerRequest *request);
  void handle_log_capture_start(AsyncWebServerRequest *request);
  void handle_log_capture_stop(AsyncWebServerRequest *request);
  // Web UI enable/disable (REST mirror of the HA switch)
  void handle_webui_status(AsyncWebServerRequest *request);
  void handle_webui_enable(AsyncWebServerRequest *request);
  // Device info
  void handle_get_info(AsyncWebServerRequest *request);

  // ── URL parsing helpers ────────────────────────────────────────────────
  // Tries to parse /elero/api/covers/0xABCDEF/command → addr = 0xABCDEF, action = "command"
  static bool parse_addr_url(const std::string &url, const char *prefix,
                              uint32_t &addr_out, std::string &action_out);

  // ── Response helpers ───────────────────────────────────────────────────
  void add_cors_headers(AsyncWebServerResponse *response);
  void send_json_error(AsyncWebServerRequest *request, int code, const char *message);
  void handle_options(AsyncWebServerRequest *request);

  Elero *parent_{nullptr};
  web_server_base::WebServerBase *base_{nullptr};
  bool enabled_{true};
};

}  // namespace elero
}  // namespace esphome
