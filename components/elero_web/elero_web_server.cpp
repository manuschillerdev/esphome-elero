#include "elero_web_server.h"
#include "elero_web_ui.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
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

// Extract uint value from JSON: "key":123
static uint32_t json_find_uint(const std::string &json, const char *key) {
  std::string pattern = std::string("\"") + key + "\":";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos)
    return 0;
  pos += pattern.length();
  return (uint32_t) strtoul(json.c_str() + pos, nullptr, 0);
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

  ESP_LOGI(TAG, "Web UI at http://<ip>:%d/elero (Mongoose)", this->port_);
}

void EleroWebServer::loop() {
  // Poll mongoose (non-blocking)
  mg_mgr_poll(&this->mgr_, 0);

  // Clean up disconnected SSE clients
  this->sse_cleanup();

  if (!this->enabled_ || this->sse_clients_.empty())
    return;

  uint32_t now = millis();

  // Push incremental updates
  if (this->covers_dirty_) {
    this->covers_dirty_ = false;
    this->sse_broadcast("covers", this->build_covers_json());
  }

  if (this->discovered_dirty_ || this->scan_dirty_) {
    this->discovered_dirty_ = false;
    this->scan_dirty_ = false;
    this->sse_broadcast("discovered", this->build_discovered_json());
  }

  if (this->logs_dirty_) {
    this->logs_dirty_ = false;
    std::string logs = this->build_logs_json(this->last_log_push_ts_, 20);
    if (!logs.empty() && logs != "[]") {
      this->sse_broadcast("log", logs);
      const auto &entries = this->parent_->get_log_entries();
      if (!entries.empty()) {
        this->last_log_push_ts_ = entries.back().timestamp_ms;
      }
    }
  }

  if (this->packets_dirty_) {
    this->packets_dirty_ = false;
    this->sse_broadcast("packets", this->build_packets_json());
  }

  // Heartbeat
  if (now - this->last_heartbeat_ms_ >= ELERO_SSE_HEARTBEAT_INTERVAL) {
    this->last_heartbeat_ms_ = now;
    this->sse_broadcast("state", this->build_full_state_json());
  }
}

void EleroWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero Web Server (Mongoose):");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
  ESP_LOGCONFIG(TAG, "  URL: /elero");
  ESP_LOGCONFIG(TAG, "  SSE: /elero/events");
  ESP_LOGCONFIG(TAG, "  Enabled: %s", this->enabled_ ? "yes" : "no");
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

    // Check if disabled (except for SSE which we just won't send data)
    bool disabled = !self->enabled_;

    // ─── SSE endpoint ─────────────────────────────────────────────────────────
    if (mg_match(hm->uri, mg_str("/elero/events"), nullptr)) {
      self->handle_sse_connect(c);
      return;
    }

    // ─── Static routes ────────────────────────────────────────────────────────
    if (mg_match(hm->uri, mg_str("/elero"), nullptr)) {
      if (disabled) {
        mg_http_reply(c, 503, "", "Web UI disabled");
        return;
      }
      self->handle_index(c);
      return;
    }

    if (mg_match(hm->uri, mg_str("/"), nullptr)) {
      mg_http_reply(c, 302, "Location: /elero\r\n", "");
      return;
    }

    // ─── API GET routes ───────────────────────────────────────────────────────
    if (mg_match(hm->uri, mg_str("/elero/api/state"), nullptr)) {
      if (disabled) {
        self->send_json_error(c, 503, "disabled");
        return;
      }
      self->handle_get_state(c);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/yaml"), nullptr)) {
      if (disabled) {
        mg_http_reply(c, 503, "", "Web UI disabled");
        return;
      }
      self->handle_get_yaml(c);
      return;
    }

    // ─── API POST routes (no body) ────────────────────────────────────────────
    if (mg_match(hm->uri, mg_str("/elero/api/scan/start"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_scan_start(c);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/scan/stop"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_scan_stop(c);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/log/start"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_log_start(c);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/log/stop"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_log_stop(c);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/log/clear"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_log_clear(c);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/dump/start"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_dump_start(c);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/dump/stop"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_dump_stop(c);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/dump/clear"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_dump_clear(c);
      return;
    }

    // ─── API POST routes (with JSON body) ─────────────────────────────────────
    if (mg_match(hm->uri, mg_str("/elero/api/cover"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_cover(c, hm);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/settings"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_settings(c, hm);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/adopt"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_adopt(c, hm);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/runtime/remove"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_runtime_remove(c, hm);
      return;
    }

    if (mg_match(hm->uri, mg_str("/elero/api/frequency"), nullptr)) {
      if (disabled) { self->send_json_error(c, 503, "disabled"); return; }
      self->handle_post_frequency(c, hm);
      return;
    }

    // ─── 404 ──────────────────────────────────────────────────────────────────
    mg_http_reply(c, 404, "", "Not found");
  }

  // Connection closed - clean up SSE client
  if (ev == MG_EV_CLOSE && c->data[0] == 'S') {
    c->data[0] = 0;  // Clear marker
    ESP_LOGD(TAG, "SSE client disconnected");
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Route Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::handle_index(struct mg_connection *c) {
  mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", ELERO_WEB_UI_HTML);
}

void EleroWebServer::handle_get_state(struct mg_connection *c) {
  std::string json = this->build_full_state_json();
  mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json.c_str());
}

void EleroWebServer::handle_get_yaml(struct mg_connection *c) {
  std::string yaml = this->build_yaml();
  mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "%s", yaml.c_str());
}

void EleroWebServer::handle_sse_connect(struct mg_connection *c) {
  mg_printf(c,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "Access-Control-Allow-Origin: *\r\n\r\n");

  c->data[0] = 'S';  // Mark as SSE connection
  this->sse_clients_.push_back(c);

  ESP_LOGI(TAG, "SSE client connected, %d total", this->sse_clients_.size());

  // Send initial state
  if (this->enabled_) {
    this->sse_send(c, "state", this->build_full_state_json());
  }
}

void EleroWebServer::handle_post_scan_start(struct mg_connection *c) {
  if (this->parent_->start_scan()) {
    this->notify_scan_status_changed();
    this->send_json_ok(c);
  } else {
    this->send_json_error(c, 409, "Already scanning");
  }
}

void EleroWebServer::handle_post_scan_stop(struct mg_connection *c) {
  if (this->parent_->stop_scan()) {
    this->notify_scan_status_changed();
    this->send_json_ok(c);
  } else {
    this->send_json_error(c, 409, "Not scanning");
  }
}

void EleroWebServer::handle_post_log_start(struct mg_connection *c) {
  this->parent_->set_log_capture(true);
  this->send_json_ok(c);
}

void EleroWebServer::handle_post_log_stop(struct mg_connection *c) {
  this->parent_->set_log_capture(false);
  this->send_json_ok(c);
}

void EleroWebServer::handle_post_log_clear(struct mg_connection *c) {
  this->parent_->clear_log_entries();
  this->last_log_push_ts_ = 0;
  this->send_json_ok(c);
}

void EleroWebServer::handle_post_dump_start(struct mg_connection *c) {
  if (this->parent_->start_packet_dump()) {
    this->send_json_ok(c);
  } else {
    this->send_json_error(c, 409, "Dump already running");
  }
}

void EleroWebServer::handle_post_dump_stop(struct mg_connection *c) {
  if (this->parent_->stop_packet_dump()) {
    this->send_json_ok(c);
  } else {
    this->send_json_error(c, 409, "No dump running");
  }
}

void EleroWebServer::handle_post_dump_clear(struct mg_connection *c) {
  this->parent_->clear_raw_packets();
  this->send_json_ok(c);
}

void EleroWebServer::handle_post_cover(struct mg_connection *c, struct mg_http_message *hm) {
  std::string body(hm->body.buf, hm->body.len);
  std::string address = json_find_str(body, "address");
  std::string action = json_find_str(body, "action");

  if (address.empty() || action.empty()) {
    this->send_json_error(c, 400, "Missing address or action");
    return;
  }

  uint32_t addr = (uint32_t) strtoul(address.c_str(), nullptr, 0);
  uint8_t cmd_byte = elero_action_to_command(action.c_str());

  if (cmd_byte == 0xFF) {
    this->send_json_error(c, 400, "Unknown action");
    return;
  }

  const auto &covers = this->parent_->get_configured_covers();
  auto it = covers.find(addr);
  if (it != covers.end()) {
    if (!it->second->perform_action(action.c_str())) {
      this->send_json_error(c, 400, "Unknown action");
      return;
    }
    this->notify_covers_changed();
    this->send_json_ok(c);
    return;
  }

  if (this->parent_->send_runtime_command(addr, cmd_byte)) {
    this->send_json_ok(c);
  } else {
    this->send_json_error(c, 404, "Cover not found");
  }
}

void EleroWebServer::handle_post_settings(struct mg_connection *c, struct mg_http_message *hm) {
  std::string body(hm->body.buf, hm->body.len);
  std::string address = json_find_str(body, "address");

  if (address.empty()) {
    this->send_json_error(c, 400, "Missing address");
    return;
  }

  uint32_t addr = (uint32_t) strtoul(address.c_str(), nullptr, 0);
  uint32_t open_dur = json_find_uint(body, "open_duration");
  uint32_t close_dur = json_find_uint(body, "close_duration");
  uint32_t poll_intvl = json_find_uint(body, "poll_interval");

  const auto &covers = this->parent_->get_configured_covers();
  auto it = covers.find(addr);
  if (it != covers.end()) {
    it->second->apply_runtime_settings(open_dur, close_dur, poll_intvl);
    this->notify_covers_changed();
    this->send_json_ok(c);
    return;
  }

  if (this->parent_->update_runtime_blind_settings(addr, open_dur, close_dur, poll_intvl)) {
    this->notify_covers_changed();
    this->send_json_ok(c);
  } else {
    this->send_json_error(c, 404, "Cover not found");
  }
}

void EleroWebServer::handle_post_adopt(struct mg_connection *c, struct mg_http_message *hm) {
  std::string body(hm->body.buf, hm->body.len);
  std::string address = json_find_str(body, "address");
  std::string name = json_find_str(body, "name");

  if (address.empty()) {
    this->send_json_error(c, 400, "Missing address");
    return;
  }

  uint32_t addr = (uint32_t) strtoul(address.c_str(), nullptr, 0);
  if (!this->parent_->adopt_blind_by_address(addr, name)) {
    this->send_json_error(c, 409, "Not found or already configured");
    return;
  }

  this->notify_covers_changed();
  this->notify_discovered_changed();
  this->send_json_ok(c);
}

void EleroWebServer::handle_post_runtime_remove(struct mg_connection *c, struct mg_http_message *hm) {
  std::string body(hm->body.buf, hm->body.len);
  std::string address = json_find_str(body, "address");

  if (address.empty()) {
    this->send_json_error(c, 400, "Missing address");
    return;
  }

  uint32_t addr = (uint32_t) strtoul(address.c_str(), nullptr, 0);
  if (this->parent_->remove_runtime_blind(addr)) {
    this->notify_covers_changed();
    this->send_json_ok(c);
  } else {
    this->send_json_error(c, 404, "Runtime blind not found");
  }
}

void EleroWebServer::handle_post_frequency(struct mg_connection *c, struct mg_http_message *hm) {
  std::string body(hm->body.buf, hm->body.len);
  std::string f2 = json_find_str(body, "freq2");
  std::string f1 = json_find_str(body, "freq1");
  std::string f0 = json_find_str(body, "freq0");

  if (f2.empty() || f1.empty() || f0.empty()) {
    this->send_json_error(c, 400, "Missing freq2, freq1 or freq0");
    return;
  }

  auto parse_byte = [](const std::string &s, uint8_t &out) -> bool {
    char *end;
    unsigned long v = strtoul(s.c_str(), &end, 0);
    if (end == s.c_str() || v > 0xFF)
      return false;
    out = (uint8_t) v;
    return true;
  };

  uint8_t freq2, freq1, freq0;
  if (!parse_byte(f2, freq2) || !parse_byte(f1, freq1) || !parse_byte(f0, freq0)) {
    this->send_json_error(c, 400, "Invalid frequency value");
    return;
  }

  this->parent_->reinit_frequency(freq2, freq1, freq0);
  this->send_json_ok(c);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SSE Helpers
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::sse_send(struct mg_connection *c, const char *event, const std::string &data) {
  if (c == nullptr || c->is_closing)
    return;
  mg_printf(c, "event: %s\ndata: %s\n\n", event, data.c_str());
}

void EleroWebServer::sse_broadcast(const char *event, const std::string &data) {
  for (auto *c : this->sse_clients_) {
    this->sse_send(c, event, data);
  }
}

void EleroWebServer::sse_cleanup() {
  this->sse_clients_.erase(
      std::remove_if(this->sse_clients_.begin(), this->sse_clients_.end(),
                     [](struct mg_connection *c) { return c->is_closing || c->data[0] != 'S'; }),
      this->sse_clients_.end());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Response Helpers
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::send_json_ok(struct mg_connection *c) {
  mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"ok\":true}");
}

void EleroWebServer::send_json_error(struct mg_connection *c, int code, const char *error) {
  mg_http_reply(c, code, "Content-Type: application/json\r\n",
                "{\"ok\":false,\"error\":\"%s\"}", error);
}

// ═══════════════════════════════════════════════════════════════════════════════
// JSON Builders
// ═══════════════════════════════════════════════════════════════════════════════

std::string EleroWebServer::build_full_state_json() {
  std::string esc_app_name = json_escape(App.get_name());
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"device_name\":\"%s\","
           "\"uptime_ms\":%lu,"
           "\"freq\":{\"freq2\":\"0x%02x\",\"freq1\":\"0x%02x\",\"freq0\":\"0x%02x\"},"
           "\"scanning\":%s,"
           "\"log_capture\":%s,"
           "\"dump_active\":%s,"
           "\"covers\":",
           esc_app_name.c_str(), (unsigned long) millis(), this->parent_->get_freq2(), this->parent_->get_freq1(),
           this->parent_->get_freq0(), this->parent_->is_scanning() ? "true" : "false",
           this->parent_->is_log_capture_active() ? "true" : "false",
           this->parent_->is_packet_dump_active() ? "true" : "false");

  std::string json = buf;
  json += this->build_covers_json();
  json += ",\"discovered\":";
  json += this->build_discovered_json();
  json += "}";

  return json;
}

std::string EleroWebServer::build_covers_json() {
  std::string json = "[";
  bool first = true;

  // ESPHome configured covers
  for (const auto &pair : this->parent_->get_configured_covers()) {
    if (!first)
      json += ",";
    first = false;
    auto *blind = pair.second;
    std::string esc_name = json_escape(blind->get_blind_name());
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"blind_address\":\"0x%06x\","
             "\"name\":\"%s\","
             "\"position\":%.2f,"
             "\"operation\":\"%s\","
             "\"last_state\":\"%s\","
             "\"last_seen_ms\":%lu,"
             "\"rssi\":%.1f,"
             "\"channel\":%d,"
             "\"remote_address\":\"0x%06x\","
             "\"poll_interval_ms\":%lu,"
             "\"open_duration_ms\":%lu,"
             "\"close_duration_ms\":%lu,"
             "\"supports_tilt\":%s,"
             "\"adopted\":false}",
             pair.first, esc_name.c_str(), blind->get_cover_position(), blind->get_operation_str(),
             elero_state_to_string(blind->get_last_state_raw()), (unsigned long) blind->get_last_seen_ms(),
             blind->get_last_rssi(), (int) blind->get_channel(), blind->get_remote_address(),
             (unsigned long) blind->get_poll_interval_ms(), (unsigned long) blind->get_open_duration_ms(),
             (unsigned long) blind->get_close_duration_ms(), blind->get_supports_tilt() ? "true" : "false");
    json += buf;
  }

  // Runtime adopted blinds
  for (const auto &entry : this->parent_->get_runtime_blinds()) {
    const auto &rb = entry.second;
    if (!first)
      json += ",";
    first = false;
    std::string esc_name = json_escape(rb.name);
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"blind_address\":\"0x%06x\","
             "\"name\":\"%s\","
             "\"position\":null,"
             "\"operation\":\"idle\","
             "\"last_state\":\"%s\","
             "\"last_seen_ms\":%lu,"
             "\"rssi\":%.1f,"
             "\"channel\":%d,"
             "\"remote_address\":\"0x%06x\","
             "\"poll_interval_ms\":%lu,"
             "\"open_duration_ms\":%lu,"
             "\"close_duration_ms\":%lu,"
             "\"supports_tilt\":false,"
             "\"adopted\":true}",
             rb.blind_address, esc_name.c_str(), elero_state_to_string(rb.last_state), (unsigned long) rb.last_seen_ms,
             rb.last_rssi, (int) rb.channel, rb.remote_address, (unsigned long) rb.poll_intvl_ms,
             (unsigned long) rb.open_duration_ms, (unsigned long) rb.close_duration_ms);
    json += buf;
  }

  json += "]";
  return json;
}

