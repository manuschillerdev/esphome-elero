#include "elero.h"
#include "elero_protocol.h"
#include "elero_packet.h"
#include "elero_strings.h"
#include "device.h"
#include "device_registry.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include <cstring>
#include <algorithm>

#ifdef USE_ESP32
#include <esp_timer.h>
#include <esp_task_wdt.h>
#endif

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

namespace esphome {
namespace elero {

static const char *const TAG = "elero";
static const char *const TAG_RF = "elero.rf";

// String conversion functions (elero_state_to_string, elero_command_to_string,
// elero_action_to_command) are defined in elero_strings.cpp for testability.

void Elero::loop() {
#ifdef USE_ESP32
  // ─── Core 1: Drain FreeRTOS queues from RF task, run ESPHome dispatch ──────

  // 1. Drain decoded RX packets from RF task
  RfPacketInfo pkt{};
  while (xQueueReceive(this->rx_queue_handle_, &pkt, 0) == pdPASS) {
    this->dispatch_packet(pkt);
  }

  // 2. Drain TX completion results and notify CommandSenders
  TxResult result{};
  while (xQueueReceive(this->tx_done_queue_handle_, &result, 0) == pdPASS) {
    if (result.success) {
      this->stat_tx_success_++;
    } else {
      this->stat_tx_fail_++;
    }
    if (result.client != nullptr) {
      result.client->on_tx_complete(result.success);
    }
  }

  // 3. Registry loop (state machines, command queues, adapter loops)
  if (this->registry_ != nullptr) {
    this->registry_->loop(millis());
  }

  // 4. Publish RF stats sensors (throttled to every 30s)
  this->publish_stats_();
#endif
}

// ─── decode_fifo_packets_: parse multiple packets from raw FIFO buffer ───────
void Elero::decode_fifo_packets_(size_t fifo_count) {
#ifdef USE_ESP32
  int64_t drain_start_us = esp_timer_get_time();
#endif

  // Log raw bytes at VERBOSE level for analysis
  ESP_LOGV(TAG, "RAW RX %d bytes: %s", static_cast<int>(fifo_count),
           format_hex_pretty(this->msg_rx_, fifo_count).c_str());

  // Parse multiple packets from the buffer
  size_t offset = 0;
  int pkt_count = 0;
  while (offset < fifo_count) {
    // Need at least PACKET_TOTAL_OVERHEAD bytes for the smallest valid packet
    if (offset + packet::PACKET_TOTAL_OVERHEAD > fifo_count) {
      break;
    }

    uint8_t pkt_len = this->msg_rx_[offset + packet::pkt_offset::LENGTH];
    size_t total = static_cast<size_t>(pkt_len) + packet::PACKET_TOTAL_OVERHEAD;

    // Incomplete packet at end of buffer
    if (offset + total > fifo_count) {
      ESP_LOGV(TAG, "Incomplete packet at offset %d: need %d bytes, have %d",
               static_cast<int>(offset), static_cast<int>(total),
               static_cast<int>(fifo_count - offset));
      break;
    }

    auto pkt = this->decode_packet(this->msg_rx_ + offset, fifo_count - offset);
    if (pkt) {
#ifdef USE_ESP32
      if (xQueueSend(this->rx_queue_handle_, &(*pkt), 0) != pdPASS) {
        ESP_LOGW(TAG, "RX queue full, dropping packet cnt=%d", pkt->cnt);
        this->stat_rx_drops_.fetch_add(1, std::memory_order_relaxed);
      }
#endif
      ++pkt_count;
    }

    offset += total;
  }

  if (pkt_count > 1) {
    ESP_LOGD(TAG, "Decoded %d packets from single FIFO read (%d bytes)", pkt_count, static_cast<int>(fifo_count));
  }

#ifdef USE_ESP32
  if (pkt_count > 0) {
    int64_t drain_us = esp_timer_get_time() - drain_start_us;
    ESP_LOGD(TAG, "decode_fifo: %d pkt(s), %d bytes, %lldus", pkt_count, static_cast<int>(fifo_count), drain_us);
  }
#endif
}

// ─── ISR: dual signal — atomic flag + task notification ──────────────────────
void IRAM_ATTR Elero::interrupt(Elero *arg) {
  // Set atomic flag for TX state machine's WAIT_TX check
  arg->received_.store(true, std::memory_order_release);

#ifdef USE_ESP32
  // Wake RF task immediately (bypasses 1ms sleep)
  if (arg->rf_task_handle_ != nullptr) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(arg->rf_task_handle_, &woken);
    portYIELD_FROM_ISR(woken);
  }
#endif
}

void Elero::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero RF:");
  ESP_LOGCONFIG(TAG, "  Version: %s", this->version_);
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  if (this->driver_) {
    this->driver_->dump_config();
  }
  if (this->registry_) {
    ESP_LOGCONFIG(TAG, "  Registered devices: %d", this->registry_->count_active());
  }
}

