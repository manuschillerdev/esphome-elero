#include "elero_web_server.h"
#include "elero_web_ui.h"
#include "../elero/elero_packet.h"
#include "../elero/nvs_config.h"
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

// Extract boolean value from JSON: "key":true/false
static bool json_find_bool(const std::string &json, const char *key, bool def) {
  std::string pattern = std::string("\"") + key + "\":";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos)
    return def;
  pos += pattern.length();
  if (pos < json.size() && json[pos] == 't')
    return true;
  if (pos < json.size() && json[pos] == 'f')
    return false;
  return def;
}

// Parse "cover"/"light"/"remote" into DeviceType enum. Returns false if invalid.
static bool parse_device_type(const std::string &str, DeviceType &out) {
  if (str == "cover") { out = DeviceType::COVER; return true; }
  if (str == "light") { out = DeviceType::LIGHT; return true; }
  if (str == "remote") { out = DeviceType::REMOTE; return true; }
  return false;
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

  // Register callback with hub for RF packet notifications
  this->parent_->set_rf_packet_callback([this](const RfPacketInfo &pkt) {
    this->on_rf_packet(pkt);
  });

  // Register as log listener to forward logs to WebSocket clients
  if (logger::global_logger != nullptr) {
    logger::global_logger->add_log_listener(this);
  }

  // Register CRUD event callback with device manager (if MQTT mode)
  // This broadcasts manager-initiated events (e.g., remote auto-discovery) to WS clients
  auto *dm = this->parent_->get_device_manager();
  if (dm != nullptr && dm->supports_crud()) {
    dm->set_crud_callback([this](const char *event, const char *json) {
      this->ws_broadcast(event, std::string(json));
    });
  }

  if (g_server != nullptr) {
    ESP_LOGE(TAG, "Only one EleroWebServer instance is supported");
    this->mark_failed();
    return;
  }
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
// Log Listener
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::on_log(uint8_t level, const char *tag, const char *message, size_t message_len) {
  if (this->ws_clients_.empty() || !this->enabled_)
    return;

  // Only forward elero-related logs
  if (tag == nullptr || strncmp(tag, "elero", 5) != 0)
    return;

  // Build JSON: {"t":<ms>,"level":<n>,"tag":"...","msg":"..."}
  std::string escaped_msg = json_escape(std::string(message, message_len));
  char buf[512];
  snprintf(buf, sizeof(buf),
           "{\"t\":%lu,\"level\":%d,\"tag\":\"%s\",\"msg\":\"%s\"}",
           (unsigned long) millis(),
           (int) level,
           tag,
           escaped_msg.c_str());

  this->ws_broadcast("log", std::string(buf));
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

  // Command to blind/light
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
      lit->second->perform_action(action.c_str());
      return;
    }

    ESP_LOGW(TAG, "Command for unknown address 0x%06x", addr);
    return;
  }

  // Device CRUD (MQTT mode only)
  if (type == "save_device") {
    this->handle_save_device_(c, msg);
    return;
  }
  if (type == "remove_device") {
    this->handle_remove_device_(c, msg);
    return;
  }
  if (type == "update_device") {
    this->handle_update_device_(c, msg);
    return;
  }
  if (type == "enable_device") {
    this->handle_enable_device_(c, msg);
    return;
  }

  // Raw TX for testing/debugging
  if (type == "raw") {
    std::string dst_addr_s = json_find_str(msg, "dst_address");
    std::string src_addr_s = json_find_str(msg, "src_address");
    std::string channel_s = json_find_str(msg, "channel");
    std::string command_s = json_find_str(msg, "command");

    // Also try numeric format for channel
    if (channel_s.empty())
      channel_s = json_find_num(msg, "channel");

    if (dst_addr_s.empty() || src_addr_s.empty() || channel_s.empty() || command_s.empty()) {
      ESP_LOGW(TAG, "Raw TX missing required fields");
      return;
    }

    uint32_t dst_addr = (uint32_t) strtoul(dst_addr_s.c_str(), nullptr, 0);
    uint32_t src_addr = (uint32_t) strtoul(src_addr_s.c_str(), nullptr, 0);
    uint8_t channel = (uint8_t) strtoul(channel_s.c_str(), nullptr, 0);
    uint8_t command = (uint8_t) strtoul(command_s.c_str(), nullptr, 0);

    uint8_t payload_1 = json_find_hex_or(msg, "payload_1", elero::packet::defaults::PAYLOAD_1);
    uint8_t payload_2 = json_find_hex_or(msg, "payload_2", elero::packet::defaults::PAYLOAD_2);
    uint8_t msg_type = json_find_hex_or(msg, "msg_type", elero::packet::msg_type::COMMAND);
    uint8_t type2 = json_find_hex_or(msg, "type2", elero::packet::defaults::TYPE2);
    uint8_t hop = json_find_hex_or(msg, "hop", elero::packet::defaults::HOP);

    bool success = this->parent_->send_raw_command(
        dst_addr, src_addr, channel, command,
        payload_1, payload_2, msg_type, type2, hop);
    ESP_LOGI(TAG, "Raw TX to 0x%06x cmd=0x%02x: %s", dst_addr, command, success ? "OK" : "FAIL");
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
  json += "],";

  // Mode
  auto *dm = this->parent_->get_device_manager();
  if (dm != nullptr && dm->mode() == HubMode::MQTT) {
    json += "\"mode\":\"mqtt\",\"crud\":true";
  } else {
    json += "\"mode\":\"native\",\"crud\":false";
  }

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
           "\"channel\":%d,"
           "\"type\":\"0x%02x\","
           "\"type2\":\"0x%02x\","
           "\"command\":\"0x%02x\","
           "\"state\":\"0x%02x\","
           "\"echo\":%s,"
           "\"cnt\":%d,"
           "\"rssi\":%.1f,"
           "\"hop\":\"0x%02x\","
           "\"raw\":\"%s\"}",
           (unsigned long) pkt.timestamp_ms,
           pkt.src,
           pkt.dst,
           pkt.channel,
           pkt.type,
           pkt.type2,
           pkt.command,
           pkt.state,
           pkt.echo ? "true" : "false",
           pkt.cnt,
           pkt.rssi,
           pkt.hop,
           hex);
  return std::string(buf);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Device Config Parser (shared by save/update)