std::string EleroWebServer::build_discovered_json() {
  std::string json = "{\"scanning\":";
  json += this->parent_->is_scanning() ? "true" : "false";
  json += ",\"blinds\":[";

  const auto &blinds = this->parent_->get_discovered_blinds();
  bool first = true;
  for (const auto &blind : blinds) {
    if (!first)
      json += ",";
    first = false;

    char buf[640];
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
             "\"last_seen_ms\":%lu,"
             "\"params_from_command\":%s,"
             "\"already_configured\":%s,"
             "\"already_adopted\":%s}",
             blind.blind_address, blind.remote_address, blind.channel, blind.rssi,
             elero_state_to_string(blind.last_state), blind.times_seen, blind.hop, blind.payload_1, blind.payload_2,
             blind.pck_inf[0], blind.pck_inf[1], (unsigned long) blind.last_seen,
             blind.params_from_command ? "true" : "false",
             this->parent_->is_cover_configured(blind.blind_address) ? "true" : "false",
             this->parent_->is_blind_adopted(blind.blind_address) ? "true" : "false");
    json += buf;
  }

  json += "]}";
  return json;
}

std::string EleroWebServer::build_logs_json(uint32_t since_ms, size_t max_entries) {
  const auto &entries = this->parent_->get_log_entries();
  static const char *level_strs[] = {"", "error", "warn", "info", "debug", "verbose"};

  std::string json = "[";
  bool first = true;
  size_t count = 0;

  for (const auto &e : entries) {
    if (e.timestamp_ms <= since_ms)
      continue;
    if (count >= max_entries)
      break;

    if (!first)
      json += ",";
    first = false;

    uint8_t lv = (e.level >= 1 && e.level <= 5) ? e.level : 3;
    std::string msg_esc = json_escape(e.message);
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"t\":%lu,\"level\":%d,\"level_str\":\"%s\",\"tag\":\"%s\",\"msg\":\"%s\"}",
             (unsigned long) e.timestamp_ms, lv, level_strs[lv], e.tag, msg_esc.c_str());
    json += buf;
    count++;
  }

  json += "]";
  return json;
}

