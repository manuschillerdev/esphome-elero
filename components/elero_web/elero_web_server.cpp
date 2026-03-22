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
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <set>

namespace esphome {
namespace elero {

static const char *const TAG = "elero.web";

// Global instance pointer for Mongoose's C callback
static EleroWebServer *g_server = nullptr;

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

// ═══════════════════════════════════════════════════════════════════════════════
// State Changed (OutputAdapter — optimistic updates)
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::on_state_changed(const Device &dev) {
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
  mg_printf(c,
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Content-Encoding: gzip\r\n"
      "Content-Length: %lu\r\n\r\n",
      (unsigned long) ELERO_WEB_UI_GZ_LEN);
  mg_send(c, ELERO_WEB_UI_GZ, ELERO_WEB_UI_GZ_LEN);
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

    if (type == "raw") {
      uint32_t dst_addr = parse_hex32(root, "dst_address");
      uint32_t src_addr = parse_hex32(root, "src_address");
      uint8_t channel = parse_hex_or(root, "channel", 0);
      uint8_t raw_command = parse_hex_or(root, "command", 0);

      if (dst_addr == 0 || src_addr == 0) {
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
      uint8_t msg_type = parse_hex_or(root, "msg_type", packet::msg_type::COMMAND);
      uint8_t type2_val = parse_hex_or(root, "type2", packet::defaults::TYPE2);
      uint8_t hop = parse_hex_or(root, "hop", packet::defaults::HOP);

      bool success = this->parent_->send_raw_command(
          dst_addr, src_addr, channel, raw_command,
          payload_1, payload_2, msg_type, type2_val, hop);
      ESP_LOGI(TAG, "Raw TX to 0x%06x cmd=0x%02x: %s", dst_addr, raw_command, success ? "OK" : "FAIL");
      return true;
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
    hub["mode"] = has_nvs ? "mqtt" : "native";
    hub["crud"] = has_nvs;

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
        obj["poll_ms"] = dev.config.poll_interval_ms;
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
    root["echo"] = pkt.echo;
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
      root["poll_ms"] = dev.config.poll_interval_ms;
      root["supports_tilt"] = dev.config.supports_tilt != 0;
    }
    if (dev.config.is_light()) {
      root["dim_ms"] = dev.config.dim_duration_ms;
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
    if (root["poll_interval_ms"].is<uint32_t>()) config.poll_interval_ms = root["poll_interval_ms"].as<uint32_t>();

    if (config.is_cover()) {
      config.supports_tilt = (root["supports_tilt"] | false) ? 1 : 0;
    }
    if (config.is_light()) {
      if (root["dim_duration_ms"].is<uint32_t>()) config.dim_duration_ms = root["dim_duration_ms"].as<uint32_t>();
    }
  }

  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Device CRUD Handlers (MQTT mode)
// ═══════════════════════════════════════════════════════════════════════════════

void EleroWebServer::handle_upsert_device_(struct mg_connection *c, JsonObject root) {
  auto *registry = this->parent_->get_registry();
  if (registry == nullptr || !registry->is_nvs_enabled()) {
    this->ws_send(c, "error", "{\"msg\":\"CRUD not supported in native mode\"}");
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
    this->ws_send(c, "error", "{\"msg\":\"CRUD not supported in native mode\"}");
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

}  // namespace elero
}  // namespace esphome
