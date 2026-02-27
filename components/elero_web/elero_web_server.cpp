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

  // Create WebSocket at /elero/ws
  this->ws_ = new AsyncWebSocket("/elero/ws");
  this->ws_->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                            AwsEventType type, void *arg, uint8_t *data, size_t len) {
    this->on_ws_event(server, client, type, arg, data, len);
  });
  server->addHandler(this->ws_);

  // Serve index at /elero
  server->on("/elero", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handle_index(request);
  });

  // Redirect root to /elero
  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/elero");
  });

  ESP_LOGI(TAG, "Elero Web UI available at /elero (WebSocket at /elero/ws)");
}

void EleroWebServer::loop() {
  if (this->ws_ == nullptr)
    return;

  uint32_t now = millis();

  // Cleanup disconnected clients periodically
  if (now - this->last_cleanup_ms_ >= ELERO_WS_CLEANUP_INTERVAL) {
    this->last_cleanup_ms_ = now;
    this->ws_->cleanupClients();
  }

  // Push incremental updates if any flags are set
  if (this->covers_changed_) {
    this->covers_changed_ = false;
    this->broadcast("covers", this->build_covers_json());
  }

  if (this->discovered_changed_ || this->scan_status_changed_) {
    this->discovered_changed_ = false;
    this->scan_status_changed_ = false;
    this->broadcast("discovered", this->build_discovered_json());
  }

  if (this->logs_changed_) {
    this->logs_changed_ = false;
    std::string logs_json = this->build_logs_json(this->last_log_push_ts_, 20);
    if (!logs_json.empty() && logs_json != "[]") {
      this->broadcast("log", logs_json);
      // Update last push timestamp
      const auto &entries = this->parent_->get_log_entries();
      if (!entries.empty()) {
        this->last_log_push_ts_ = entries.back().timestamp_ms;
      }
    }
  }

  if (this->packets_changed_) {
    this->packets_changed_ = false;
    this->broadcast("packets", this->build_packets_json());
  }

  // Heartbeat: send full state every 5 seconds
  if (now - this->last_heartbeat_ms_ >= ELERO_WS_HEARTBEAT_INTERVAL) {
    this->last_heartbeat_ms_ = now;
    this->broadcast_full_state();
  }
}

void EleroWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero Web Server (WebSocket):");
  ESP_LOGCONFIG(TAG, "  URL: /elero");
  ESP_LOGCONFIG(TAG, "  WebSocket: /elero/ws");
  ESP_LOGCONFIG(TAG, "  Enabled: %s", this->enabled_ ? "yes" : "no");
}

// ─── WebSocket event handler ──────────────────────────────────────────────────

void EleroWebServer::on_ws_event(AsyncWebSocket *server, AsyncWebSocketClient *client,
                                  AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (!this->enabled_) {
    if (type == WS_EVT_CONNECT) {
      client->close(503, "Web UI disabled");
    }
    return;
  }

  switch (type) {
    case WS_EVT_CONNECT:
      ESP_LOGI(TAG, "WebSocket client #%u connected from %s", client->id(),
               client->remoteIP().toString().c_str());
      this->send_full_state(client);
      break;

    case WS_EVT_DISCONNECT:
      ESP_LOGI(TAG, "WebSocket client #%u disconnected", client->id());
      break;

    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      if (info->opcode == WS_TEXT && info->final && info->index == 0 && info->len == len) {
        // Complete text message
        std::string msg((char *)data, len);
        this->handle_ws_message(client, msg);
      }
      break;
    }

    case WS_EVT_ERROR:
      ESP_LOGW(TAG, "WebSocket error from client #%u", client->id());
      break;

    case WS_EVT_PONG:
      break;
  }
}

// ─── Message dispatcher ───────────────────────────────────────────────────────

