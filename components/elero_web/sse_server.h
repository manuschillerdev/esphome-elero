#pragma once

#include "esphome/core/defines.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include <functional>
#include <string>

#ifdef USE_ARDUINO
#include <ESPAsyncWebServer.h>
#endif

#ifdef USE_ESP_IDF
#include <esp_http_server.h>
#include <vector>
#include <freertos/semphr.h>
#endif

namespace esphome {
namespace elero {

// Heartbeat interval for SSE connections (ms)
constexpr uint32_t SSE_HEARTBEAT_INTERVAL = 15000;

#ifdef USE_ESP_IDF
// Maximum concurrent SSE clients for IDF implementation
constexpr size_t SSE_MAX_CLIENTS = 4;

struct SSEClientInfo {
  httpd_handle_t hd;
  int fd;
  bool active;
};
#endif

/// @brief Cross-framework SSE server abstraction
///
/// Provides a unified interface for Server-Sent Events that works on both
/// Arduino (ESPAsyncWebServer) and ESP-IDF (esp_http_server) frameworks.
///
/// Usage:
///   SSEServer sse;
///   sse.set_on_connect([](SSEServer &s) { s.send("state", build_state()); });
///   sse.setup(web_server_base, "/events");
///   sse.send("update", json_data);
///
class SSEServer {
 public:
  using OnConnectCallback = std::function<void(SSEServer &)>;

  /// Initialize the SSE server and register the endpoint
  /// @param base The ESPHome web_server_base instance
  /// @param path The URL path for SSE connections (e.g., "/elero/events")
  void setup(web_server_base::WebServerBase *base, const char *path);

  /// Broadcast an event to all connected clients
  /// @param event Event type name (e.g., "state", "covers", "log")
  /// @param data JSON data to send
  void send(const char *event, const std::string &data);

  /// Get the number of currently connected clients
  size_t client_count() const;

  /// Set callback to be invoked when a new client connects
  /// Use this to send initial state to new clients
  void set_on_connect(OnConnectCallback callback) { this->on_connect_ = std::move(callback); }

 protected:
  OnConnectCallback on_connect_;

#ifdef USE_ARDUINO
  AsyncEventSource *events_{nullptr};
#endif

#ifdef USE_ESP_IDF
  httpd_handle_t server_{nullptr};
  std::vector<SSEClientInfo> clients_;
  SemaphoreHandle_t lock_{nullptr};
  const char *path_{nullptr};

  void add_client_(httpd_handle_t hd, int fd);
  void remove_client_(int fd);
  void cleanup_disconnected_();
  static esp_err_t handle_sse_request_(httpd_req_t *req);
#endif
};

}  // namespace elero
}  // namespace esphome