// ═══════════════════════════════════════════════════════════════════════════════

bool EleroWebServer::parse_device_config_(const std::string &msg, NvsDeviceConfig &config, std::string &error) {
  if (!parse_device_type(json_find_str(msg, "device_type"), config.type)) {
    error = "Invalid device_type";
    return false;
  }

  std::string dst_addr_s = json_find_str(msg, "dst_address");
  if (dst_addr_s.empty()) {
    error = "Missing dst_address";
    return false;
  }
  config.dst_address = (uint32_t) strtoul(dst_addr_s.c_str(), nullptr, 0);

  std::string name_s = json_find_str(msg, "name");
  if (!name_s.empty()) {
    config.set_name(name_s.c_str());
  }

  // RF params (covers and lights only)
  if (!config.is_remote()) {
    std::string src_addr_s = json_find_str(msg, "src_address");
    if (!src_addr_s.empty()) {
      config.src_address = (uint32_t) strtoul(src_addr_s.c_str(), nullptr, 0);
    }
    std::string channel_s = json_find_num(msg, "channel");
    if (!channel_s.empty()) {
      config.channel = (uint8_t) strtoul(channel_s.c_str(), nullptr, 0);
    }
    config.hop = json_find_hex_or(msg, "hop", packet::defaults::HOP);
    config.payload_1 = json_find_hex_or(msg, "payload_1", packet::defaults::PAYLOAD_1);
    config.payload_2 = json_find_hex_or(msg, "payload_2", packet::defaults::PAYLOAD_2);
    config.type_byte = json_find_hex_or(msg, "msg_type", packet::msg_type::COMMAND);
    config.type2 = json_find_hex_or(msg, "type2", packet::defaults::TYPE2);

    // Cover-specific
    if (config.is_cover()) {
      config.supports_tilt = json_find_bool(msg, "supports_tilt", false) ? 1 : 0;
    }
  }

  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Device CRUD Handlers (MQTT mode)
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::handle_save_device_(struct mg_connection *c, const std::string &msg) {
  auto *dm = this->parent_->get_device_manager();
  if (dm == nullptr || !dm->supports_crud()) {
    this->ws_send(c, "error", "{\"msg\":\"CRUD not supported in native mode\"}");
    return;
  }

  NvsDeviceConfig config{};
  std::string error;
  if (!parse_device_config_(msg, config, error)) {
    std::string resp = "{\"msg\":\"" + error + "\"}";
    this->ws_send(c, "error", resp);
    return;
  }

  if (dm->add_device(config)) {
    // CRUD event broadcast handled by device manager's notify_crud_ callback
  } else {
    this->ws_send(c, "error", "{\"msg\":\"Failed to save device\"}");
  }
}

void EleroWebServer::handle_remove_device_(struct mg_connection *c, const std::string &msg) {
  auto *dm = this->parent_->get_device_manager();
  if (dm == nullptr || !dm->supports_crud()) {
    this->ws_send(c, "error", "{\"msg\":\"CRUD not supported in native mode\"}");
    return;
  }

  std::string addr_s = json_find_str(msg, "dst_address");
  if (addr_s.empty()) {
    this->ws_send(c, "error", "{\"msg\":\"Missing dst_address\"}");
    return;
  }

  DeviceType type;
  if (!parse_device_type(json_find_str(msg, "device_type"), type)) {
    this->ws_send(c, "error", "{\"msg\":\"Invalid device_type\"}");
    return;
  }

  uint32_t addr = (uint32_t) strtoul(addr_s.c_str(), nullptr, 0);

  if (dm->remove_device(type, addr)) {
    // CRUD event broadcast handled by device manager's notify_crud_ callback
  } else {
    this->ws_send(c, "error", "{\"msg\":\"Failed to remove device\"}");
  }
}

void EleroWebServer::handle_update_device_(struct mg_connection *c, const std::string &msg) {
  auto *dm = this->parent_->get_device_manager();
  if (dm == nullptr || !dm->supports_crud()) {
    this->ws_send(c, "error", "{\"msg\":\"CRUD not supported in native mode\"}");
    return;
  }

  NvsDeviceConfig config{};
  std::string error;
  if (!parse_device_config_(msg, config, error)) {
    std::string resp = "{\"msg\":\"" + error + "\"}";
    this->ws_send(c, "error", resp);
    return;
  }

  if (dm->update_device(config)) {
    // CRUD event broadcast handled by device manager's notify_crud_ callback
  } else {
    this->ws_send(c, "error", "{\"msg\":\"Failed to update device\"}");
  }
}

void EleroWebServer::handle_enable_device_(struct mg_connection *c, const std::string &msg) {
  auto *dm = this->parent_->get_device_manager();
  if (dm == nullptr || !dm->supports_crud()) {
    this->ws_send(c, "error", "{\"msg\":\"CRUD not supported in native mode\"}");
    return;
  }

  std::string addr_s = json_find_str(msg, "dst_address");
  bool enabled = json_find_bool(msg, "enabled", true);

  if (addr_s.empty()) {
    this->ws_send(c, "error", "{\"msg\":\"Missing dst_address\"}");
    return;
  }

  DeviceType type;
  if (!parse_device_type(json_find_str(msg, "device_type"), type) ||
      type == DeviceType::REMOTE) {
    this->ws_send(c, "error", "{\"msg\":\"Only covers and lights can be enabled/disabled\"}");
    return;
  }

  uint32_t addr = (uint32_t) strtoul(addr_s.c_str(), nullptr, 0);

  if (dm->set_device_enabled(type, addr, enabled)) {
    // CRUD event broadcast handled by device manager's notify_crud_ callback
  } else {
    this->ws_send(c, "error", "{\"msg\":\"Failed to set device enabled state\"}");
  }
}

}  // namespace elero
}  // namespace esphome
