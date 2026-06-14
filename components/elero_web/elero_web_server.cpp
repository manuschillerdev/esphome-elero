#include "elero_web_server.h"
#include "elero_web_ui.h"
#include "../elero/elero_packet.h"
#include "../elero/elero_strings.h"
#include "../elero/nvs_config.h"
#include "../elero/state_snapshot.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/logger/logger.h"
#include "esphome/components/json/json_util.h"
#ifdef USE_ESP32
#include "esp_mac.h"
#endif
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <set>

namespace esphome {
namespace elero {

static const char *const TAG = "elero.web";

// Global instance pointer for Mongoose's C callback
static EleroWebServer *g_server = nullptr;

static constexpr char INDEX_STREAM_MARKER = 'H';
static constexpr size_t INDEX_STREAM_OFFSET_POS = 8;
static constexpr size_t INDEX_STREAM_CHUNK_SIZE = 1024;
static constexpr size_t INDEX_STREAM_MAX_QUEUED = 4096;
static_assert(INDEX_STREAM_OFFSET_POS + sizeof(size_t) <= MG_DATA_SIZE,
              "mg_connection::data must fit index stream state");

static size_t load_index_stream_offset(struct mg_connection *c) {
  size_t offset = 0;
  memcpy(&offset, &c->data[INDEX_STREAM_OFFSET_POS], sizeof(offset));
  return offset;
}

static void store_index_stream_offset(struct mg_connection *c, size_t offset) {
  memcpy(&c->data[INDEX_STREAM_OFFSET_POS], &offset, sizeof(offset));
}

static void stream_index_body(struct mg_connection *c) {
  if (c == nullptr || c->is_closing || c->data[0] != INDEX_STREAM_MARKER) return;

  size_t offset = load_index_stream_offset(c);
  while (offset < ELERO_WEB_UI_GZ_LEN && c->send.len < INDEX_STREAM_MAX_QUEUED) {
    const size_t queued_space = INDEX_STREAM_MAX_QUEUED - c->send.len;
    const size_t remaining = ELERO_WEB_UI_GZ_LEN - offset;
    const size_t chunk_len = std::min({INDEX_STREAM_CHUNK_SIZE, queued_space, remaining});
    if (chunk_len == 0) break;

    if (!mg_send(c, ELERO_WEB_UI_GZ + offset, chunk_len)) {
      ESP_LOGW(TAG, "Failed to queue Web UI chunk at offset %zu", offset);
      c->is_closing = 1;
      return;
    }
    offset += chunk_len;
    store_index_stream_offset(c, offset);
  }

  if (offset >= ELERO_WEB_UI_GZ_LEN) {
    c->data[0] = 0;
    c->is_resp = 0;
    c->is_draining = 1;
  }
}

static uint32_t derive_default_virtual_remote_address() {
#ifdef USE_ESP32
  uint8_t mac[6] = {0};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    return 0x000001;
  }
  uint32_t addr = (static_cast<uint32_t>(mac[3]) << 16) |
                  (static_cast<uint32_t>(mac[4]) << 8) |
                  static_cast<uint32_t>(mac[5]);
  return addr != 0 ? addr : 0x000001;
#else
  return 0x000001;
#endif
}

/// Parse hex string ("0xNN" or decimal) to uint8_t, returning default if missing/null
static uint8_t parse_hex_or(JsonObject root, const char *key, uint8_t def) {
  if (!root[key]) return def;
  if (root[key].is<const char *>()) {
    return (uint8_t) strtoul(root[key].as<const char *>(), nullptr, 0);
  }
  return root[key].as<uint8_t>();
}

/// Parse hex string ("0xNNNNNN" or decimal) to uint32_t, returning 0 if missing
static uint32_t parse_hex32(JsonObject root, const char *key) {
  if (!root[key]) return 0;
  if (root[key].is<const char *>()) {
    return (uint32_t) strtoul(root[key].as<const char *>(), nullptr, 0);
  }
  return root[key].as<uint32_t>();
}

/// Parse "cover"/"light"/"remote" into DeviceType enum. Returns false if invalid.
static bool parse_device_type(const char *str, DeviceType &out) {
  if (str == nullptr) return false;
  if (strcmp(str, device_type_str(DeviceType::COVER)) == 0) { out = DeviceType::COVER; return true; }
  if (strcmp(str, device_type_str(DeviceType::LIGHT)) == 0) { out = DeviceType::LIGHT; return true; }
  if (strcmp(str, device_type_str(DeviceType::REMOTE)) == 0) { out = DeviceType::REMOTE; return true; }
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

  // Register as log listener to forward logs to WebSocket clients
  if (logger::global_logger != nullptr) {
    logger::global_logger->add_log_listener(this);
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

  if (!this->enabled_ || this->ws_clients_.empty() || this->parent_ == nullptr) {
    return;
  }

  const auto &learn_in = this->parent_->learn_in();
  bool active = this->parent_->is_learn_in_active();
  bool busy = learn_in.is_busy();
  uint32_t src = learn_in.src_addr();
  uint8_t channel = learn_in.channel();
  uint8_t cmd = learn_in.programming_cmd();
  LearnInState state = this->parent_->learn_in_state();

  if (state != this->last_learn_in_state_ ||
      active != this->last_learn_in_active_ ||
      busy != this->last_learn_in_busy_ ||
      src != this->last_learn_in_src_ ||
      channel != this->last_learn_in_channel_ ||
      cmd != this->last_learn_in_cmd_) {
    this->last_learn_in_state_ = state;
    this->last_learn_in_active_ = active;
    this->last_learn_in_busy_ = busy;
    this->last_learn_in_src_ = src;
    this->last_learn_in_channel_ = channel;
    this->last_learn_in_cmd_ = cmd;
    this->ws_broadcast("learn_in_state", this->build_learn_in_state_json_());
  }
}

void EleroWebServer::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero Web Server:");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
  ESP_LOGCONFIG(TAG, "  URL: /elero");
  ESP_LOGCONFIG(TAG, "  WebSocket: /elero/ws");
}

// ═══════════════════════════════════════════════════════════════════════════════
// OutputAdapter — Device CRUD Events
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::on_device_added(const Device &dev) {
  if (this->ws_clients_.empty() || !this->enabled_)
    return;
  this->ws_broadcast("device_upserted", this->build_device_upserted_json_(dev));
}

void EleroWebServer::on_config_changed(const Device &dev) {
  if (this->ws_clients_.empty() || !this->enabled_)
    return;
  this->ws_broadcast("device_upserted", this->build_device_upserted_json_(dev));
}

void EleroWebServer::on_device_removed(const Device &dev) {
  if (this->ws_clients_.empty() || !this->enabled_)
    return;
  std::string payload = json::build_json([&](JsonObject root) {
    root["address"] = hex_str(dev.config.dst_address);
    root["device_type"] = device_type_str(dev.config.type);
  });
  this->ws_broadcast("device_removed", payload);
}

void EleroWebServer::on_group_upserted(const NvsGroupConfig &group) {
  if (this->ws_clients_.empty() || !this->enabled_)
    return;
  this->ws_broadcast("group_upserted", this->build_group_json_(group));
}

void EleroWebServer::on_group_removed(const char *id) {
  if (this->ws_clients_.empty() || !this->enabled_)
    return;
  std::string payload = json::build_json([&](JsonObject root) {
    root["id"] = id == nullptr ? "" : id;
  });
  this->ws_broadcast("group_removed", payload);
}

// ═══════════════════════════════════════════════════════════════════════════════
// State Changed (OutputAdapter — optimistic updates)
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::on_state_changed(const Device &dev, uint16_t /*changes*/) {
  if (this->ws_clients_.empty() || !this->enabled_)
    return;

  uint32_t now = millis();
  std::string payload;

  if (dev.is_cover()) {
    auto snap = compute_cover_snapshot(dev, now);
    payload = json::build_json([&](JsonObject root) {
      root["address"] = hex_str(dev.config.dst_address);
      root["device_type"] = device_type_str(dev.config.type);
      snap.to_json(root);
    });
  } else if (dev.is_light()) {
    auto snap = compute_light_snapshot(dev, now);
    payload = json::build_json([&](JsonObject root) {
      root["address"] = hex_str(dev.config.dst_address);
      root["device_type"] = device_type_str(dev.config.type);
      snap.to_json(root);
    });
  } else {
    return;
  }

  this->ws_broadcast("state_changed", payload);
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

  // Strip ANSI escape sequences — ESPHome LogBuffer embeds color codes
  // (e.g. \033[0;36m) which are invalid control characters in JSON
  std::string msg;
  msg.reserve(message_len);
  bool in_escape = false;
  for (size_t i = 0; i < message_len; i++) {
    char c = message[i];
    if (!in_escape) {
      if (c == '\033') {
        in_escape = true;
      } else {
        msg += c;
      }
    } else if (isalpha(c)) {
      in_escape = false;
    }
  }

  std::string log_json = json::build_json([&](JsonObject root) {
    root["t"] = millis();
    root["level"] = level;
    root["tag"] = tag;
    root["msg"] = msg;
  });

  this->ws_broadcast("log", log_json);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mongoose Event Handler
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::event_handler(struct mg_connection *c, int ev, void *ev_data) {
  auto *self = g_server;
  if (self == nullptr)
    return;

  if ((ev == MG_EV_POLL || ev == MG_EV_WRITE) && c->data[0] == INDEX_STREAM_MARKER) {
    stream_index_body(c);
  }

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
  c->data[0] = INDEX_STREAM_MARKER;
  store_index_stream_offset(c, 0);
  mg_printf(c,
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Content-Encoding: gzip\r\n"
      "Connection: close\r\n"
      "Content-Length: %lu\r\n\r\n",
      (unsigned long) ELERO_WEB_UI_GZ_LEN);
  stream_index_body(c);
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
    this->ws_send(c, "learn_in_state", this->build_learn_in_state_json_());
  }
}

void EleroWebServer::handle_ws_message(struct mg_connection *c, struct mg_ws_message *wm) {
  if (!this->enabled_)
    return;

  std::string msg(wm->data.buf, wm->data.len);

  json::parse_json(msg, [this, c, &msg](JsonObject root) -> bool {
    std::string type = root["type"] | "";

    if (type == "cmd") {
      const char *address = root["address"];
      const char *action_str = root["action"];
      if (address == nullptr || action_str == nullptr) return false;

      uint32_t addr = (uint32_t) strtoul(address, nullptr, 0);
      auto *registry = this->parent_->get_registry();
      if (registry == nullptr) return false;

      Device *dev = registry->find(addr);
      if (dev == nullptr) {
        ESP_LOGW(TAG, "Command for unknown address 0x%06x", addr);
        return true;
      }

      uint8_t cmd_byte = elero_action_to_command(action_str);
      if (cmd_byte == packet::command::INVALID) {
        ESP_LOGW(TAG, "Unknown action: %s", action_str);
        return true;
      }

      this->dispatch_device_command_(*dev, cmd_byte);
      return true;
    }

    if (type == "upsert_device") { this->handle_upsert_device_(c, root); return true; }
    if (type == "remove_device") { this->handle_remove_device_(c, root); return true; }
    if (type == "upsert_group") { this->handle_upsert_group_(c, root); return true; }
    if (type == "remove_group") { this->handle_remove_group_(c, root); return true; }
    if (type == "group_cmd") { this->handle_group_command_(c, root); return true; }
    if (type == "set_hub_config") { this->handle_set_hub_config_(c, root); return true; }
    if (type == "export_config") { this->handle_export_config_(c, root); return true; }
    if (type == "import_config") { this->handle_import_config_(c, root); return true; }
    if (type == "learn_in_start") { this->handle_learn_in_start_(c, root); return true; }
    if (type == "learn_in_confirm_up") { this->handle_learn_in_confirm_up_(c); return true; }
    if (type == "learn_in_confirm_down") { this->handle_learn_in_confirm_down_(c); return true; }
    if (type == "learn_in_cancel") { this->handle_learn_in_cancel_(c); return true; }
    if (type == "restart") { App.safe_reboot(); return true; }

    if (type == "raw") {
      uint32_t dst_addr = parse_hex32(root, "dst_address");
      uint32_t src_addr = parse_hex32(root, "src_address");
      uint8_t channel = parse_hex_or(root, "channel", 0);
      uint8_t raw_command = parse_hex_or(root, "command", 0);
      uint8_t msg_type = parse_hex_or(root, "msg_type", packet::msg_type::COMMAND);

      // Button broadcasts (0x44) don't need dst_address — they use channel addressing
      bool is_broadcast = (msg_type == packet::msg_type::BUTTON);
      if (src_addr == 0 || (!is_broadcast && dst_addr == 0)) {
        ESP_LOGW(TAG, "Raw TX missing required fields");
        return false;
      }

      // Route through registry for known devices (non-blocking, coordinated TX)
      auto *registry = this->parent_->get_registry();
      if (registry != nullptr) {
        Device *dev = registry->find(dst_addr);
        if (dev != nullptr) {
          this->dispatch_device_command_(*dev, raw_command);
          return true;
        }
      }

      // Unknown address → raw TX (blocking, debug only)
      uint8_t payload_1 = parse_hex_or(root, "payload_1", packet::defaults::PAYLOAD_1);
      uint8_t payload_2 = parse_hex_or(root, "payload_2", packet::defaults::PAYLOAD_2);
      uint8_t type2_val = parse_hex_or(root, "type2", packet::defaults::TYPE2);
      uint8_t hop = parse_hex_or(root, "hop", packet::defaults::HOP);

      bool success = this->parent_->send_raw_command(
          dst_addr, src_addr, channel, raw_command,
          payload_1, payload_2, msg_type, type2_val, hop);
      ESP_LOGI(TAG, "Raw TX to 0x%06x cmd=0x%02x: %s", dst_addr, raw_command, success ? "OK" : "FAIL");
      return true;
    }

    if (!type.empty()) {
      ESP_LOGW(TAG, "Unknown WebSocket message type: %s", type.c_str());
    }
    return false;
  });
}

// ═══════════════════════════════════════════════════════════════════════════════
// WebSocket Helpers
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::ws_send(struct mg_connection *c, const char *event, const std::string &data) {
  if (c == nullptr || c->is_closing)
    return;
  std::string ws_msg = json::build_json([&](JsonObject root) {
    root["event"] = event;
    root["data"] = serialized(data);
  });
  mg_ws_send(c, ws_msg.c_str(), ws_msg.size(), WEBSOCKET_OP_TEXT);
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
  return json::build_json([this](JsonObject root) {
    auto *registry = this->parent_->get_registry();
    bool has_nvs = registry != nullptr && registry->is_nvs_enabled();

    // hub — gateway identity and operating mode
    JsonObject hub = root["hub"].to<JsonObject>();
    hub["device"] = App.get_name();
    hub["version"] = this->parent_->get_version();
    hub["mode"] = registry ? hub_mode_str(registry->hub_mode()) : "native";
    hub["crud"] = has_nvs;
    // Assign std::string by value — ArduinoJson copies, so we don't depend on
    // the registry's std::string outliving the document.
    hub["name"] = registry ? registry->hub_display_name() : App.get_name();
    hub["default_src_address"] = hex_str(derive_default_virtual_remote_address());

    // radio — RF hardware configuration and capabilities
    auto *drv = this->parent_->get_driver();
    JsonObject radio = root["radio"].to<JsonObject>();
    radio["chipset"] = drv ? drv->radio_name() : "unknown";
    radio["rx_sensitivity"] = drv ? drv->rx_sensitivity_dbm() : -104;
    JsonObject freq = radio["freq"].to<JsonObject>();
    freq["freq2"] = hex_str8(this->parent_->get_freq2());
    freq["freq1"] = hex_str8(this->parent_->get_freq1());
    freq["freq0"] = hex_str8(this->parent_->get_freq0());

    JsonArray blinds = root["blinds"].to<JsonArray>();
    JsonArray lights_arr = root["lights"].to<JsonArray>();
    JsonArray remotes_arr = root["remotes"].to<JsonArray>();
    JsonArray groups_arr = root["groups"].to<JsonArray>();
    std::set<uint32_t> remote_addrs;

    if (registry != nullptr) {
      uint32_t now = millis();

      registry->for_each_active(DeviceType::COVER, [&](const Device &dev) {
        auto snap = compute_cover_snapshot(dev, now);
        JsonObject obj = blinds.add<JsonObject>();
        obj["address"] = hex_str(dev.config.dst_address);
        obj["name"] = dev.config.name;
        obj["channel"] = dev.config.channel;
        obj["remote"] = hex_str(dev.config.src_address);
        obj["open_ms"] = dev.config.open_duration_ms;
        obj["close_ms"] = dev.config.close_duration_ms;
        obj["supports_tilt"] = dev.config.supports_tilt != 0;
        obj["enabled"] = dev.config.is_enabled();
        obj["updated_at"] = dev.config.updated_at;
        snap.to_json(obj);
        remote_addrs.insert(dev.config.src_address);
      });

      registry->for_each_active(DeviceType::LIGHT, [&](const Device &dev) {
        auto snap = compute_light_snapshot(dev, now);
        JsonObject obj = lights_arr.add<JsonObject>();
        obj["address"] = hex_str(dev.config.dst_address);
        obj["name"] = dev.config.name;
        obj["channel"] = dev.config.channel;
        obj["remote"] = hex_str(dev.config.src_address);
        obj["dim_ms"] = dev.config.dim_duration_ms;
        obj["enabled"] = dev.config.is_enabled();
        obj["updated_at"] = dev.config.updated_at;
        snap.to_json(obj);
        remote_addrs.insert(dev.config.src_address);
      });

      registry->for_each_active(DeviceType::REMOTE, [&](const Device &dev) {
        JsonObject obj = remotes_arr.add<JsonObject>();
        obj["address"] = hex_str(dev.config.dst_address);
        obj["name"] = dev.config.name;
        obj["updated_at"] = dev.config.updated_at;
      });

      registry->for_each_group([&](const NvsGroupConfig &group) {
        JsonObject obj = groups_arr.add<JsonObject>();
        obj["id"] = group.id;
        obj["name"] = group.name;
        JsonArray ids = obj["device_ids"].to<JsonArray>();
        for (uint8_t i = 0; i < group.member_count; ++i) {
          ids.add(hex_str(group.device_ids[i]));
        }
      });

      // Add any remotes from cover/light src_addresses not already tracked
      for (uint32_t addr : remote_addrs) {
        if (registry->find(addr, DeviceType::REMOTE) == nullptr) {
          JsonObject obj = remotes_arr.add<JsonObject>();
          obj["address"] = hex_str(addr);
          obj["name"] = hex_str(addr);
        }
      }
    }

    // mode and crud are in hub object above
  });
}

std::string EleroWebServer::build_rf_json(const RfPacketInfo &pkt) {
  // Build hex string of raw packet
  std::string raw_hex;
  raw_hex.reserve(pkt.raw_len * 3);
  for (int i = 0; i < pkt.raw_len && i < CC1101_FIFO_LENGTH; i++) {
    char byte_buf[4];
    snprintf(byte_buf, sizeof(byte_buf), i == 0 ? "%02x" : " %02x", pkt.raw[i]);
    raw_hex += byte_buf;
  }

  return json::build_json([&](JsonObject root) {
    root["t"] = pkt.timestamp_ms;
    root["src"] = hex_str(pkt.src);
    root["dst"] = hex_str(pkt.dst);
    root["channel"] = pkt.channel;
    root["type"] = hex_str8(pkt.type);
    root["type2"] = hex_str8(pkt.type2);
    root["command"] = hex_str8(pkt.command);
    root["state"] = hex_str8(pkt.state);
    root["cnt"] = pkt.cnt;
    root["rssi"] = round_rssi(pkt.rssi);
    root["hop"] = hex_str8(pkt.hop);
    root["raw"] = raw_hex;
  });
}

std::string EleroWebServer::build_device_upserted_json_(const Device &dev) {
  return json::build_json([&](JsonObject root) {
    root["address"] = hex_str(dev.config.dst_address);
    root["device_type"] = device_type_str(dev.config.type);
    root["name"] = dev.config.name;
    root["enabled"] = dev.config.is_enabled();
    root["updated_at"] = dev.config.updated_at;

    if (!dev.config.is_remote()) {
      root["channel"] = dev.config.channel;
      root["remote"] = hex_str(dev.config.src_address);
    }
    if (dev.config.is_cover()) {
      root["open_ms"] = dev.config.open_duration_ms;
      root["close_ms"] = dev.config.close_duration_ms;
      root["supports_tilt"] = dev.config.supports_tilt != 0;
    }
    if (dev.config.is_light()) {
      root["dim_ms"] = dev.config.dim_duration_ms;
    }
  });
}

std::string EleroWebServer::build_group_json_(const NvsGroupConfig &group) const {
  return json::build_json([&](JsonObject root) {
    root["id"] = group.id;
    root["name"] = group.name;
    JsonArray ids = root["device_ids"].to<JsonArray>();
    for (uint8_t i = 0; i < group.member_count; ++i) {
      ids.add(hex_str(group.device_ids[i]));
    }
  });
}

std::string EleroWebServer::build_learn_in_state_json_() const {
  if (this->parent_ == nullptr) {
    return "{\"state\":\"idle\",\"active\":false,\"busy\":false}";
  }

  const auto &learn_in = this->parent_->learn_in();
  return json::build_json([&](JsonObject root) {
    root["state"] = learn_in_state_str(this->parent_->learn_in_state());
    root["active"] = this->parent_->is_learn_in_active();
    root["busy"] = learn_in.is_busy();
    if (learn_in.src_addr() != 0) {
      root["src_address"] = hex_str(learn_in.src_addr());
      root["channel"] = learn_in.channel();
    }
    if (learn_in.programming_cmd() != packet::command::INVALID) {
      root["programming_cmd"] = hex_str8(learn_in.programming_cmd());
    }
  });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Device Config Parser (shared by save/update)
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::dispatch_device_command_(Device &dev, uint8_t cmd_byte) {
  auto *registry = this->parent_->get_registry();
  if (registry == nullptr) {
    ESP_LOGW(TAG, "No registry for command dispatch");
    return;
  }

  if (dev.is_cover()) {
    registry->command_cover(dev, cmd_byte);
  } else if (dev.is_light()) {
    registry->command_light(dev, cmd_byte);
  } else {
    (void) dev.sender.enqueue(cmd_byte);
  }
  ESP_LOGI(TAG, "Device TX to 0x%06x cmd=0x%02x", dev.config.dst_address, cmd_byte);
}

bool EleroWebServer::parse_device_config_(JsonObject root, NvsDeviceConfig &config, std::string &error) {
  if (!parse_device_type(root["device_type"] | "", config.type)) {
    error = "Invalid device_type";
    return false;
  }

  uint32_t dst_addr = parse_hex32(root, "dst_address");
  if (dst_addr == 0) {
    error = "Missing dst_address";
    return false;
  }
  config.dst_address = dst_addr;

  const char *name = root["name"];
  if (name != nullptr) {
    config.set_name(name);
  }

  // Enabled flag (defaults to true if not specified)
  config.set_enabled(root["enabled"] | true);

  // RF params (covers and lights only)
  if (!config.is_remote()) {
    config.src_address = parse_hex32(root, "src_address");
    if (root["channel"]) config.channel = root["channel"].as<uint8_t>();
    config.hop = parse_hex_or(root, "hop", packet::defaults::HOP);
    config.payload_1 = parse_hex_or(root, "payload_1", packet::defaults::PAYLOAD_1);
    config.payload_2 = parse_hex_or(root, "payload_2", packet::defaults::PAYLOAD_2);
    config.type_byte = parse_hex_or(root, "msg_type", packet::msg_type::COMMAND);
    config.type2 = parse_hex_or(root, "type2", packet::defaults::TYPE2);

    // Timing
    if (root["open_duration_ms"].is<uint32_t>()) config.open_duration_ms = root["open_duration_ms"].as<uint32_t>();
    if (root["close_duration_ms"].is<uint32_t>()) config.close_duration_ms = root["close_duration_ms"].as<uint32_t>();

    if (config.is_cover()) {
      config.supports_tilt = (root["supports_tilt"] | false) ? 1 : 0;
    }
    if (config.is_light()) {
      if (root["dim_duration_ms"].is<uint32_t>()) config.dim_duration_ms = root["dim_duration_ms"].as<uint32_t>();
    }
  }

  return true;
}

bool EleroWebServer::parse_group_config_(JsonObject root, NvsGroupConfig &config, std::string &error) {
  const char *id = root["id"];
  if (id == nullptr || id[0] == '\0') {
    error = "Missing group id";
    return false;
  }
  if (strlen(id) >= NVS_GROUP_ID_MAX) {
    error = "Group id is too long";
    return false;
  }
  config.set_id(id);

  const char *name = root["name"];
  if (name == nullptr || name[0] == '\0') {
    error = "Missing group name";
    return false;
  }
  if (strlen(name) >= NVS_NAME_MAX) {
    error = "Group name is too long";
    return false;
  }
  config.set_name(name);

  if (!root["device_ids"].is<JsonArray>()) {
    error = "Missing device_ids";
    return false;
  }
  JsonArray ids = root["device_ids"].as<JsonArray>();
  if (ids.size() < 2) {
    error = "Group requires at least 2 devices";
    return false;
  }
  if (ids.size() > NVS_GROUP_MAX_MEMBERS) {
    error = "Too many group members";
    return false;
  }

  uint8_t count = 0;
  for (JsonVariant v : ids) {
    uint32_t addr = 0;
    if (v.is<const char *>()) {
      addr = (uint32_t) strtoul(v.as<const char *>(), nullptr, 0);
    } else {
      addr = v.as<uint32_t>();
    }
    if (addr == 0) {
      error = "Invalid device id";
      return false;
    }
    config.device_ids[count++] = addr;
  }
  config.member_count = count;
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Device CRUD Handlers (MQTT mode)
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::handle_upsert_device_(struct mg_connection *c, JsonObject root) {
  auto *registry = this->parent_->get_registry();
  if (registry == nullptr || !registry->is_nvs_enabled()) {
    this->ws_send(c, "error", "{\"msg\":\"Device CRUD requires elero_nvs or elero_mqtt\"}");
    return;
  }

  NvsDeviceConfig config{};
  std::string error;
  if (!parse_device_config_(root, config, error)) {
    this->ws_send(c, "error", json::build_json([&](JsonObject r) { r["msg"] = error; }));
    return;
  }

  if (registry->upsert(config) == nullptr) {
    this->ws_send(c, "error", "{\"msg\":\"Failed to upsert device\"}");
  }
}

void EleroWebServer::handle_remove_device_(struct mg_connection *c, JsonObject root) {
  auto *registry = this->parent_->get_registry();
  if (registry == nullptr || !registry->is_nvs_enabled()) {
    this->ws_send(c, "error", "{\"msg\":\"Device CRUD requires elero_nvs or elero_mqtt\"}");
    return;
  }

  uint32_t addr = parse_hex32(root, "dst_address");
  if (addr == 0) {
    this->ws_send(c, "error", "{\"msg\":\"Missing dst_address\"}");
    return;
  }

  DeviceType type;
  if (!parse_device_type(root["device_type"] | "", type)) {
    this->ws_send(c, "error", "{\"msg\":\"Invalid device_type\"}");
    return;
  }

  if (!registry->remove(addr, type)) {
    this->ws_send(c, "error", "{\"msg\":\"Failed to remove device\"}");
  }
}

void EleroWebServer::handle_upsert_group_(struct mg_connection *c, JsonObject root) {
  auto *registry = this->parent_->get_registry();
  if (registry == nullptr || !registry->is_nvs_enabled()) {
    this->ws_send(c, "error", "{\"msg\":\"Group CRUD requires elero_nvs or elero_mqtt\"}");
    return;
  }

  NvsGroupConfig config{};
  std::string error;
  if (!parse_group_config_(root, config, error)) {
    this->ws_send(c, "error", json::build_json([&](JsonObject r) { r["msg"] = error; }));
    return;
  }

  if (registry->upsert_group(config, &error) == nullptr) {
    this->ws_send(c, "error", json::build_json([&](JsonObject r) { r["msg"] = error.empty() ? "Failed to upsert group" : error; }));
  }
}

void EleroWebServer::handle_remove_group_(struct mg_connection *c, JsonObject root) {
  auto *registry = this->parent_->get_registry();
  if (registry == nullptr || !registry->is_nvs_enabled()) {
    this->ws_send(c, "error", "{\"msg\":\"Group CRUD requires elero_nvs or elero_mqtt\"}");
    return;
  }

  const char *id = root["id"];
  if (id == nullptr || id[0] == '\0') {
    this->ws_send(c, "error", "{\"msg\":\"Missing group id\"}");
    return;
  }
  if (!registry->remove_group(id)) {
    this->ws_send(c, "error", "{\"msg\":\"Failed to remove group\"}");
  }
}

void EleroWebServer::handle_group_command_(struct mg_connection *c, JsonObject root) {
  auto *registry = this->parent_->get_registry();
  if (registry == nullptr) {
    ESP_LOGW(TAG, "Group command rejected: registry unavailable");
    this->ws_send(c, "error", "{\"msg\":\"Registry unavailable\"}");
    return;
  }

  const char *id = root["id"];
  const char *action_str = root["action"];
  if (id == nullptr || id[0] == '\0' || action_str == nullptr) {
    ESP_LOGW(TAG, "Group command rejected: missing fields");
    this->ws_send(c, "error", "{\"msg\":\"Missing group command fields\"}");
    return;
  }
  uint8_t cmd_byte = elero_action_to_command(action_str);
  if (cmd_byte == packet::command::INVALID) {
    ESP_LOGW(TAG, "Group command rejected: invalid action '%s' for group '%s'", action_str, id);
    this->ws_send(c, "error", "{\"msg\":\"Invalid group action\"}");
    return;
  }

  ESP_LOGI(TAG, "Group command request: id='%s' action='%s' cmd=0x%02x", id, action_str, cmd_byte);
  std::string error;
  if (!registry->command_saved_group(id, cmd_byte, &error)) {
    const std::string msg = error.empty() ? "Failed to command group" : error;
    ESP_LOGW(TAG, "Group command rejected for '%s': %s", id, msg.c_str());
    this->ws_send(c, "error", json::build_json([&](JsonObject r) { r["msg"] = msg; }));
    return;
  }
  ESP_LOGI(TAG, "Group command accepted: id='%s' action='%s'", id, action_str);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Backup / Restore (export_config / import_config)
// ═══════════════════════════════════════════════════════════════════════════════

// Bump in lockstep with the `snapshot_version` const in
// frontend/app/asyncapi.yaml (`ConfigSnapshot.snapshot_version`).
// A drift makes every import fail with "Unsupported snapshot_version".
constexpr uint8_t SNAPSHOT_VERSION = 2;

void EleroWebServer::build_device_snapshot_(const NvsDeviceConfig &cfg, JsonObject out) {
  out["device_type"] = device_type_str(cfg.type);
  out["dst_address"] = hex_str(cfg.dst_address);
  out["name"] = cfg.name;
  out["enabled"] = cfg.is_enabled();

  if (!cfg.is_remote()) {
    out["src_address"] = hex_str(cfg.src_address);
    out["channel"] = cfg.channel;
    out["hop"] = hex_str8(cfg.hop);
    out["payload_1"] = hex_str8(cfg.payload_1);
    out["payload_2"] = hex_str8(cfg.payload_2);
    out["msg_type"] = hex_str8(cfg.type_byte);
    out["type2"] = hex_str8(cfg.type2);
  }
  if (cfg.is_cover()) {
    out["open_duration_ms"] = cfg.open_duration_ms;
    out["close_duration_ms"] = cfg.close_duration_ms;
    out["supports_tilt"] = cfg.supports_tilt != 0;
    out["ha_device_class"] = cfg.ha_device_class;
  }
  if (cfg.is_light()) {
    out["dim_duration_ms"] = cfg.dim_duration_ms;
  }
}

std::string EleroWebServer::build_config_snapshot_json_() {
  return json::build_json([this](JsonObject root) {
    auto *registry = this->parent_->get_registry();

    root["snapshot_version"] = SNAPSHOT_VERSION;
    root["exported_at"] = millis();

    JsonObject exporter = root["exporter"].to<JsonObject>();
    exporter["device"] = App.get_name();
    exporter["version"] = this->parent_->get_version();

    JsonObject hub = root["hub"].to<JsonObject>();
    // Only include the override if one is set; absence == "no override".
    if (registry != nullptr) {
      const std::string &display = registry->hub_display_name();
      // We want the persisted *override*, not the effective name. The display
      // name is override-or-default; if it equals the default, no override is set.
      const std::string &def = registry->hub_default_name();
      if (display != def) {
        hub["name_override"] = display;
      }
    }

    JsonArray devices = root["devices"].to<JsonArray>();
    JsonArray groups = root["groups"].to<JsonArray>();
    if (registry != nullptr) {
      registry->for_each_active([&](const Device &dev) {
        // Skip auto-discovered remotes that haven't been persisted yet.
        if (dev.config.updated_at == 0) return;
        JsonObject obj = devices.add<JsonObject>();
        this->build_device_snapshot_(dev.config, obj);
      });
      registry->for_each_group([&](const NvsGroupConfig &group) {
        JsonObject obj = groups.add<JsonObject>();
        obj["id"] = group.id;
        obj["name"] = group.name;
        JsonArray ids = obj["device_ids"].to<JsonArray>();
        for (uint8_t i = 0; i < group.member_count; ++i) {
          ids.add(hex_str(group.device_ids[i]));
        }
      });
    }
  });
}

void EleroWebServer::handle_export_config_(struct mg_connection *c, JsonObject /*root*/) {
  std::string snapshot = this->build_config_snapshot_json_();
  this->ws_send(c, "config_snapshot", snapshot);
  ESP_LOGI(TAG, "Sent config snapshot (%zu bytes)", snapshot.size());
}

void EleroWebServer::handle_import_config_(struct mg_connection *c, JsonObject root) {
  auto *registry = this->parent_->get_registry();
  if (registry == nullptr || !registry->is_nvs_enabled()) {
    this->ws_send(c, "error", "{\"msg\":\"Import requires elero_nvs or elero_mqtt\"}");
    return;
  }

  if (!root["snapshot"].is<JsonObject>()) {
    this->ws_send(c, "error", "{\"msg\":\"Missing 'snapshot' object\"}");
    return;
  }
  JsonObject snap = root["snapshot"].as<JsonObject>();

  uint32_t snap_version = snap["snapshot_version"] | 0;
  if (snap_version == 0 || snap_version > SNAPSHOT_VERSION) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"msg\":\"Unsupported snapshot_version %u (max %u)\"}",
             (unsigned) snap_version, (unsigned) SNAPSHOT_VERSION);
    this->ws_send(c, "error", buf);
    return;
  }

  uint32_t added = 0;
  uint32_t updated = 0;
  uint32_t skipped = 0;
  uint32_t groups_added = 0;
  uint32_t groups_updated = 0;
  uint32_t groups_skipped = 0;
  bool hub_applied = false;

  // Collect errors as a serialized JSON array (built incrementally to avoid
  // building two ArduinoJson docs at once).
  std::string errors_json = "[";
  auto append_error = [&](int idx, const std::string &msg) {
    if (errors_json.size() > 1) errors_json += ',';
    std::string entry = json::build_json([&](JsonObject e) {
      e["index"] = idx;
      e["msg"] = msg;
    });
    errors_json += entry;
  };

  // Apply hub overrides (currently just name_override). set_hub_name_override
  // returns false when the value matches what's already persisted — avoid
  // claiming a no-op as a successful restore in the import_result toast.
  if (snap["hub"].is<JsonObject>()) {
    JsonObject hub_obj = snap["hub"].as<JsonObject>();
    if (hub_obj["name_override"].is<const char *>()) {
      const char *name = hub_obj["name_override"].as<const char *>();
      hub_applied = registry->set_hub_name_override(name == nullptr ? "" : name);
    }
  }

  // Apply each device through parse_device_config_ + upsert.
  if (snap["devices"].is<JsonArray>()) {
    JsonArray devs = snap["devices"].as<JsonArray>();
    int idx = -1;
    for (JsonVariant v : devs) {
      ++idx;
      if (!v.is<JsonObject>()) {
        append_error(idx, "Device entry is not an object");
        ++skipped;
        continue;
      }
      JsonObject obj = v.as<JsonObject>();
      NvsDeviceConfig cfg{};
      std::string error;
      if (!this->parse_device_config_(obj, cfg, error)) {
        append_error(idx, error);
        ++skipped;
        continue;
      }

      bool was_existing = (registry->find(cfg.dst_address, cfg.type) != nullptr);
      if (registry->upsert(cfg) == nullptr) {
        append_error(idx, "No free slot");
        ++skipped;
        continue;
      }
      if (was_existing) ++updated; else ++added;
    }
  }

  if (snap["groups"].is<JsonArray>()) {
    JsonArray group_arr = snap["groups"].as<JsonArray>();
    int idx = -1;
    for (JsonVariant v : group_arr) {
      ++idx;
      if (!v.is<JsonObject>()) {
        append_error(idx, "Group entry is not an object");
        ++groups_skipped;
        continue;
      }
      JsonObject obj = v.as<JsonObject>();
      NvsGroupConfig cfg{};
      std::string error;
      if (!this->parse_group_config_(obj, cfg, error)) {
        append_error(idx, error);
        ++groups_skipped;
        continue;
      }
      bool was_existing = (registry->find_group(cfg.id) != nullptr);
      if (registry->upsert_group(cfg, &error) == nullptr) {
        append_error(idx, error.empty() ? "Failed to upsert group" : error);
        ++groups_skipped;
        continue;
      }
      if (was_existing) ++groups_updated; else ++groups_added;
    }
  }

  errors_json += "]";

  std::string reply = json::build_json([&](JsonObject r) {
    r["added"] = added;
    r["updated"] = updated;
    r["skipped"] = skipped;
    r["groups_added"] = groups_added;
    r["groups_updated"] = groups_updated;
    r["groups_skipped"] = groups_skipped;
    r["hub_applied"] = hub_applied;
    r["errors"] = serialized(errors_json);
  });
  this->ws_send(c, "import_result", reply);
  ESP_LOGI(TAG, "Import: %u added, %u updated, %u skipped, %u groups added, %u groups updated, %u groups skipped, hub_applied=%d",
           (unsigned) added, (unsigned) updated, (unsigned) skipped,
           (unsigned) groups_added, (unsigned) groups_updated, (unsigned) groups_skipped, hub_applied);
}

void EleroWebServer::handle_set_hub_config_(struct mg_connection *c, JsonObject root) {
  auto *registry = this->parent_->get_registry();
  if (registry == nullptr) {
    this->ws_send(c, "error", "{\"msg\":\"Registry unavailable\"}");
    return;
  }
  // `name`: empty/missing clears the override and falls back to the YAML default.
  const char *name = root["name"] | "";
  registry->set_hub_name_override(name);

  // Broadcast updated config to all clients so frontends refresh.
  // Capture the name by value to keep the JSON build pure of registry lifetime.
  std::string display_name = registry->hub_display_name();
  this->ws_broadcast("hub_config", json::build_json([&display_name](JsonObject r) {
    r["name"] = display_name;
  }));
}

void EleroWebServer::handle_learn_in_start_(struct mg_connection *c, JsonObject root) {
  if (this->parent_ == nullptr) {
    this->ws_send(c, "error", "{\"msg\":\"Hub unavailable\"}");
    return;
  }

  LearnInStartRequest request{};
  request.src_addr = parse_hex32(root, "src_address");
  if (request.src_addr == 0) {
    request.src_addr = derive_default_virtual_remote_address();
  }
  request.channel = root["channel"] | 0;
  request.programming_cmd = parse_hex_or(root, "programming_cmd", packet::command::INVALID);
  request.packets = parse_hex_or(root, "packets", packet::button::PACKETS);
  request.type2 = parse_hex_or(root, "type2", packet::button::TYPE2);
  request.hop = parse_hex_or(root, "hop", packet::button::HOP);
  request.session_timeout_ms = root["session_timeout_ms"] | 300000;

  if (!this->parent_->start_learn_in(request)) {
    this->ws_send(c, "error", "{\"msg\":\"Failed to start learn-in\"}");
    return;
  }
  this->ws_broadcast("learn_in_state", this->build_learn_in_state_json_());
}

void EleroWebServer::handle_learn_in_confirm_up_(struct mg_connection *c) {
  if (this->parent_ == nullptr || !this->parent_->confirm_learn_in_up()) {
    this->ws_send(c, "error", "{\"msg\":\"Failed to confirm learn-in UP step\"}");
    return;
  }
  this->ws_broadcast("learn_in_state", this->build_learn_in_state_json_());
}

void EleroWebServer::handle_learn_in_confirm_down_(struct mg_connection *c) {
  if (this->parent_ == nullptr || !this->parent_->confirm_learn_in_down()) {
    this->ws_send(c, "error", "{\"msg\":\"Failed to confirm learn-in DOWN step\"}");
    return;
  }
  this->ws_broadcast("learn_in_state", this->build_learn_in_state_json_());
}

void EleroWebServer::handle_learn_in_cancel_(struct mg_connection *c) {
  if (this->parent_ == nullptr) {
    this->ws_send(c, "error", "{\"msg\":\"Hub unavailable\"}");
    return;
  }
  this->parent_->cancel_learn_in();
  this->ws_broadcast("learn_in_state", this->build_learn_in_state_json_());
}

}  // namespace elero
}  // namespace esphome
