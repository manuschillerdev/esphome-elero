#include "elero_web_server.h"
#include "elero_web_ui.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/logger/logger.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace esphome {
namespace elero {

static const char *const TAG = "elero.web";

// Global instance pointer for Mongoose's C callback
static EleroWebServer *g_server = nullptr;

// ─── JSON helpers ─────────────────────────────────────────────────────────────

static std::string json_escape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

// Extract string value from JSON: "key":"value"
static std::string json_find_str(const std::string &json, const char *key) {
  std::string pattern = std::string("\"") + key + "\":\"";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos)
    return "";
  pos += pattern.length();
  size_t end = json.find('"', pos);
  if (end == std::string::npos)
    return "";
  return json.substr(pos, end - pos);
}

// Extract number value from JSON: "key":123
static std::string json_find_num(const std::string &json, const char *key) {
  std::string pattern = std::string("\"") + key + "\":";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos)
    return "";
  pos += pattern.length();
  size_t end = pos;
  while (end < json.size() && (isdigit(json[end]) || json[end] == '-'))
    ++end;
  if (end == pos)
    return "";
  return json.substr(pos, end - pos);
}

// Parse hex string (from "key":"0xNN" or "key":NN) with default
static uint8_t json_find_hex_or(const std::string &json, const char *key, uint8_t def) {
  std::string val = json_find_str(json, key);
  if (val.empty()) {
    val = json_find_num(json, key);
    if (val.empty())
      return def;
  }
  return (uint8_t) strtoul(val.c_str(), nullptr, 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Component Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::setup() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "Elero parent not set");
    this->mark_failed();
    return;
  }

  // Register with hub for RF packet notifications
  this->parent_->set_web_server(this);

  g_server = this;
  mg_mgr_init(&this->mgr_);

  char addr[32];
  snprintf(addr, sizeof(addr), "http://0.0.0.0:%d", this->port_);

  this->listener_ = mg_http_listen(&this->mgr_, addr, event_handler, nullptr);
  if (this->listener_ == nullptr) {
    ESP_LOGE(TAG, "Failed to bind to port %d", this->port_);
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Web UI at http://<ip>:%d/elero", this->port_);
}

void EleroWebServer::loop() {
  // Poll mongoose (non-blocking)
  mg_mgr_poll(&this->mgr_, 0);

  // Clean up disconnected WebSocket clients
  this->ws_cleanup();
}

void EleroWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero Web Server:");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
  ESP_LOGCONFIG(TAG, "  URL: /elero");
  ESP_LOGCONFIG(TAG, "  WebSocket: /elero/ws");
}