void Elero::setup() {
  ESP_LOGI(TAG, "Setting up Elero Component...");

  // Initialize radio driver
  if (this->driver_ == nullptr) {
    ESP_LOGE(TAG, "No radio driver configured");
    this->mark_failed();
    return;
  }

  // Pass ISR flag to driver and initialize
  this->driver_->set_irq_flag(&this->received_);
  if (!this->driver_->init()) {
    ESP_LOGE(TAG, "Radio driver initialization failed");
    this->mark_failed();
    return;
  }

  // Setup IRQ pin and attach interrupt
  this->irq_pin_->setup();
  auto edge = this->driver_->irq_rising_edge() ? gpio::INTERRUPT_RISING_EDGE
                                                : gpio::INTERRUPT_FALLING_EDGE;
  this->irq_pin_->attach_interrupt(Elero::interrupt, this, edge);

  // Clear any IRQs that fired during init() before the ISR was attached.
  // For SX1262: DIO1 goes HIGH on preamble detection during init → rising edge missed.
  this->received_.store(false);
  this->driver_->recover();  // Clears hardware IRQ flags + re-enters RX

  // New architecture: device registry lifecycle
  if (this->registry_ != nullptr) {
    // Setup all output adapters (MQTT, etc.) before restoring devices
    this->registry_->setup_adapters();

    // Restore NVS-persisted devices (MQTT/NVS modes)
    if (this->registry_->is_nvs_enabled()) {
      this->registry_->init_preferences();
      this->registry_->restore_all();
    }
  }

#ifdef USE_ESP32
  // ─── Create FreeRTOS queues and spawn RF task on Core 0 ────────────────────
  this->rx_queue_handle_ = xQueueCreate(16, sizeof(RfPacketInfo));
  this->tx_queue_handle_ = xQueueCreate(4, sizeof(RfTaskRequest));
  this->tx_done_queue_handle_ = xQueueCreate(4, sizeof(TxResult));

  if (this->rx_queue_handle_ == nullptr ||
      this->tx_queue_handle_ == nullptr ||
      this->tx_done_queue_handle_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create FreeRTOS queues (heap exhausted?)");
    this->mark_failed();
    return;
  }

  BaseType_t task_created = xTaskCreatePinnedToCore(
      rf_task_func_,         // Task function
      "elero_rf",            // Name (for debugging)
      4096,                  // Stack size (bytes)
      this,                  // Parameter
      5,                     // Priority (above default ESPHome loop)
      &this->rf_task_handle_,  // Handle
      0                      // Core 0 (ESPHome runs on Core 1)
  );
  if (task_created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create RF task (heap exhausted?)");
    this->mark_failed();
    return;
  }
  esp_task_wdt_add(this->rf_task_handle_);
  ESP_LOGI(TAG, "RF task started on Core 0 (priority 5, stack 4096, WDT registered)");
#endif
}

