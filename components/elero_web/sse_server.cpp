#include "sse_server.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

static const char *const TAG = "elero.sse";

// ═══════════════════════════════════════════════════════════════════════════════
// Arduino Implementation (ESPAsyncWebServer)
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef USE_ARDUINO

void SSEServer::setup(web_server_base::WebServerBase *base, const char *path) {
  this->events_ = new AsyncEventSource(path);

  this->events_->onConnect([this](AsyncEventSourceClient *client) {
    ESP_LOGI(TAG, "SSE client connected from %s", client->client()->remoteIP().toString().c_str());
    if (this->on_connect_) {
      this->on_connect_(*this);
    }
  });

  base->get_server()->addHandler(this->events_);
  ESP_LOGI(TAG, "SSE endpoint registered at %s (Arduino)", path);
}

void SSEServer::send(const char *event, const std::string &data) {
  if (this->events_ == nullptr || this->events_->count() == 0)
    return;
  this->events_->send(data.c_str(), event, millis());
}

size_t SSEServer::client_count() const {
  if (this->events_ == nullptr)
    return 0;
  return this->events_->count();
}

#endif  // USE_ARDUINO

// ═══════════════════════════════════════════════════════════════════════════════
// ESP-IDF Implementation (esp_http_server)
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef USE_ESP_IDF

void SSEServer::setup(web_server_base::WebServerBase *base, const char *path) {
  this->path_ = path;
  this->lock_ = xSemaphoreCreateMutex();
  this->clients_.reserve(SSE_MAX_CLIENTS);

  // Get the underlying httpd_handle_t from web_server_base
  // Note: web_server_base exposes this via get_server() which returns httpd_handle_t on IDF
  this->server_ = base->get_server();

  httpd_uri_t sse_uri = {
      .uri = path,
      .method = HTTP_GET,
      .handler = SSEServer::handle_sse_request_,
      .user_ctx = this,
  };

  esp_err_t err = httpd_register_uri_handler(this->server_, &sse_uri);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register SSE handler: %s", esp_err_to_name(err));
    return;
  }

  ESP_LOGI(TAG, "SSE endpoint registered at %s (ESP-IDF)", path);
}

esp_err_t SSEServer::handle_sse_request_(httpd_req_t *req) {
  auto *self = static_cast<SSEServer *>(req->user_ctx);

  // Set SSE headers
  httpd_resp_set_type(req, "text/event-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // Send initial response to establish connection
  const char *initial = ":ok\n\n";
  esp_err_t err = httpd_resp_send_chunk(req, initial, strlen(initial));
  if (err != ESP_OK) {
    return err;
  }

  // Get the socket fd for this connection
  int fd = httpd_req_to_sockfd(req);
  if (fd < 0) {
    return ESP_FAIL;
  }

  // Track this client
  self->add_client_(req->handle, fd);

  ESP_LOGI(TAG, "SSE client connected (fd=%d)", fd);

  // Call on_connect callback
  if (self->on_connect_) {
    self->on_connect_(*self);
  }

  // Keep the connection open - we'll send data via send()
  // Return ESP_OK but don't finalize the response
  return ESP_OK;
}

void SSEServer::add_client_(httpd_handle_t hd, int fd) {
  if (xSemaphoreTake(this->lock_, pdMS_TO_TICKS(100)) != pdTRUE)
    return;

  // Clean up any disconnected clients first
  this->cleanup_disconnected_();

  // Check if we have room
  if (this->clients_.size() >= SSE_MAX_CLIENTS) {
    ESP_LOGW(TAG, "Max SSE clients reached, rejecting connection");
    xSemaphoreGive(this->lock_);
    return;
  }

  this->clients_.push_back({hd, fd, true});
  xSemaphoreGive(this->lock_);
}

void SSEServer::remove_client_(int fd) {
  if (xSemaphoreTake(this->lock_, pdMS_TO_TICKS(100)) != pdTRUE)
    return;

  for (auto &client : this->clients_) {
    if (client.fd == fd) {
      client.active = false;
      break;
    }
  }

  xSemaphoreGive(this->lock_);
}

void SSEServer::cleanup_disconnected_() {
  // Must be called with lock held
  this->clients_.erase(std::remove_if(this->clients_.begin(), this->clients_.end(),
                                      [](const SSEClientInfo &c) { return !c.active; }),
                       this->clients_.end());
}

void SSEServer::send(const char *event, const std::string &data) {
  if (xSemaphoreTake(this->lock_, pdMS_TO_TICKS(100)) != pdTRUE)
    return;

  if (this->clients_.empty()) {
    xSemaphoreGive(this->lock_);
    return;
  }

  // Format SSE message: "event: <event>\ndata: <data>\n\n"
  std::string msg = "event: ";
  msg += event;
  msg += "\ndata: ";
  msg += data;
  msg += "\n\n";

  // Send to all active clients
  for (auto &client : this->clients_) {
    if (!client.active)
      continue;

    int ret = httpd_socket_send(client.hd, client.fd, msg.c_str(), msg.length(), 0);
    if (ret < 0) {
      ESP_LOGW(TAG, "SSE send failed to fd=%d, marking disconnected", client.fd);
      client.active = false;
    }
  }

  // Clean up any that failed
  this->cleanup_disconnected_();

  xSemaphoreGive(this->lock_);
}

size_t SSEServer::client_count() const {
  if (this->lock_ == nullptr)
    return 0;

  size_t count = 0;
  if (xSemaphoreTake(this->lock_, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (const auto &client : this->clients_) {
      if (client.active)
        count++;
    }
    xSemaphoreGive(this->lock_);
  }
  return count;
}

#endif  // USE_ESP_IDF

}  // namespace elero
}  // namespace esphome