// ═══════════════════════════════════════════════════════════════════════════════
// RF Packet Handler
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::on_rf_packet(const RfPacketInfo &pkt) {
  if (this->ws_clients_.empty() || !this->enabled_)
    return;
  this->ws_broadcast("rf", this->build_rf_json(pkt));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mongoose Event Handler
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::event_handler(struct mg_connection *c, int ev, void *ev_data) {
  auto *self = g_server;
  if (self == nullptr)
    return;

  if (ev == MG_EV_HTTP_MSG) {
    auto *hm = static_cast<struct mg_http_message *>(ev_data);

    // WebSocket upgrade
    if (mg_match(hm->uri, mg_str("/elero/ws"), nullptr)) {
      self->handle_ws_upgrade(c, hm);
      return;
    }

    // HTML UI
    if (mg_match(hm->uri, mg_str("/elero"), nullptr)) {
      if (!self->enabled_) {
        mg_http_reply(c, 503, "", "Web UI disabled");
        return;
      }
      self->handle_index(c);
      return;
    }

    // Redirect root to /elero
    if (mg_match(hm->uri, mg_str("/"), nullptr)) {
      mg_http_reply(c, 302, "Location: /elero\r\n", "");
      return;
    }

    mg_http_reply(c, 404, "", "Not found");
  }

  // WebSocket message received
  if (ev == MG_EV_WS_MSG) {
    auto *wm = static_cast<struct mg_ws_message *>(ev_data);
    self->handle_ws_message(c, wm);
  }

  // Connection closed - clean up WebSocket client
  if (ev == MG_EV_CLOSE && c->data[0] == 'W') {
    c->data[0] = 0;
    ESP_LOGD(TAG, "WebSocket client disconnected");
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// HTTP Route Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::handle_index(struct mg_connection *c) {
  mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", ELERO_WEB_UI_HTML);
}

// ═══════════════════════════════════════════════════════════════════════════════
// WebSocket Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::handle_ws_upgrade(struct mg_connection *c, struct mg_http_message *hm) {
  mg_ws_upgrade(c, hm, nullptr);
  c->data[0] = 'W';  // Mark as WebSocket connection
  this->ws_clients_.push_back(c);

  ESP_LOGI(TAG, "WebSocket client connected, %d total", this->ws_clients_.size());

  // Send config on connect
  if (this->enabled_) {
    this->ws_send(c, "config", this->build_config_json());
  }
}

void EleroWebServer::handle_ws_message(struct mg_connection *c, struct mg_ws_message *wm) {
  if (!this->enabled_)
    return;

  std::string msg(wm->data.buf, wm->data.len);
  std::string type = json_find_str(msg, "type");

  // Only command type supported
  if (type == "cmd") {
    std::string address = json_find_str(msg, "address");
    std::string action = json_find_str(msg, "action");

    if (address.empty() || action.empty())
      return;

    uint32_t addr = (uint32_t) strtoul(address.c_str(), nullptr, 0);

    // Try configured covers first
    const auto &covers = this->parent_->get_configured_covers();
    auto it = covers.find(addr);
    if (it != covers.end()) {
      it->second->perform_action(action.c_str());
      return;
    }

    // Try configured lights
    const auto &lights = this->parent_->get_configured_lights();
    auto lit = lights.find(addr);
    if (lit != lights.end()) {
      uint8_t cmd_byte = elero_action_to_command(action.c_str());
      if (cmd_byte != 0xFF) {
        lit->second->enqueue_command(cmd_byte);
      }
      return;
    }

    // Unknown address - could send raw command if we had protocol params
    ESP_LOGW(TAG, "Command for unknown address 0x%06x", addr);
  }

  // Raw TX for testing/debugging
  if (type == "raw") {
    std::string blind_addr_s = json_find_str(msg, "blind_address");
    std::string remote_addr_s = json_find_str(msg, "remote_address");
    std::string channel_s = json_find_str(msg, "channel");
    std::string command_s = json_find_str(msg, "command");

    // Also try numeric format for channel
    if (channel_s.empty())
      channel_s = json_find_num(msg, "channel");

    if (blind_addr_s.empty() || remote_addr_s.empty() || channel_s.empty() || command_s.empty()) {
      ESP_LOGW(TAG, "Raw TX missing required fields");
      return;
    }

    uint32_t blind_addr = (uint32_t) strtoul(blind_addr_s.c_str(), nullptr, 0);
    uint32_t remote_addr = (uint32_t) strtoul(remote_addr_s.c_str(), nullptr, 0);
    uint8_t channel = (uint8_t) strtoul(channel_s.c_str(), nullptr, 0);
    uint8_t command = (uint8_t) strtoul(command_s.c_str(), nullptr, 0);

    uint8_t payload_1 = json_find_hex_or(msg, "payload_1", 0x00);
    uint8_t payload_2 = json_find_hex_or(msg, "payload_2", 0x04);
    uint8_t pck_inf1 = json_find_hex_or(msg, "pck_inf1", 0x6a);
    uint8_t pck_inf2 = json_find_hex_or(msg, "pck_inf2", 0x00);
    uint8_t hop = json_find_hex_or(msg, "hop", 0x0a);

    bool success = this->parent_->send_raw_command(
        blind_addr, remote_addr, channel, command,
        payload_1, payload_2, pck_inf1, pck_inf2, hop);
    ESP_LOGI(TAG, "Raw TX to 0x%06x cmd=0x%02x: %s", blind_addr, command, success ? "OK" : "FAIL");
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// WebSocket Helpers
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::ws_send(struct mg_connection *c, const char *event, const std::string &data) {
  if (c == nullptr || c->is_closing)
    return;
  std::string msg = "{\"event\":\"";
  msg += event;
  msg += "\",\"data\":";
  msg += data;
  msg += "}";
  mg_ws_send(c, msg.c_str(), msg.size(), WEBSOCKET_OP_TEXT);
}

void EleroWebServer::ws_broadcast(const char *event, const std::string &data) {
  for (auto *c : this->ws_clients_) {
    this->ws_send(c, event, data);
  }
}

void EleroWebServer::ws_cleanup() {
  this->ws_clients_.erase(
      std::remove_if(this->ws_clients_.begin(), this->ws_clients_.end(),
                     [](struct mg_connection *c) { return c->is_closing || c->data[0] != 'W'; }),
      this->ws_clients_.end());
}

// ═══════════════════════════════════════════════════════════════════════════════
// JSON Builders
// ═══════════════════════════════════════════════════════════════════════════════

std::string EleroWebServer::build_config_json() {
  std::string json = "{";

  // Device info
  json += "\"device\":\"";
  json += json_escape(App.get_name());
  json += "\",";

  // Frequency
  char freq_buf[64];
  snprintf(freq_buf, sizeof(freq_buf),
           "\"freq\":{\"freq2\":\"0x%02x\",\"freq1\":\"0x%02x\",\"freq0\":\"0x%02x\"},",
           this->parent_->get_freq2(), this->parent_->get_freq1(), this->parent_->get_freq0());
  json += freq_buf;

  // Configured blinds
  json += "\"blinds\":[";
  bool first = true;
  for (const auto &pair : this->parent_->get_configured_covers()) {
    if (!first) json += ",";
    first = false;
    auto *blind = pair.second;
    char buf[384];
    snprintf(buf, sizeof(buf),
             "{\"address\":\"0x%06x\","
             "\"name\":\"%s\","
             "\"channel\":%d,"
             "\"remote\":\"0x%06x\","
             "\"open_ms\":%lu,"
             "\"close_ms\":%lu,"
             "\"poll_ms\":%lu,"
             "\"tilt\":%s}",
             pair.first,
             json_escape(blind->get_blind_name()).c_str(),
             (int) blind->get_channel(),
             blind->get_remote_address(),
             (unsigned long) blind->get_open_duration_ms(),
             (unsigned long) blind->get_close_duration_ms(),
             (unsigned long) blind->get_poll_interval_ms(),
             blind->get_supports_tilt() ? "true" : "false");
    json += buf;
  }
  json += "],";

  // Configured lights
  json += "\"lights\":[";
  first = true;
  for (const auto &pair : this->parent_->get_configured_lights()) {
    if (!first) json += ",";
    first = false;
    auto *light = pair.second;
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"address\":\"0x%06x\","
             "\"name\":\"%s\","
             "\"channel\":%d,"
             "\"remote\":\"0x%06x\","
             "\"dim_ms\":%lu}",
             pair.first,
             json_escape(light->get_light_name()).c_str(),
             (int) light->get_channel(),
             light->get_remote_address(),
             (unsigned long) light->get_dim_duration_ms());
    json += buf;
  }
  json += "]";

  json += "}";
  return json;
}

std::string EleroWebServer::build_rf_json(const RfPacketInfo &pkt) {
  // Build hex string of raw packet
  char hex[CC1101_FIFO_LENGTH * 3 + 1];
  hex[0] = '\0';
  for (int i = 0; i < pkt.raw_len && i < CC1101_FIFO_LENGTH; i++) {
    char byte_buf[4];
    snprintf(byte_buf, sizeof(byte_buf), i == 0 ? "%02x" : " %02x", pkt.raw[i]);
    strncat(hex, byte_buf, sizeof(hex) - strlen(hex) - 1);
  }

  char buf[512];
  snprintf(buf, sizeof(buf),
           "{\"t\":%lu,"
           "\"src\":\"0x%06x\","
           "\"dst\":\"0x%06x\","
           "\"ch\":%d,"
           "\"type\":\"0x%02x\","
           "\"cmd\":\"0x%02x\","
           "\"state\":\"0x%02x\","
           "\"rssi\":%.1f,"
           "\"hop\":\"0x%02x\","
           "\"raw\":\"%s\"}",
           (unsigned long) pkt.timestamp_ms,
           pkt.src,
           pkt.dst,
           pkt.channel,
           pkt.type,
           pkt.cmd,
           pkt.state,
           pkt.rssi,
           pkt.hop,
           hex);
  return std::string(buf);
}

}  // namespace elero
}  // namespace esphome
