#include "elero_web_server.h"
#include "elero_web_ui.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace esphome {
namespace elero {

static const char *const TAG = "elero.web_server";

// ─── JSON helpers ─────────────────────────────────────────────────────────────

static std::string json_escape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '"') {
      out += "\\\"";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }
  return out;
}

// Simple JSON field extractors for parsing POST bodies
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

static uint32_t json_find_uint(const std::string &json, const char *key) {
  std::string pattern = std::string("\"") + key + "\":";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos)
    return 0;
  pos += pattern.length();
  return (uint32_t) strtoul(json.c_str() + pos, nullptr, 0);
}

// ─── Setup / config ───────────────────────────────────────────────────────────

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

  auto server = this->base_->get_server();
  if (!server) {
    ESP_LOGE(TAG, "Failed to get web server instance");
    this->mark_failed();
    return;
  }

  // SSE endpoint: send full state on connect
  this->events_.onConnect([this](AsyncEventSourceClient *client) {
    if (!this->enabled_) {
      return;
    }
    ESP_LOGI(TAG, "SSE client connected from %s", client->client()->remoteIP().toString().c_str());
    std::string state = this->build_full_state_json();
    client->send(state.c_str(), "state", millis());
  });
  server->addHandler(&this->events_);

  // ─── GET routes ─────────────────────────────────────────────────────────────

  // Serve index at /elero
  server->on("/elero", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_index(request);
  });

  // Redirect root to /elero
  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/elero");
  });

  // GET state (for manual refresh)
  server->on("/elero/api/state", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_get_state(request);
  });

  // GET YAML
  server->on("/elero/api/yaml", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_get_yaml(request);
  });

  // ─── POST routes (no body) ──────────────────────────────────────────────────

  server->on("/elero/api/scan/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handle_post_scan_start(request);
  });

  server->on("/elero/api/scan/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handle_post_scan_stop(request);
  });

  server->on("/elero/api/log/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handle_post_log_start(request);
  });

  server->on("/elero/api/log/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handle_post_log_stop(request);
  });

  server->on("/elero/api/log/clear", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handle_post_log_clear(request);
  });

  server->on("/elero/api/dump/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handle_post_dump_start(request);
  });

  server->on("/elero/api/dump/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handle_post_dump_stop(request);
  });

  server->on("/elero/api/dump/clear", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handle_post_dump_clear(request);
  });

  // ─── POST routes (with JSON body) ───────────────────────────────────────────

  // Cover command: {"address":"0x...","action":"up"}
  server->on(
      "/elero/api/cover", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        // Response sent in body handler
      },
      nullptr,
      [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        this->handle_post_cover(request, data, len);
      });

  // Settings: {"address":"0x...","open_duration":25000,...}
  server->on(
      "/elero/api/settings", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      nullptr,
      [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        this->handle_post_settings(request, data, len);
      });

  // Adopt: {"address":"0x...","name":"..."}
  server->on(
      "/elero/api/adopt", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      nullptr,
      [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        this->handle_post_adopt(request, data, len);
      });

  // Runtime remove: {"address":"0x..."}
  server->on(
      "/elero/api/runtime/remove", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      nullptr,
      [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        this->handle_post_runtime_remove(request, data, len);
      });

  // Frequency: {"freq2":"0x21","freq1":"0x71","freq0":"0x7a"}
  server->on(
      "/elero/api/frequency", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      nullptr,
      [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        this->handle_post_frequency(request, data, len);
      });

  ESP_LOGI(TAG, "Elero Web UI available at /elero (SSE at /elero/events)");
}

void EleroWebServer::loop() {
  uint32_t now = millis();

  // Push incremental updates if any flags are set
  if (this->covers_changed_) {
    this->covers_changed_ = false;
    this->broadcast_event("covers", this->build_covers_json());
  }

  if (this->discovered_changed_ || this->scan_status_changed_) {
    this->discovered_changed_ = false;
    this->scan_status_changed_ = false;
    this->broadcast_event("discovered", this->build_discovered_json());
  }

  if (this->logs_changed_) {
    this->logs_changed_ = false;
    std::string logs_json = this->build_logs_json(this->last_log_push_ts_, 20);
    if (!logs_json.empty() && logs_json != "[]") {
      this->broadcast_event("log", logs_json);
      // Update last push timestamp
      const auto &entries = this->parent_->get_log_entries();
      if (!entries.empty()) {
        this->last_log_push_ts_ = entries.back().timestamp_ms;
      }
    }
  }

  if (this->packets_changed_) {
    this->packets_changed_ = false;
    this->broadcast_event("packets", this->build_packets_json());
  }

  // Heartbeat: send full state periodically
  if (now - this->last_heartbeat_ms_ >= ELERO_SSE_HEARTBEAT_INTERVAL) {
    this->last_heartbeat_ms_ = now;
    this->broadcast_event("state", this->build_full_state_json());
  }
}

void EleroWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero Web Server (SSE):");
  ESP_LOGCONFIG(TAG, "  URL: /elero");
  ESP_LOGCONFIG(TAG, "  SSE: /elero/events");
  ESP_LOGCONFIG(TAG, "  Enabled: %s", this->enabled_ ? "yes" : "no");
}

// ─── Notification methods ─────────────────────────────────────────────────────

void EleroWebServer::notify_covers_changed() { this->covers_changed_ = true; }

void EleroWebServer::notify_discovered_changed() { this->discovered_changed_ = true; }

void EleroWebServer::notify_log_entry() { this->logs_changed_ = true; }

void EleroWebServer::notify_packet_received() { this->packets_changed_ = true; }

void EleroWebServer::notify_scan_status_changed() { this->scan_status_changed_ = true; }

// ─── SSE broadcast helper ─────────────────────────────────────────────────────

void EleroWebServer::broadcast_event(const char *event_type, const std::string &json) {
  if (!this->enabled_ || this->events_.count() == 0)
    return;
  this->events_.send(json.c_str(), event_type, millis());
}

// ─── HTTP response helpers ────────────────────────────────────────────────────

void EleroWebServer::send_json_ok(AsyncWebServerRequest *request) {
  request->send(200, "application/json", "{\"ok\":true}");
}

void EleroWebServer::send_json_error(AsyncWebServerRequest *request, int code, const char *error) {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", error);
  request->send(code, "application/json", buf);
}

// ─── HTTP GET handlers ────────────────────────────────────────────────────────

void EleroWebServer::handle_index(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    request->send(503, "text/plain", "Web UI disabled");
    return;
  }
  request->send(200, "text/html", ELERO_WEB_UI_HTML);
}

void EleroWebServer::handle_get_state(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    request->send(503, "application/json", "{\"ok\":false,\"error\":\"disabled\"}");
    return;
  }
  std::string json = this->build_full_state_json();
  request->send(200, "application/json", json.c_str());
}

void EleroWebServer::handle_get_yaml(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    request->send(503, "text/plain", "Web UI disabled");
    return;
  }
  std::string yaml = this->build_yaml();
  request->send(200, "text/plain", yaml.c_str());
}

// ─── HTTP POST handlers (no body) ─────────────────────────────────────────────

void EleroWebServer::handle_post_scan_start(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }
  if (!this->parent_->start_scan()) {
    this->send_json_error(request, 409, "Already scanning");
    return;
  }
  this->notify_scan_status_changed();
  this->send_json_ok(request);
}

void EleroWebServer::handle_post_scan_stop(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }
  if (!this->parent_->stop_scan()) {
    this->send_json_error(request, 409, "No scan running");
    return;
  }
  this->notify_scan_status_changed();
  this->send_json_ok(request);
}

void EleroWebServer::handle_post_log_start(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }
  this->parent_->set_log_capture(true);
  this->send_json_ok(request);
}

void EleroWebServer::handle_post_log_stop(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }
  this->parent_->set_log_capture(false);
  this->send_json_ok(request);
}

void EleroWebServer::handle_post_log_clear(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }
  this->parent_->clear_log_entries();
  this->last_log_push_ts_ = 0;
  this->send_json_ok(request);
}

void EleroWebServer::handle_post_dump_start(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }
  if (!this->parent_->start_packet_dump()) {
    this->send_json_error(request, 409, "Dump already running");
    return;
  }
  this->send_json_ok(request);
}

void EleroWebServer::handle_post_dump_stop(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }
  if (!this->parent_->stop_packet_dump()) {
    this->send_json_error(request, 409, "No dump running");
    return;
  }
  this->send_json_ok(request);
}

void EleroWebServer::handle_post_dump_clear(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }
  this->parent_->clear_raw_packets();
  this->send_json_ok(request);
}