std::string EleroWebServer::build_packets_json() {
  const auto &packets = this->parent_->get_raw_packets();

  std::string json = "{\"dump_active\":";
  json += this->parent_->is_packet_dump_active() ? "true" : "false";
  json += ",\"count\":";
  char cnt_buf[12];
  snprintf(cnt_buf, sizeof(cnt_buf), "%d", (int) packets.size());
  json += cnt_buf;
  json += ",\"packets\":[";

  bool first = true;
  for (const auto &pkt : packets) {
    if (!first)
      json += ",";
    first = false;

    char hex_buf[CC1101_FIFO_LENGTH * 3 + 1];
    hex_buf[0] = '\0';
    for (int i = 0; i < pkt.fifo_len && i < CC1101_FIFO_LENGTH; i++) {
      char byte_buf[4];
      snprintf(byte_buf, sizeof(byte_buf), i == 0 ? "%02x" : " %02x", pkt.data[i]);
      strncat(hex_buf, byte_buf, sizeof(hex_buf) - strlen(hex_buf) - 1);
    }

    char entry_buf[320];
    snprintf(entry_buf, sizeof(entry_buf), "{\"t\":%lu,\"len\":%d,\"valid\":%s,\"reason\":\"%s\",\"hex\":\"%s\"}",
             (unsigned long) pkt.timestamp_ms, pkt.fifo_len, pkt.valid ? "true" : "false", pkt.reject_reason, hex_buf);
    json += entry_buf;
  }

  json += "]}";
  return json;
}

