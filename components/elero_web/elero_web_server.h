#pragma once

#include "esphome/core/component.h"
#include "mongoose.h"
#include "../elero/elero.h"
#include <string>
#include <vector>

namespace esphome {
namespace elero {

class EleroWebServer : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

  void set_elero_parent(Elero *parent) { this->parent_ = parent; }
  void set_port(uint16_t port) { this->port_ = port; }

  // Enable/disable web UI (used by HA switch)
  void set_enabled(bool en) { this->enabled_ = en; }
  bool is_enabled() const { return this->enabled_; }

  // State change notifications (called from hub)
  void notify_covers_changed() { this->covers_dirty_ = true; }
  void notify_discovered_changed() { this->discovered_dirty_ = true; }
  void notify_log_entry() { this->logs_dirty_ = true; }
  void notify_packet_received() { this->packets_dirty_ = true; }
  void notify_scan_status_changed() { this->scan_dirty_ = true; }

 protected:
  Elero *parent_{nullptr};
  uint16_t port_{80};
  bool enabled_{true};

  // Mongoose state
  struct mg_mgr mgr_;
  struct mg_connection *listener_{nullptr};
  std::vector<struct mg_connection *> sse_clients_;

  // Dirty flags for incremental updates
  bool covers_dirty_{false};
  bool discovered_dirty_{false};
  bool logs_dirty_{false};
  bool packets_dirty_{false};
  bool scan_dirty_{false};
  uint32_t last_log_push_ts_{0};
  uint32_t last_heartbeat_ms_{0};

  // Mongoose event handler (static for C callback)
  static void event_handler(struct mg_connection *c, int ev, void *ev_data);

  // Route handlers
  void handle_index(struct mg_connection *c);
  void handle_get_state(struct mg_connection *c);
  void handle_get_yaml(struct mg_connection *c);
  void handle_sse_connect(struct mg_connection *c);
  void handle_post_scan_start(struct mg_connection *c);
  void handle_post_scan_stop(struct mg_connection *c);
  void handle_post_log_start(struct mg_connection *c);
  void handle_post_log_stop(struct mg_connection *c);
  void handle_post_log_clear(struct mg_connection *c);
  void handle_post_dump_start(struct mg_connection *c);
  void handle_post_dump_stop(struct mg_connection *c);
  void handle_post_dump_clear(struct mg_connection *c);
  void handle_post_cover(struct mg_connection *c, struct mg_http_message *hm);
  void handle_post_settings(struct mg_connection *c, struct mg_http_message *hm);
  void handle_post_adopt(struct mg_connection *c, struct mg_http_message *hm);
  void handle_post_runtime_remove(struct mg_connection *c, struct mg_http_message *hm);
  void handle_post_frequency(struct mg_connection *c, struct mg_http_message *hm);

  // SSE helpers
  void sse_send(struct mg_connection *c, const char *event, const std::string &data);
  void sse_broadcast(const char *event, const std::string &data);
  void sse_cleanup();

  // JSON builders
  std::string build_full_state_json();
  std::string build_covers_json();
  std::string build_discovered_json();
  std::string build_logs_json(uint32_t since_ms = 0, size_t max_entries = 20);
  std::string build_packets_json();
  std::string build_yaml();

  // Response helpers
  void send_json_ok(struct mg_connection *c);
  void send_json_error(struct mg_connection *c, int code, const char *error);
};

// Heartbeat interval for SSE (ms)
constexpr uint32_t ELERO_SSE_HEARTBEAT_INTERVAL = 15000;

}  // namespace elero
}  // namespace esphome