// ─── HTTP POST handlers (with JSON body) ──────────────────────────────────────

void EleroWebServer::handle_post_cover(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }

  std::string body((char *) data, len);
  std::string address = json_find_str(body, "address");
  std::string action = json_find_str(body, "action");

  if (address.empty() || action.empty()) {
    this->send_json_error(request, 400, "Missing address or action");
    return;
  }

  uint32_t addr = (uint32_t) strtoul(address.c_str(), nullptr, 0);
  uint8_t cmd_byte = elero_action_to_command(action.c_str());

  if (cmd_byte == 0xFF) {
    this->send_json_error(request, 400, "Unknown action");
    return;
  }

  // Try configured cover first
  const auto &covers = this->parent_->get_configured_covers();
  auto it = covers.find(addr);
  if (it != covers.end()) {
    if (!it->second->perform_action(action.c_str())) {
      this->send_json_error(request, 400, "Unknown action");
      return;
    }
    this->notify_covers_changed();
    this->send_json_ok(request);
    return;
  }

  // Try runtime blind
  if (this->parent_->send_runtime_command(addr, cmd_byte)) {
    this->send_json_ok(request);
  } else {
    this->send_json_error(request, 404, "Cover not found");
  }
}

void EleroWebServer::handle_post_settings(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }

  std::string body((char *) data, len);
  std::string address = json_find_str(body, "address");

  if (address.empty()) {
    this->send_json_error(request, 400, "Missing address");
    return;
  }

  uint32_t addr = (uint32_t) strtoul(address.c_str(), nullptr, 0);
  uint32_t open_dur = json_find_uint(body, "open_duration");
  uint32_t close_dur = json_find_uint(body, "close_duration");
  uint32_t poll_intvl = json_find_uint(body, "poll_interval");

  // Try configured cover first
  const auto &covers = this->parent_->get_configured_covers();
  auto it = covers.find(addr);
  if (it != covers.end()) {
    it->second->apply_runtime_settings(open_dur, close_dur, poll_intvl);
    this->notify_covers_changed();
    this->send_json_ok(request);
    return;
  }

  // Try runtime blind
  if (this->parent_->update_runtime_blind_settings(addr, open_dur, close_dur, poll_intvl)) {
    this->notify_covers_changed();
    this->send_json_ok(request);
  } else {
    this->send_json_error(request, 404, "Cover not found");
  }
}

void EleroWebServer::handle_post_adopt(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }

  std::string body((char *) data, len);
  std::string address = json_find_str(body, "address");
  std::string name = json_find_str(body, "name");

  if (address.empty()) {
    this->send_json_error(request, 400, "Missing address");
    return;
  }

  uint32_t addr = (uint32_t) strtoul(address.c_str(), nullptr, 0);
  if (!this->parent_->adopt_blind_by_address(addr, name)) {
    this->send_json_error(request, 409, "Not found or already configured");
    return;
  }

  this->notify_covers_changed();
  this->notify_discovered_changed();
  this->send_json_ok(request);
}

void EleroWebServer::handle_post_runtime_remove(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }

  std::string body((char *) data, len);
  std::string address = json_find_str(body, "address");

  if (address.empty()) {
    this->send_json_error(request, 400, "Missing address");
    return;
  }

  uint32_t addr = (uint32_t) strtoul(address.c_str(), nullptr, 0);
  if (this->parent_->remove_runtime_blind(addr)) {
    this->notify_covers_changed();
    this->send_json_ok(request);
  } else {
    this->send_json_error(request, 404, "Runtime blind not found");
  }
}

void EleroWebServer::handle_post_frequency(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
  if (!this->enabled_) {
    this->send_json_error(request, 503, "disabled");
    return;
  }

  std::string body((char *) data, len);
  std::string f2 = json_find_str(body, "freq2");
  std::string f1 = json_find_str(body, "freq1");
  std::string f0 = json_find_str(body, "freq0");

  if (f2.empty() || f1.empty() || f0.empty()) {
    this->send_json_error(request, 400, "Missing freq2, freq1 or freq0");
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
    this->send_json_error(request, 400, "Invalid frequency value");
    return;
  }

  this->parent_->reinit_frequency(freq2, freq1, freq0);
  this->send_json_ok(request);
}

// ─── JSON builders ────────────────────────────────────────────────────────────

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