void EleroWebServer::handle_ws_message(AsyncWebSocketClient *client, const std::string &msg) {
  // Simple JSON parsing - look for "cmd" and other fields
  // Format: {"id": "...", "cmd": "...", ...}

  auto find_str = [&msg](const char *key) -> std::string {
    std::string pattern = std::string("\"") + key + "\":\"";
    size_t pos = msg.find(pattern);
    if (pos == std::string::npos)
      return "";
    pos += pattern.length();
    size_t end = msg.find('"', pos);
    if (end == std::string::npos)
      return "";
    return msg.substr(pos, end - pos);
  };

  auto find_uint = [&msg](const char *key) -> uint32_t {
    std::string pattern = std::string("\"") + key + "\":";
    size_t pos = msg.find(pattern);
    if (pos == std::string::npos)
      return 0;
    pos += pattern.length();
    return (uint32_t)strtoul(msg.c_str() + pos, nullptr, 0);
  };

  std::string id = find_str("id");
  std::string cmd = find_str("cmd");
  const char *id_cstr = id.empty() ? nullptr : id.c_str();

  if (cmd.empty()) {
    this->send_result(client, id_cstr, false, "Missing cmd");
    return;
  }

  // ── Cover/blind control ──
  if (cmd == "cover") {
    std::string address = find_str("address");
    std::string action = find_str("action");
    if (address.empty() || action.empty()) {
      this->send_result(client, id_cstr, false, "Missing address or action");
      return;
    }

    uint32_t addr = (uint32_t)strtoul(address.c_str(), nullptr, 0);
    uint8_t cmd_byte = elero_action_to_command(action.c_str());

    if (cmd_byte == 0xFF) {
      this->send_result(client, id_cstr, false, "Unknown action");
      return;
    }

    // Try configured cover — use perform_action() for same code path as Home Assistant
    const auto &covers = this->parent_->get_configured_covers();
    auto it = covers.find(addr);
    if (it != covers.end()) {
      if (!it->second->perform_action(action.c_str())) {
        this->send_result(client, id_cstr, false, "Unknown action");
        return;
      }
      this->send_result(client, id_cstr, true);
      this->notify_covers_changed();
      return;
    }

    // Try runtime blind
    if (this->parent_->send_runtime_command(addr, cmd_byte)) {
      this->send_result(client, id_cstr, true);
    } else {
      this->send_result(client, id_cstr, false, "Cover not found");
    }
    return;
  }

  // ── Cover settings ──
  if (cmd == "settings") {
    std::string address = find_str("address");
    if (address.empty()) {
      this->send_result(client, id_cstr, false, "Missing address");
      return;
    }

    uint32_t addr = (uint32_t) strtoul(address.c_str(), nullptr, 0);
    uint32_t open_dur = find_uint("open_duration");
    uint32_t close_dur = find_uint("close_duration");
    uint32_t poll_intvl = find_uint("poll_interval");

    // Try configured cover first, then runtime blind
    // Note: apply_runtime_settings treats 0 as "keep existing"
    const auto &covers = this->parent_->get_configured_covers();
    auto it = covers.find(addr);
    if (it != covers.end()) {
      it->second->apply_runtime_settings(open_dur, close_dur, poll_intvl);
      this->send_result(client, id_cstr, true);
      this->notify_covers_changed();
      return;
    }

    if (this->parent_->update_runtime_blind_settings(addr, open_dur, close_dur, poll_intvl)) {
      this->send_result(client, id_cstr, true);
      this->notify_covers_changed();
    } else {
      this->send_result(client, id_cstr, false, "Cover not found");
    }
    return;
  }

  // ── Discovery ──
  if (cmd == "scan_start") {
    if (this->parent_->is_scanning()) {
      this->send_result(client, id_cstr, false, "Already scanning");
      return;
    }
    this->parent_->start_scan();  // Clears discovered + starts
    this->send_result(client, id_cstr, true);
    this->notify_scan_status_changed();
    return;
  }

  if (cmd == "scan_stop") {
    if (!this->parent_->is_scanning()) {
      this->send_result(client, id_cstr, false, "No scan running");
      return;
    }
    this->parent_->stop_scan();
    this->send_result(client, id_cstr, true);
    this->notify_scan_status_changed();
    return;
  }

  if (cmd == "adopt") {
    std::string address = find_str("address");
    std::string name = find_str("name");
    if (address.empty()) {
      this->send_result(client, id_cstr, false, "Missing address");
      return;
    }

    uint32_t addr = (uint32_t) strtoul(address.c_str(), nullptr, 0);
    if (!this->parent_->adopt_blind_by_address(addr, name)) {
      this->send_result(client, id_cstr, false, "Not found or already configured");
      return;
    }
    this->send_result(client, id_cstr, true);
    this->notify_covers_changed();
    this->notify_discovered_changed();
    return;
  }

  if (cmd == "runtime_remove") {
    std::string address = find_str("address");
    if (address.empty()) {
      this->send_result(client, id_cstr, false, "Missing address");
      return;
    }

    uint32_t addr = (uint32_t)strtoul(address.c_str(), nullptr, 0);
    if (this->parent_->remove_runtime_blind(addr)) {
      this->send_result(client, id_cstr, true);
      this->notify_covers_changed();
    } else {
      this->send_result(client, id_cstr, false, "Runtime blind not found");
    }
    return;
  }

  // ── Logs & packets ──
  if (cmd == "log_start") {
    this->parent_->set_log_capture(true);
    this->send_result(client, id_cstr, true);
    return;
  }

  if (cmd == "log_stop") {
    this->parent_->set_log_capture(false);
    this->send_result(client, id_cstr, true);
    return;
  }

  if (cmd == "log_clear") {
    this->parent_->clear_log_entries();
    this->last_log_push_ts_ = 0;
    this->send_result(client, id_cstr, true);
    return;
  }

  if (cmd == "dump_start") {
    if (this->parent_->is_packet_dump_active()) {
      this->send_result(client, id_cstr, false, "Dump already running");
      return;
    }
    this->parent_->start_packet_dump();  // Clears + starts
    this->send_result(client, id_cstr, true);
    return;
  }

  if (cmd == "dump_stop") {
    if (!this->parent_->is_packet_dump_active()) {
      this->send_result(client, id_cstr, false, "No dump running");
      return;
    }
    this->parent_->stop_packet_dump();
    this->send_result(client, id_cstr, true);
    return;
  }

  if (cmd == "dump_clear") {
    this->parent_->clear_raw_packets();
    this->send_result(client, id_cstr, true);
    return;
  }

  // ── Configuration ──
  if (cmd == "set_frequency") {
    std::string f2 = find_str("freq2");
    std::string f1 = find_str("freq1");
    std::string f0 = find_str("freq0");
    if (f2.empty() || f1.empty() || f0.empty()) {
      this->send_result(client, id_cstr, false, "Missing freq2, freq1 or freq0");
      return;
    }

    auto parse_byte = [](const std::string &s, uint8_t &out) -> bool {
      char *end;
      unsigned long v = strtoul(s.c_str(), &end, 0);
      if (end == s.c_str() || v > 0xFF)
        return false;
      out = (uint8_t)v;
      return true;
    };

    uint8_t freq2, freq1, freq0;
    if (!parse_byte(f2, freq2) || !parse_byte(f1, freq1) || !parse_byte(f0, freq0)) {
      this->send_result(client, id_cstr, false, "Invalid frequency value");
      return;
    }

    this->parent_->reinit_frequency(freq2, freq1, freq0);
    this->send_result(client, id_cstr, true);
    return;
  }

  // ── YAML request ──
  if (cmd == "get_yaml") {
    this->send_yaml(client, id_cstr);
    return;
  }

  // ── Get state ──
  if (cmd == "get_state") {
    this->send_full_state(client);
    return;
  }

  this->send_result(client, id_cstr, false, "Unknown command");
}

