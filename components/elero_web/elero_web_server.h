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

  bool canHandle(AsyncWebServerRequest *request) override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
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

  void add_cors_headers(AsyncWebServerResponse *response);
  void send_json_error(AsyncWebServerRequest *request, int code, const char *message);
  void handle_options(AsyncWebServerRequest *request);

  Elero *parent_{nullptr};
  web_server_base::WebServerBase *base_{nullptr};
};

}  // namespace elero
}  // namespace esphome
