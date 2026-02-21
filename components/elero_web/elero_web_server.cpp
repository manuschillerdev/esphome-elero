#include "elero_web_server.h"
#include "elero_web_ui.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstdio>

namespace esphome {
namespace elero {

static const char *const TAG = "elero.web_server";

void EleroWebServer::add_cors_headers(AsyncWebServerResponse *response) {
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  response->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

void EleroWebServer::send_json_error(AsyncWebServerRequest *request, int code, const char *message) {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", message);
  AsyncWebServerResponse *response = request->beginResponse(code, "application/json", buf);
  this->add_cors_headers(response);
  request->send(response);
}

void EleroWebServer::handle_options(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(204);
  this->add_cors_headers(response);
  response->addHeader("Access-Control-Max-Age", "86400");
  request->send(response);
}

void EleroWebServer::setup() {
  if (this->base_ == nullptr) {
    ESP_LOGE(TAG, "web_server_base not set, cannot start Elero Web UI");
    this->mark_failed();
    return;
  }
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "Elero parent not set, cannot start Elero Web UI");
    this->mark_failed();
    return;
  }

  this->base_->init();

  auto *server = this->base_->get_server();
  if (server == nullptr) {
    ESP_LOGE(TAG, "Failed to get web server instance");
    this->mark_failed();
    return;
  }

  // --- HTML UI ---
  server->on("/elero", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_index(request);
  });

  // --- Scan API ---
  server->on("/elero/api/scan/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handle_scan_start(request);
  });
  server->on("/elero/api/scan/start", HTTP_OPTIONS, [this](AsyncWebServerRequest *request) {
    this->handle_options(request);
  });

  server->on("/elero/api/scan/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handle_scan_stop(request);
  });
  server->on("/elero/api/scan/stop", HTTP_OPTIONS, [this](AsyncWebServerRequest *request) {
    this->handle_options(request);
  });

  // --- Data API ---
  server->on("/elero/api/discovered", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_get_discovered(request);
  });
  server->on("/elero/api/discovered", HTTP_OPTIONS, [this](AsyncWebServerRequest *request) {
    this->handle_options(request);
  });

  server->on("/elero/api/configured", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_get_configured(request);
  });
  server->on("/elero/api/configured", HTTP_OPTIONS, [this](AsyncWebServerRequest *request) {
    this->handle_options(request);
  });

  server->on("/elero/api/yaml", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_get_yaml(request);
  });
  server->on("/elero/api/yaml", HTTP_OPTIONS, [this](AsyncWebServerRequest *request) {
    this->handle_options(request);
  });

  ESP_LOGI(TAG, "Elero Web UI available at /elero");
}

void EleroWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero Web Server:");
  ESP_LOGCONFIG(TAG, "  URL: /elero");
  ESP_LOGCONFIG(TAG, "  API: /elero/api/*");
}

void EleroWebServer::handle_index(AsyncWebServerRequest *request) {
  request->send_P(200, "text/html", ELERO_WEB_UI_HTML);
}

void EleroWebServer::handle_scan_start(AsyncWebServerRequest *request) {
  if (this->parent_->is_scanning()) {
    this->send_json_error(request, 409, "Scan already running");
    return;
  }
  this->parent_->clear_discovered();
  this->parent_->start_scan();
  AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", "{\"status\":\"scanning\"}");
  this->add_cors_headers(response);
  request->send(response);
}

void EleroWebServer::handle_scan_stop(AsyncWebServerRequest *request) {
  if (!this->parent_->is_scanning()) {
    this->send_json_error(request, 409, "No scan running");
    return;
  }
  this->parent_->stop_scan();
  AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", "{\"status\":\"stopped\"}");
  this->add_cors_headers(response);
  request->send(response);
}