// ─── RF Task: Core 0 dedicated radio controller ─────────────────────────────
// Owns all SPI access after setup(). Communicates with main loop via FreeRTOS
// queues only. Woken by GDO0 ISR notification or 1ms timeout.
#ifdef USE_ESP32
void Elero::rf_task_func_(void *arg) {
  auto *self = static_cast<Elero *>(arg);
  uint32_t last_stack_check_ms = 0;
  bool tx_in_progress = false;

  for (;;) {
    uint32_t now = millis();

    // 1. Process TX requests from main loop (only when radio is idle)
    if (!tx_in_progress) {
      RfTaskRequest req{};
      if (xQueueReceive(self->tx_queue_handle_, &req, 0) == pdPASS) {
        switch (req.type) {
          case RfTaskRequest::Type::TX:
            self->build_tx_packet_(req.cmd);
            if (self->driver_->load_and_transmit(self->msg_tx_, self->msg_tx_[0] + 1)) {
              self->tx_owner_ = req.client;
              tx_in_progress = true;
            } else {
              // load_and_transmit failed — report failure immediately
              TxResult r{req.client, false};
              xQueueSend(self->tx_done_queue_handle_, &r, 0);
            }
            break;

          case RfTaskRequest::Type::REINIT_FREQ:
            // Abort any pending TX
            if (self->tx_owner_ != nullptr) {
              TxResult r{self->tx_owner_, false};
              self->tx_owner_ = nullptr;
              xQueueSend(self->tx_done_queue_handle_, &r, 0);
              tx_in_progress = false;
            }
            self->received_.store(false);
            self->freq2_.store(req.freq.f2);
            self->freq1_.store(req.freq.f1);
            self->freq0_.store(req.freq.f0);
            self->driver_->set_frequency_regs(req.freq.f2, req.freq.f1, req.freq.f0);
            break;
        }
      }
    }

    // 2. Progress TX via driver
    if (tx_in_progress) {
      auto result = self->driver_->poll_tx();
      switch (result) {
        case TxPollResult::PENDING:
          break;
        case TxPollResult::SUCCESS:
          ESP_LOGV(TAG, "TX complete (success)");
          {
            TxResult r{self->tx_owner_, true};
            self->tx_owner_ = nullptr;
            tx_in_progress = false;
            xQueueSend(self->tx_done_queue_handle_, &r, 0);
          }
          break;
        case TxPollResult::FAILED:
          ESP_LOGW(TAG, "TX complete (failed)");
          self->stat_tx_recover_.fetch_add(1, std::memory_order_relaxed);
          {
            TxResult r{self->tx_owner_, false};
            self->tx_owner_ = nullptr;
            tx_in_progress = false;
            xQueueSend(self->tx_done_queue_handle_, &r, 0);
          }
          break;
      }
    }

    // 3. Drain FIFO if GDO0 interrupt fired
    if (self->driver_->has_data()) {
      // Clear IRQ flag
      self->received_.exchange(false);
      // Read FIFO bytes from driver
      size_t count = self->driver_->read_fifo(self->msg_rx_, sizeof(self->msg_rx_));
      if (count > 0) {
        // Parse packets from buffer (same multi-packet logic as before)
        self->decode_fifo_packets_(count);
      }
    }

    // 4. Radio health check (only when idle, throttled internally to every 5s)
    if (!tx_in_progress) {
      auto health = self->driver_->check_health();
      switch (health) {
        case RadioHealth::OK:
          break;
        case RadioHealth::FIFO_OVERFLOW:
        case RadioHealth::STUCK:
        case RadioHealth::UNRECOVERABLE:
          self->driver_->recover();
          self->stat_watchdog_recoveries_.fetch_add(1, std::memory_order_relaxed);
          break;
      }
    }

    // 5. Stack watermark check (development aid, every 30s)
    now = millis();
    if (now - last_stack_check_ms > 30000) {
      last_stack_check_ms = now;
      ESP_LOGV(TAG, "RF task stack HWM: %u bytes free",
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
    }

    // 6. Feed task watchdog (registered in setup)
    esp_task_wdt_reset();

    // 7. Yield — sleep until ISR notification or 1ms timeout
    //    This ensures Core 0 IDLE task runs (prevents TWDT) while keeping
    //    the RF task responsive to both RX interrupts and TX requests.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
  }
}
#endif

void Elero::reinit_frequency(uint8_t freq2, uint8_t freq1, uint8_t freq0) {
#ifdef USE_ESP32
  // Post frequency change request to RF task (all SPI happens on Core 0)
  RfTaskRequest req{};
  req.type = RfTaskRequest::Type::REINIT_FREQ;
  req.freq = {freq2, freq1, freq0};
  if (xQueueSend(this->tx_queue_handle_, &req, 0) != pdPASS) {
    ESP_LOGW(TAG, "Frequency change failed: tx_queue full");
    return;
  }

  // Update atomic copies for get_freq*() accessors (immediate visibility on Core 1)
  this->freq2_.store(freq2);
  this->freq1_.store(freq1);
  this->freq0_.store(freq0);
  ESP_LOGI(TAG, "Frequency change queued: freq2=0x%02x freq1=0x%02x freq0=0x%02x", freq2, freq1, freq0);
#endif
}

void Elero::build_tx_packet_(const EleroCommand &cmd) {
  if (cmd.type == packet::msg_type::BUTTON) {
    packet::ButtonTxParams params;
    params.counter = cmd.counter;
    params.src_addr = cmd.src_addr;
    params.channel = cmd.channel;
    params.command = cmd.payload[4];
    params.type2 = cmd.type2;
    params.hop = cmd.hop;
    packet::build_button_packet(params, this->msg_tx_);
  } else {
    packet::TxParams params;
    params.counter = cmd.counter;
    params.dst_addr = cmd.dst_addr;
    params.src_addr = cmd.src_addr;
    params.channel = cmd.channel;
    params.type = cmd.type;
    params.type2 = cmd.type2;
    params.hop = cmd.hop;
    params.command = cmd.payload[4];
    params.payload_1 = cmd.payload[0];
    params.payload_2 = cmd.payload[1];
    packet::build_tx_packet(params, this->msg_tx_);
  }
}

bool Elero::request_tx(TxClient *client, const EleroCommand &cmd) {
#ifdef USE_ESP32
  // Look up device name for TX intent log (runs on Core 1, no SPI)
  std::string blind_name;
  if (this->registry_) {
    Device *dev = this->registry_->find(cmd.dst_addr);
    if (dev) {
      blind_name = dev->config.name;
    }
  }

  // JSON log for TX packet (no msg_tx_ reference — packet built on RF task)
  if (!blind_name.empty()) {
    ESP_LOGD(TAG_RF,
             "{\"ts_ms\":%lu,\"dir\":\"tx\",\"blind\":\"%s\",\"cmd_name\":\"%s\",\"cnt\":%d,"
             "\"type\":\"0x%02x\",\"type2\":\"0x%02x\",\"hop\":\"0x%02x\","
             "\"channel\":%d,\"src\":\"0x%06x\",\"dst\":\"0x%06x\",\"command\":\"0x%02x\"}",
             (unsigned long) millis(), blind_name.c_str(), elero_command_to_string(cmd.payload[4]),
             cmd.counter, cmd.type, cmd.type2, cmd.hop,
             cmd.channel, cmd.src_addr, cmd.dst_addr, cmd.payload[4]);
  } else {
    ESP_LOGD(TAG_RF,
             "{\"ts_ms\":%lu,\"dir\":\"tx\",\"blind\":\"0x%06x\",\"cmd_name\":\"%s\",\"cnt\":%d,"
             "\"type\":\"0x%02x\",\"type2\":\"0x%02x\",\"hop\":\"0x%02x\","
             "\"channel\":%d,\"src\":\"0x%06x\",\"dst\":\"0x%06x\",\"command\":\"0x%02x\"}",
             (unsigned long) millis(), cmd.dst_addr, elero_command_to_string(cmd.payload[4]),
             cmd.counter, cmd.type, cmd.type2, cmd.hop,
             cmd.channel, cmd.src_addr, cmd.dst_addr, cmd.payload[4]);
  }

  // Post to RF task queue (non-blocking, no SPI)
  RfTaskRequest req{};
  req.type = RfTaskRequest::Type::TX;
  req.cmd = cmd;
  req.client = client;
  return xQueueSend(this->tx_queue_handle_, &req, 0) == pdPASS;
#else
  return false;
#endif
}

// ─── decode_packet: fast path — pure decoding, no side effects ───────────────
optional<RfPacketInfo> Elero::decode_packet(const uint8_t *buf, size_t buf_len) {
  using namespace packet;

  // Use the existing pure parse_packet() function
  ParseResult r = parse_packet(buf, buf_len);
  if (!r.valid) {
    if (r.reject_reason) {
      ESP_LOGV(TAG, "Packet rejected: %s", r.reject_reason);
    }
    return {};
  }

  // Extract fields relevant to this packet type
  bool is_cmd = is_command_packet(r.type);
  bool is_status_pkt = is_status_packet(r.type);
  bool is_btn = is_button_packet(r.type);
  uint8_t command = (is_cmd || is_btn) ? r.payload[payload_offset::COMMAND] : 0;
  uint8_t state = is_status_pkt ? r.payload[payload_offset::STATE] : 0;

  // Build RfPacketInfo
  RfPacketInfo pkt{};
  pkt.timestamp_ms = millis();
  pkt.src = r.src_addr;
  pkt.dst = r.dst_addr;
  pkt.channel = r.channel;
  pkt.type = r.type;
  pkt.type2 = r.type2;
  pkt.command = command;
  pkt.state = state;
  pkt.cnt = r.counter;
  pkt.rssi = r.rssi;
  pkt.lqi = r.lqi;
  pkt.crc_ok = (r.crc_ok != 0);
  pkt.hop = r.hop;
#ifdef USE_ESP32
  pkt.decoded_at_us = esp_timer_get_time();
#endif
  memcpy(pkt.payload, r.payload, sizeof(pkt.payload));

  // Copy raw bytes for WebSocket broadcast
  size_t raw_total = static_cast<size_t>(r.length) + PACKET_TOTAL_OVERHEAD;
  pkt.raw_len = (raw_total <= CC1101_FIFO_LENGTH) ? static_cast<uint8_t>(raw_total) : CC1101_FIFO_LENGTH;
  memcpy(pkt.raw, buf, pkt.raw_len);

  return pkt;
}

// ─── dispatch_packet: slow path — logging, registry, sensors ─────────────────
void Elero::dispatch_packet(const RfPacketInfo &pkt) {
  using namespace packet;
#ifdef USE_ESP32
  const int64_t dispatch_start_us = esp_timer_get_time();
#endif

  bool is_cmd = is_command_packet(pkt.type);
  bool is_status_pkt = is_status_packet(pkt.type);
  bool is_btn = is_button_packet(pkt.type);

  // Look up device name: for status packets src is the blind, for commands dst is the blind
  std::string blind_name;
  uint32_t blind_addr = is_status_pkt ? pkt.src : pkt.dst;
  if (this->registry_) {
    Device *dev = this->registry_->find(blind_addr);
    if (dev) {
      blind_name = dev->config.name;
    }
  }

  // JSON log: include only fields relevant to the packet type
  char blind_buf[32];
  if (!blind_name.empty()) {
    snprintf(blind_buf, sizeof(blind_buf), "%s", blind_name.c_str());
  } else {
    snprintf(blind_buf, sizeof(blind_buf), "0x%06x", blind_addr);
  }

  if (is_status_pkt) {
    ESP_LOGD(TAG_RF,
             "{\"ts_ms\":%lu,\"dir\":\"rx\",\"blind\":\"%s\",\"state_name\":\"%s\","
             "\"len\":%d,\"cnt\":%d,\"type\":\"0x%02x\",\"type2\":\"0x%02x\",\"hop\":\"0x%02x\","
             "\"channel\":%d,\"src\":\"0x%06x\",\"dst\":\"0x%06x\",\"state\":\"0x%02x\","
             "\"rssi\":%.1f,\"lqi\":%d,\"crc_ok\":%s}",
             (unsigned long) pkt.timestamp_ms, blind_buf, elero_state_to_string(pkt.state),
             pkt.raw_len - PACKET_TOTAL_OVERHEAD, pkt.cnt, pkt.type, pkt.type2, pkt.hop,
             pkt.channel, pkt.src, pkt.dst, pkt.state,
             pkt.rssi, pkt.lqi, pkt.crc_ok ? "true" : "false");
  } else if (is_cmd) {
    ESP_LOGD(TAG_RF,
             "{\"ts_ms\":%lu,\"dir\":\"rx\",\"blind\":\"%s\",\"cmd_name\":\"%s\","
             "\"len\":%d,\"cnt\":%d,\"type\":\"0x%02x\",\"type2\":\"0x%02x\",\"hop\":\"0x%02x\","
             "\"channel\":%d,\"src\":\"0x%06x\",\"dst\":\"0x%06x\",\"command\":\"0x%02x\","
             "\"rssi\":%.1f,\"lqi\":%d,\"crc_ok\":%s}",
             (unsigned long) pkt.timestamp_ms, blind_buf, elero_command_to_string(pkt.command),
             pkt.raw_len - PACKET_TOTAL_OVERHEAD, pkt.cnt, pkt.type, pkt.type2, pkt.hop,
             pkt.channel, pkt.src, pkt.dst, pkt.command,
             pkt.rssi, pkt.lqi, pkt.crc_ok ? "true" : "false");
  } else if (is_btn) {
    ESP_LOGD(TAG_RF,
             "{\"ts_ms\":%lu,\"dir\":\"rx\",\"blind\":\"%s\",\"cmd_name\":\"%s\","
             "\"len\":%d,\"cnt\":%d,\"type\":\"0x%02x\",\"type2\":\"0x%02x\",\"hop\":\"0x%02x\","
             "\"channel\":%d,\"src\":\"0x%06x\",\"dst\":\"0x%06x\",\"command\":\"0x%02x\","
             "\"rssi\":%.1f,\"lqi\":%d,\"crc_ok\":%s}",
             (unsigned long) pkt.timestamp_ms, blind_buf, elero_command_to_string(pkt.command),
             pkt.raw_len - PACKET_TOTAL_OVERHEAD, pkt.cnt, pkt.type, pkt.type2, pkt.hop,
             pkt.channel, pkt.src, pkt.dst, pkt.command,
             pkt.rssi, pkt.lqi, pkt.crc_ok ? "true" : "false");
  } else {
    ESP_LOGD(TAG_RF,
             "{\"ts_ms\":%lu,\"dir\":\"rx\",\"blind\":\"%s\","
             "\"len\":%d,\"cnt\":%d,\"type\":\"0x%02x\",\"type2\":\"0x%02x\",\"hop\":\"0x%02x\","
             "\"channel\":%d,\"src\":\"0x%06x\",\"dst\":\"0x%06x\","
             "\"rssi\":%.1f,\"lqi\":%d,\"crc_ok\":%s}",
             (unsigned long) pkt.timestamp_ms, blind_buf,
             pkt.raw_len - PACKET_TOTAL_OVERHEAD, pkt.cnt, pkt.type, pkt.type2, pkt.hop,
             pkt.channel, pkt.src, pkt.dst,
             pkt.rssi, pkt.lqi, pkt.crc_ok ? "true" : "false");
  }

  // Dispatch through unified device registry (state machines, adapters, observers)
  if (this->registry_ != nullptr) {
    this->registry_->on_rf_packet(pkt, pkt.timestamp_ms);
  }

#ifdef USE_ESP32
  int64_t dispatch_us = esp_timer_get_time() - dispatch_start_us;
  int64_t queue_transit_us = (pkt.decoded_at_us > 0) ? (dispatch_start_us - pkt.decoded_at_us) : 0;
  // Log timing: dispatch cost + queue transit (decode->dispatch latency)
  ESP_LOGD(TAG, "dispatch: %lldus, queue_transit: %lldus (cnt=%d)",
           dispatch_us, queue_transit_us, pkt.cnt);

  // Update stats counters (Core 1 only)
  this->stat_rx_packets_++;
  this->stat_last_rx_ms_ = pkt.timestamp_ms;
  this->stat_dispatch_latency_us_ = static_cast<float>(dispatch_us);
  this->stat_queue_transit_us_ = static_cast<float>(queue_transit_us);
#endif
}


bool Elero::send_raw_command(uint32_t dst_addr, uint32_t src_addr, uint8_t channel, uint8_t command,
                              uint8_t payload_1, uint8_t payload_2, uint8_t type, uint8_t type2, uint8_t hop) {
#ifdef USE_ESP32
  static uint8_t raw_msg_cnt = 1;

  EleroCommand cmd{};
  cmd.counter = raw_msg_cnt;
  cmd.dst_addr = dst_addr;
  cmd.src_addr = src_addr;
  cmd.channel = channel;
  cmd.type = type;
  cmd.type2 = type2;
  cmd.hop = hop;
  cmd.payload[0] = payload_1;
  cmd.payload[1] = payload_2;
  cmd.payload[4] = command;

  // Fire-and-forget via RF task queue (no blocking, no completion callback)
  RfTaskRequest req{};
  req.type = RfTaskRequest::Type::TX;
  req.cmd = cmd;
  req.client = nullptr;  // No completion callback

  if (xQueueSend(this->tx_queue_handle_, &req, 0) == pdPASS) {
    ++raw_msg_cnt;
    if (raw_msg_cnt > packet::limits::COUNTER_MAX)
      raw_msg_cnt = 1;
    return true;
  }
  return false;
#else
  return false;
#endif
}

// ─── RF Stats Publishing ──────────────────────────────────────────────────────
void Elero::publish_stats_() {
#ifdef USE_SENSOR
  uint32_t now = millis();
  if (now - this->last_stats_publish_ms_ < 30000)
    return;
  this->last_stats_publish_ms_ = now;

  if (this->stats_tx_success_)
    this->stats_tx_success_->publish_state(this->stat_tx_success_);
  if (this->stats_tx_fail_)
    this->stats_tx_fail_->publish_state(this->stat_tx_fail_);
  if (this->stats_tx_recover_)
    this->stats_tx_recover_->publish_state(this->stat_tx_recover_.load(std::memory_order_relaxed));
  if (this->stats_rx_packets_)
    this->stats_rx_packets_->publish_state(this->stat_rx_packets_);
  if (this->stats_rx_drops_)
    this->stats_rx_drops_->publish_state(this->stat_rx_drops_.load(std::memory_order_relaxed));
  if (this->stats_fifo_overflows_)
    this->stats_fifo_overflows_->publish_state(this->stat_fifo_overflows_.load(std::memory_order_relaxed));
  if (this->stats_watchdog_)
    this->stats_watchdog_->publish_state(this->stat_watchdog_recoveries_.load(std::memory_order_relaxed));
  if (this->stats_dispatch_latency_)
    this->stats_dispatch_latency_->publish_state(this->stat_dispatch_latency_us_);
  if (this->stats_queue_transit_)
    this->stats_queue_transit_->publish_state(this->stat_queue_transit_us_);
  if (this->stats_last_rx_age_)
    this->stats_last_rx_age_->publish_state(this->stat_last_rx_ms_ > 0 ? static_cast<float>(now - this->stat_last_rx_ms_) : -1.0f);
#endif
}

}  // namespace elero
}  // namespace esphome