// ─── Notification methods ─────────────────────────────────────────────────────

void EleroWebServer::notify_covers_changed() {
  this->covers_changed_ = true;
}

void EleroWebServer::notify_discovered_changed() {
  this->discovered_changed_ = true;
}

void EleroWebServer::notify_log_entry() {
  this->logs_changed_ = true;
}

void EleroWebServer::notify_packet_received() {
  this->packets_changed_ = true;
}

void EleroWebServer::notify_scan_status_changed() {
  this->scan_status_changed_ = true;
}

// ─── Broadcast helpers ────────────────────────────────────────────────────────

void EleroWebServer::broadcast(const char *type, const std::string &json) {
  if (this->ws_ == nullptr || this->ws_->count() == 0)
    return;

  std::string msg = "{\"type\":\"";
  msg += type;
  msg += "\",\"data\":";
  msg += json;
  msg += "}";

  this->ws_->textAll(msg.c_str(), msg.length());
}

void EleroWebServer::broadcast_full_state() {
  if (this->ws_ == nullptr || this->ws_->count() == 0)
    return;

  std::string msg = "{\"type\":\"state\",\"data\":";
  msg += this->build_full_state_json();
  msg += "}";

  this->ws_->textAll(msg.c_str(), msg.length());
}

void EleroWebServer::send_full_state(AsyncWebSocketClient *client) {
  if (client == nullptr)
    return;

  std::string msg = "{\"type\":\"state\",\"data\":";
  msg += this->build_full_state_json();
  msg += "}";

  client->text(msg.c_str(), msg.length());
}