std::string EleroWebServer::build_yaml() {
  const auto &blinds = this->parent_->get_discovered_blinds();
  if (blinds.empty()) {
    return "# No discovered blinds.\n# Start a scan and press buttons on your remote.\n";
  }

  std::string yaml = "# Auto-generated YAML from Elero RF discovery\n";
  yaml += "# Copy this into your ESPHome configuration.\n\n";
  yaml += "cover:\n";

  int idx = 0;
  for (const auto &blind : blinds) {
    if (this->parent_->is_cover_configured(blind.blind_address))
      continue;

    char buf[640];
    snprintf(buf, sizeof(buf),
             "%s"
             "  - platform: elero\n"
             "    blind_address: 0x%06x\n"
             "    channel: %d\n"
             "    remote_address: 0x%06x\n"
             "    name: \"Discovered Blind %d\"\n"
             "    # open_duration: 25s\n"
             "    # close_duration: 22s\n"
             "    hop: 0x%02x\n"
             "    payload_1: 0x%02x\n"
             "    payload_2: 0x%02x\n"
             "    pck_inf1: 0x%02x\n"
             "    pck_inf2: 0x%02x\n"
             "\n",
             blind.params_from_command
                 ? ""
                 : "  # WARNING: params derived from status packet only — press a remote\n"
                   "  # button during scan so command packets can be captured for reliable values.\n",
             blind.blind_address, blind.channel, blind.remote_address, ++idx, blind.hop, blind.payload_1,
             blind.payload_2, blind.pck_inf[0], blind.pck_inf[1]);
    yaml += buf;
  }

  if (idx == 0)
    yaml += "  # All discovered blinds are already configured.\n";

  return yaml;
}

}  // namespace elero
}  // namespace esphome
