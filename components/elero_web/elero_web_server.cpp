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
  AsyncWebServerResponse *response = request->beginResponse(204, "text/plain", "");
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

  // Register ourselves as the handler for all /elero/* routes.
  // canHandle() filters by URL prefix; handleRequest() does the routing.
  server->addHandler(this);

  ESP_LOGI(TAG, "Elero Web UI available at /elero");
}

void EleroWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero Web Server:");
  ESP_LOGCONFIG(TAG, "  URL: /elero");
  ESP_LOGCONFIG(TAG, "  API: /elero/api/*");
}

bool EleroWebServer::canHandle(AsyncWebServerRequest *request) {
  const std::string &url = request->url();
  return url.size() >= 6 && url.substr(0, 6) == "/elero";
}

void EleroWebServer::handleRequest(AsyncWebServerRequest *request) {
  const std::string url = request->url();
  const auto method = request->method();

  if (url == "/elero" && method == HTTP_GET) {
    handle_index(request);
  } else if (url == "/elero/api/scan/start") {
    if (method == HTTP_POST) handle_scan_start(request);
    else if (method == HTTP_OPTIONS) handle_options(request);
  } else if (url == "/elero/api/scan/stop") {
    if (method == HTTP_POST) handle_scan_stop(request);
    else if (method == HTTP_OPTIONS) handle_options(request);
  } else if (url == "/elero/api/discovered") {
    if (method == HTTP_GET) handle_get_discovered(request);
    else if (method == HTTP_OPTIONS) handle_options(request);
  } else if (url == "/elero/api/configured") {
    if (method == HTTP_GET) handle_get_configured(request);
    else if (method == HTTP_OPTIONS) handle_options(request);
  } else if (url == "/elero/api/yaml") {
    if (method == HTTP_GET) handle_get_yaml(request);
    else if (method == HTTP_OPTIONS) handle_options(request);
  } else if (url == "/elero/api/dump/start") {
    if (method == HTTP_POST) handle_packet_dump_start(request);
    else if (method == HTTP_OPTIONS) handle_options(request);
  } else if (url == "/elero/api/dump/stop") {
    if (method == HTTP_POST) handle_packet_dump_stop(request);
    else if (method == HTTP_OPTIONS) handle_options(request);
  } else if (url == "/elero/api/packets") {
    if (method == HTTP_GET) handle_get_packets(request);
    else if (method == HTTP_OPTIONS) handle_options(request);
  } else if (url == "/elero/api/packets/clear") {
    if (method == HTTP_POST) handle_clear_packets(request);
    else if (method == HTTP_OPTIONS) handle_options(request);
  } else if (url == "/elero/api/frequency") {
    if (method == HTTP_GET) handle_get_frequency(request);
    else if (method == HTTP_OPTIONS) handle_options(request);
  } else if (url == "/elero/api/frequency/set") {
    if (method == HTTP_POST) handle_set_frequency(request);
    else if (method == HTTP_OPTIONS) handle_options(request);
  } else {
    request->send(404, "text/plain", "Not Found");
  }
}