void EleroWebServer::handle_get_discovered(AsyncWebServerRequest *request) {
  std::string json = "{\"scanning\":";
  json += this->parent_->is_scanning() ? "true" : "false";
  json += ",\"blinds\":[";

  const auto &blinds = this->parent_->get_discovered_blinds();
  bool first = true;
  for (const auto &blind : blinds) {
    if (!first) json += ",";
    first = false;

    char buf[512];
    snprintf(buf, sizeof(buf),
      "{\"blind_address\":\"0x%06x\","
      "\"remote_address\":\"0x%06x\","
      "\"channel\":%d,"
      "\"rssi\":%.1f,"
      "\"last_state\":\"%s\","
      "\"times_seen\":%d,"
      "\"hop\":\"0x%02x\","
      "\"payload_1\":\"0x%02x\","
      "\"payload_2\":\"0x%02x\","
      "\"pck_inf1\":\"0x%02x\","
      "\"pck_inf2\":\"0x%02x\","
      "\"already_configured\":%s}",
      blind.blind_address,
      blind.remote_address,
      blind.channel,
      blind.rssi,
      elero_state_to_string(blind.last_state),
      blind.times_seen,
      blind.hop,
      blind.payload_1,
      blind.payload_2,
      blind.pck_inf[0],
      blind.pck_inf[1],
      this->parent_->is_cover_configured(blind.blind_address) ? "true" : "false"
    );
    json += buf;
  }

  json += "]}";
  AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", json.c_str());
  this->add_cors_headers(response);
  request->send(response);
}

void EleroWebServer::handle_get_configured(AsyncWebServerRequest *request) {
  std::string json = "{\"covers\":[";

  const auto &covers = this->parent_->get_configured_covers();
  bool first = true;
  for (const auto &pair : covers) {
    if (!first) json += ",";
    first = false;

    auto *blind = pair.second;
    char buf[256];
    snprintf(buf, sizeof(buf),
      "{\"blind_address\":\"0x%06x\","
      "\"name\":\"%s\","
      "\"position\":%.2f,"
      "\"operation\":\"%s\"}",
      pair.first,
      blind->get_blind_name().c_str(),
      blind->get_cover_position(),
      blind->get_operation_str()
    );
    json += buf;
  }

  json += "]}";
  AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", json.c_str());
  this->add_cors_headers(response);
  request->send(response);
}

void EleroWebServer::handle_get_yaml(AsyncWebServerRequest *request) {
  const auto &blinds = this->parent_->get_discovered_blinds();
  if (blinds.empty()) {
    AsyncWebServerResponse *response = request->beginResponse(
        200, "text/plain; charset=utf-8",
        "# No discovered blinds.\n# Start a scan and press buttons on your remote.\n");
    this->add_cors_headers(response);
    request->send(response);
    return;
  }

  std::string yaml = "# Auto-generated YAML from Elero RF discovery\n";
  yaml += "# Copy this into your ESPHome configuration.\n\n";
  yaml += "cover:\n";

  int idx = 0;
  for (const auto &blind : blinds) {
    if (this->parent_->is_cover_configured(blind.blind_address))
      continue;

    char buf[512];
    snprintf(buf, sizeof(buf),
      "  - platform: elero\n"
      "    blind_address: 0x%06x\n"
      "    channel: %d\n"
      "    remote_address: 0x%06x\n"
      "    name: \"Discovered Blind %d\"\n"
      "    # open_duration: 25s\n"
      "    # close_duration: 22s\n"
      "    # Detected RF parameters:\n"
      "    hop: 0x%02x\n"
      "    payload_1: 0x%02x\n"
      "    payload_2: 0x%02x\n"
      "    pck_inf1: 0x%02x\n"
      "    pck_inf2: 0x%02x\n"
      "\n",
      blind.blind_address,
      blind.channel,
      blind.remote_address,
      ++idx,
      blind.hop,
      blind.payload_1,
      blind.payload_2,
      blind.pck_inf[0],
      blind.pck_inf[1]
    );
    yaml += buf;
  }

  if (idx == 0) {
    yaml += "  # All discovered blinds are already configured.\n";
  }

  AsyncWebServerResponse *response =
      request->beginResponse(200, "text/plain; charset=utf-8", yaml.c_str());
  this->add_cors_headers(response);
  request->send(response);
}

}  // namespace elero
}  // namespace esphome