void EleroWebServer::send_result(AsyncWebSocketClient *client, const char *id, bool ok, const char *error) {
  if (client == nullptr)
    return;

  std::string msg = "{\"type\":\"result\"";
  if (id != nullptr && id[0] != '\0') {
    msg += ",\"id\":\"";
    msg += id;
    msg += "\"";
  }
  msg += ",\"ok\":";
  msg += ok ? "true" : "false";
  if (!ok && error != nullptr) {
    msg += ",\"error\":\"";
    msg += json_escape(error);
    msg += "\"";
  }
  msg += "}";

  client->text(msg.c_str(), msg.length());
}

void EleroWebServer::send_yaml(AsyncWebSocketClient *client, const char *id) {
  if (client == nullptr)
    return;

  std::string yaml = this->build_yaml();
  std::string msg = "{\"type\":\"yaml\"";
  if (id != nullptr && id[0] != '\0') {
    msg += ",\"id\":\"";
    msg += id;
    msg += "\"";
  }
  msg += ",\"data\":\"";
  msg += json_escape(yaml);
  msg += "\"}";

  client->text(msg.c_str(), msg.length());
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
           esc_app_name.c_str(), (unsigned long)millis(), this->parent_->get_freq2(), this->parent_->get_freq1(),
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
             elero_state_to_string(blind->get_last_state_raw()), (unsigned long)blind->get_last_seen_ms(),
             blind->get_last_rssi(), (int)blind->get_channel(), blind->get_remote_address(),
             (unsigned long)blind->get_poll_interval_ms(), (unsigned long)blind->get_open_duration_ms(),
             (unsigned long)blind->get_close_duration_ms(), blind->get_supports_tilt() ? "true" : "false");
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
             rb.blind_address, esc_name.c_str(), elero_state_to_string(rb.last_state), (unsigned long)rb.last_seen_ms,
             rb.last_rssi, (int)rb.channel, rb.remote_address, (unsigned long)rb.poll_intvl_ms,
             (unsigned long)rb.open_duration_ms, (unsigned long)rb.close_duration_ms);
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
             blind.pck_inf[0], blind.pck_inf[1], (unsigned long)blind.last_seen,
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
             (unsigned long)e.timestamp_ms, lv, level_strs[lv], e.tag, msg_esc.c_str());
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
  snprintf(cnt_buf, sizeof(cnt_buf), "%d", (int)packets.size());
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
             (unsigned long)pkt.timestamp_ms, pkt.fifo_len, pkt.valid ? "true" : "false", pkt.reject_reason, hex_buf);
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

// ─── HTTP handlers (minimal) ──────────────────────────────────────────────────

void EleroWebServer::handle_index(AsyncWebServerRequest *request) {
  if (!this->enabled_) {
    request->send(503, "text/plain", "Web UI disabled");
    return;
  }
  request->send(200, "text/html", ELERO_WEB_UI_HTML);
}

}  // namespace elero
}  // namespace esphome