void EleroWebServer::handle_index(AsyncWebServerRequest *request) {
  request->send(200, "text/html", ELERO_WEB_UI_HTML);
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

void EleroWebServer::handle_packet_dump_start(AsyncWebServerRequest *request) {
  if (this->parent_->is_packet_dump_active()) {
    this->send_json_error(request, 409, "Packet dump already running");
    return;
  }
  this->parent_->clear_raw_packets();
  this->parent_->start_packet_dump();
  AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", "{\"status\":\"dumping\"}");
  this->add_cors_headers(response);
  request->send(response);
}

void EleroWebServer::handle_packet_dump_stop(AsyncWebServerRequest *request) {
  if (!this->parent_->is_packet_dump_active()) {
    this->send_json_error(request, 409, "No packet dump running");
    return;
  }
  this->parent_->stop_packet_dump();
  AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", "{\"status\":\"stopped\"}");
  this->add_cors_headers(response);
  request->send(response);
}

void EleroWebServer::handle_get_packets(AsyncWebServerRequest *request) {
  const auto &packets = this->parent_->get_raw_packets();

  std::string json = "{\"dump_active\":";
  json += this->parent_->is_packet_dump_active() ? "true" : "false";
  json += ",\"count\":";
  char cnt_buf[12];
  snprintf(cnt_buf, sizeof(cnt_buf), "%d", (int)packets.size());
  json += cnt_buf;
  json += ",\"packets\":[";

  bool first = true;
  for (const auto &pkt : packets) {
    if (!first) json += ",";
    first = false;

    // Build hex string: "xx xx xx ..."
    char hex_buf[CC1101_FIFO_LENGTH * 3 + 1];
    hex_buf[0] = '\0';
    for (int i = 0; i < pkt.fifo_len && i < CC1101_FIFO_LENGTH; i++) {
      char byte_buf[4];
      snprintf(byte_buf, sizeof(byte_buf), i == 0 ? "%02x" : " %02x", pkt.data[i]);
      strncat(hex_buf, byte_buf, sizeof(hex_buf) - strlen(hex_buf) - 1);
    }

    char entry_buf[320];
    snprintf(entry_buf, sizeof(entry_buf),
      "{\"t\":%lu,\"len\":%d,\"valid\":%s,\"reason\":\"%s\",\"hex\":\"%s\"}",
      (unsigned long)pkt.timestamp_ms,
      pkt.fifo_len,
      pkt.valid ? "true" : "false",
      pkt.reject_reason,
      hex_buf
    );
    json += entry_buf;
  }

  json += "]}";
  AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", json.c_str());
  this->add_cors_headers(response);
  request->send(response);
}

void EleroWebServer::handle_clear_packets(AsyncWebServerRequest *request) {
  this->parent_->clear_raw_packets();
  AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", "{\"status\":\"cleared\"}");
  this->add_cors_headers(response);
  request->send(response);
}

void EleroWebServer::handle_get_frequency(AsyncWebServerRequest *request) {
  char buf[80];
  snprintf(buf, sizeof(buf),
    "{\"freq2\":\"0x%02x\",\"freq1\":\"0x%02x\",\"freq0\":\"0x%02x\"}",
    this->parent_->get_freq2(),
    this->parent_->get_freq1(),
    this->parent_->get_freq0());
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", buf);
  this->add_cors_headers(response);
  request->send(response);
}

void EleroWebServer::handle_set_frequency(AsyncWebServerRequest *request) {
  if (!request->hasParam("freq2") || !request->hasParam("freq1") || !request->hasParam("freq0")) {
    this->send_json_error(request, 400, "Missing freq2, freq1 or freq0 parameters");
    return;
  }
  // Accept hex (0x...) or decimal values
  auto parse_byte = [](const char *s, uint8_t &out) -> bool {
    char *end;
    unsigned long v = strtoul(s, &end, 0);
    if (end == s || v > 0xFF) return false;
    out = (uint8_t)v;
    return true;
  };
  uint8_t f2, f1, f0;
  if (!parse_byte(request->getParam("freq2")->value().c_str(), f2) ||
      !parse_byte(request->getParam("freq1")->value().c_str(), f1) ||
      !parse_byte(request->getParam("freq0")->value().c_str(), f0)) {
    this->send_json_error(request, 400, "Invalid frequency value (0x00-0xFF)");
    return;
  }
  this->parent_->reinit_frequency(f2, f1, f0);
  char buf[96];
  snprintf(buf, sizeof(buf),
    "{\"status\":\"ok\",\"freq2\":\"0x%02x\",\"freq1\":\"0x%02x\",\"freq0\":\"0x%02x\"}",
    f2, f1, f0);
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", buf);
  this->add_cors_headers(response);
  request->send(response);
}

}  // namespace elero
}  // namespace esphome
