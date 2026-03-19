#include "elero.h"
#include "elero_protocol.h"
#include "elero_packet.h"
#include "elero_strings.h"
#include "device.h"
#include "device_registry.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cstring>
#include <algorithm>

#ifdef USE_ESP32
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <esp32/rom/ets_sys.h>
#endif

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "state_snapshot.h"
#endif

namespace esphome {
namespace elero {

static const char *const TAG = "elero";
static const char *const TAG_RF = "elero.rf";

// ─── SpiTransaction RAII Implementation ───────────────────────────────────
SpiTransaction::SpiTransaction(Elero *device) : device_(device) {
  device_->enable();
}

SpiTransaction::~SpiTransaction() {
  device_->disable();
}

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

// ─── FIFO Drain: read all bytes, decode multiple packets ─────────────────────
void Elero::drain_fifo_() {
  ESP_LOGVV(TAG, "drain_fifo_");
#ifdef USE_ESP32
  int64_t drain_start_us = esp_timer_get_time();
#endif
  uint8_t len = this->read_status_reliable_(CC1101_RXBYTES);

  // Overflow — FIFO data unreliable, flush and bail
  if (len & packet::cc1101_status::RXBYTES_OVERFLOW_BIT) {
    ESP_LOGV(TAG, "Rx overflow, flushing FIFOs");
    this->stat_fifo_overflows_.fetch_add(1, std::memory_order_relaxed);
    this->flush_and_rx();
    return;
  }

  uint8_t fifo_count = len & packet::cc1101_status::BYTE_COUNT_MASK;
  if (fifo_count == 0) {
    return;
  }

  // Read entire FIFO in one SPI burst
  if (fifo_count > CC1101_FIFO_LENGTH) {
    ESP_LOGV(TAG, "RXBYTES > FIFO length (%d), clamping", fifo_count);
    fifo_count = CC1101_FIFO_LENGTH;
  }
  this->read_buf(CC1101_RXFIFO, this->msg_rx_, fifo_count);

  // Log raw bytes at VERBOSE level for analysis
  ESP_LOGV(TAG, "RAW RX %d bytes: %s", fifo_count,
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
    ESP_LOGD(TAG, "Decoded %d packets from single FIFO read (%d bytes)", pkt_count, fifo_count);
  }

#ifdef USE_ESP32
  if (pkt_count > 0) {
    int64_t drain_us = esp_timer_get_time() - drain_start_us;
    ESP_LOGD(TAG, "drain_fifo: %d pkt(s), %d bytes, %lldus", pkt_count, fifo_count, drain_us);
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
  ESP_LOGCONFIG(TAG, "Elero CC1101:");
  ESP_LOGCONFIG(TAG, "  Version: %s", this->version_);
  LOG_PIN("  GDO0 Pin: ", this->gdo0_pin_);
  ESP_LOGCONFIG(TAG, "  freq2: 0x%02x, freq1: 0x%02x, freq0: 0x%02x",
                this->freq2_.load(), this->freq1_.load(), this->freq0_.load());
  if (this->registry_) {
    ESP_LOGCONFIG(TAG, "  Registered devices: %d", this->registry_->count_active());
  }
}

void Elero::setup() {
  ESP_LOGI(TAG, "Setting up Elero Component...");
  this->spi_setup();
  this->gdo0_pin_->setup();
  this->gdo0_pin_->attach_interrupt(Elero::interrupt, this, gpio::INTERRUPT_FALLING_EDGE);
  this->reset();

  // Wait for crystal oscillator to stabilize after reset (CC1101 datasheet: ~1ms typical)
  delay(5);

  // Verify CC1101 is responding — known versions: 0x04, 0x14, 0x82 (per RadioLib)
  uint8_t version = this->read_status(CC1101_VERSION);
  if (version == packet::cc1101_status::VERSION_NOT_CONNECTED_LOW ||
      version == packet::cc1101_status::VERSION_NOT_CONNECTED_HIGH) {
    ESP_LOGE(TAG, "CC1101 not found (VERSION=0x%02x). Check SPI wiring and CS pin.", version);
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "CC1101 version: 0x%02x", version);

  this->init();

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

  for (;;) {
    uint32_t now = millis();

    // 1. Process TX requests from main loop (only when radio is idle)
    if (self->tx_ctx_.state == TxState::IDLE) {
      RfTaskRequest req{};
      if (xQueueReceive(self->tx_queue_handle_, &req, 0) == pdPASS) {
        switch (req.type) {
          case RfTaskRequest::Type::TX:
            self->build_tx_packet_(req.cmd);
            self->record_tx_(req.cmd.counter);
            if (self->start_transmit()) {
              self->tx_owner_ = req.client;
            } else {
              // start_transmit failed — report failure immediately
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
            }
            self->received_.store(false);
            self->freq2_.store(req.freq.f2);
            self->freq1_.store(req.freq.f1);
            self->freq0_.store(req.freq.f0);
            self->reset();
            self->init();
            ESP_LOGI(TAG, "CC1101 re-initialised: freq2=0x%02x freq1=0x%02x freq0=0x%02x",
                     req.freq.f2, req.freq.f1, req.freq.f0);
            break;
        }
      }
    }

    // 2. Progress TX state machine
    now = millis();
    if (self->tx_ctx_.state != TxState::IDLE) {
      self->handle_tx_state_(now);

      // Check if TX just completed — post result to main loop
      if (self->tx_ctx_.state == TxState::IDLE && self->tx_owner_ != nullptr) {
        TxResult r{self->tx_owner_, self->tx_pending_success_};
        self->tx_owner_ = nullptr;
        xQueueSend(self->tx_done_queue_handle_, &r, 0);
      }
    }

    // 3. Drain FIFO if GDO0 interrupt fired
    if (self->received_.exchange(false)) {
      self->drain_fifo_();
    }

    // 4. Radio health check (only when idle, throttled internally to every 5s)
    if (self->tx_ctx_.state == TxState::IDLE) {
      self->check_radio_health_();
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
  xQueueSend(this->tx_queue_handle_, &req, pdMS_TO_TICKS(100));

  // Update atomic copies for get_freq*() accessors (immediate visibility on Core 1)
  this->freq2_.store(freq2);
  this->freq1_.store(freq1);
  this->freq0_.store(freq0);
  ESP_LOGI(TAG, "Frequency change queued: freq2=0x%02x freq1=0x%02x freq0=0x%02x", freq2, freq1, freq0);
#endif
}

void Elero::flush_and_rx() {
  ESP_LOGVV(TAG, "flush_and_rx");

  // 1. Force IDLE
  (void) this->write_cmd(CC1101_SIDLE);
  esp_rom_delay_us(100);

  // 2. Clear atomic flag (safe — radio is idle, no new interrupts)
  this->received_.store(false, std::memory_order_release);

  // 3. Flush both FIFOs
  (void) this->write_cmd(CC1101_SFRX);
  (void) this->write_cmd(CC1101_SFTX);

  // 4. Re-enable RX
  (void) this->write_cmd(CC1101_SRX);

  // 5. Verify radio entered RX — caller (recover_radio_) handles escalation
  uint8_t marc = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
  if (marc != CC1101_MARCSTATE_RX) {
    ESP_LOGW(TAG, "flush_and_rx: not in RX after SRX, MARCSTATE=0x%02x", marc);
  }
}

void Elero::reset() {
  // We don't do a hardware reset as we can't read
  // the MISO pin directly. Rely on software-reset only.

  this->enable();
  this->write_byte(CC1101_SRES);
  delay_microseconds_safe(50);
  this->write_byte(CC1101_SIDLE);
  delay_microseconds_safe(50);
  this->disable();
}

void Elero::init() {
  // PA table: +10 dBm output power for all 8 power levels
  uint8_t patable_data[] = {0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0};

  // CC1101 register configuration for Elero protocol (868 MHz, 2-FSK, 9.6 kBaud).
  // Values derived from TI SmartRF Studio and Elero protocol reverse-engineering.
  // Reference: https://github.com/QuadCorei8085/elero_protocol
  (void) this->write_reg(CC1101_FSCTRL1, 0x08);
  (void) this->write_reg(CC1101_FSCTRL0, 0x00);
  (void) this->write_reg(CC1101_FREQ2, this->freq2_.load());
  (void) this->write_reg(CC1101_FREQ1, this->freq1_.load());
  (void) this->write_reg(CC1101_FREQ0, this->freq0_.load());
  (void) this->write_reg(CC1101_MDMCFG4, 0x7B);
  (void) this->write_reg(CC1101_MDMCFG3, 0x83);
  (void) this->write_reg(CC1101_MDMCFG2, 0x13);
  (void) this->write_reg(CC1101_MDMCFG1, 0x52);
  (void) this->write_reg(CC1101_MDMCFG0, 0xF8);
  (void) this->write_reg(CC1101_CHANNR, 0x00);
  (void) this->write_reg(CC1101_DEVIATN, 0x43);
  (void) this->write_reg(CC1101_FREND1, 0xB6);
  (void) this->write_reg(CC1101_FREND0, 0x10);
  (void) this->write_reg(CC1101_MCSM0, 0x18);
  (void) this->write_reg(CC1101_MCSM1, 0x3F);
  (void) this->write_reg(CC1101_FOCCFG, 0x1D);
  (void) this->write_reg(CC1101_BSCFG, 0x1F);
  (void) this->write_reg(CC1101_AGCCTRL2, 0xC7);
  (void) this->write_reg(CC1101_AGCCTRL1, 0x00);
  (void) this->write_reg(CC1101_AGCCTRL0, 0xB2);
  (void) this->write_reg(CC1101_FSCAL3, 0xEA);
  (void) this->write_reg(CC1101_FSCAL2, 0x2A);
  (void) this->write_reg(CC1101_FSCAL1, 0x00);
  (void) this->write_reg(CC1101_FSCAL0, 0x1F);
  (void) this->write_reg(CC1101_FSTEST, 0x59);
  (void) this->write_reg(CC1101_TEST2, 0x81);
  (void) this->write_reg(CC1101_TEST1, 0x35);
  (void) this->write_reg(CC1101_TEST0, 0x09);
  (void) this->write_reg(CC1101_IOCFG0, 0x06);
  (void) this->write_reg(CC1101_PKTCTRL1, 0x8C);
  (void) this->write_reg(CC1101_PKTCTRL0, 0x45);
  (void) this->write_reg(CC1101_ADDR, 0x00);
  (void) this->write_reg(CC1101_PKTLEN, 0x3C);
  (void) this->write_reg(CC1101_SYNC1, 0xD3);
  (void) this->write_reg(CC1101_SYNC0, 0x91);
  (void) this->write_burst(CC1101_PATABLE, patable_data, 8);

  (void) this->write_cmd(CC1101_SRX);
  (void) this->wait_rx();
}

bool Elero::write_reg(uint8_t addr, uint8_t data) {
  uint8_t status;
  {
    SpiTransaction txn(this);
    status = this->transfer_byte(addr);
    this->write_byte(data);
  }  // CS released here
  delay_microseconds_safe(15);

  // Check SPI status byte state field (bits 6:4) for RXFIFO overflow
  if ((status & packet::cc1101_status::SPI_STATE_MASK) == packet::cc1101_status::SPI_STATE_RXFIFO_OVERFLOW) {
    ESP_LOGW(TAG, "SPI write_reg 0x%02x: RXFIFO overflow (status=0x%02x)", addr, status);
    return false;
  }
  return true;
}

bool Elero::write_burst(uint8_t addr, uint8_t *data, uint8_t len) {
  uint8_t status;
  {
    SpiTransaction txn(this);
    status = this->transfer_byte(addr | CC1101_WRITE_BURST);
    for (int i = 0; i < len; ++i) {
      this->write_byte(data[i]);
    }
  }  // CS released here
  delay_microseconds_safe(15);

  // Check SPI status byte state field (bits 6:4) for RXFIFO overflow
  if ((status & packet::cc1101_status::SPI_STATE_MASK) == packet::cc1101_status::SPI_STATE_RXFIFO_OVERFLOW) {
    ESP_LOGW(TAG, "SPI write_burst 0x%02x (%d bytes): RXFIFO overflow (status=0x%02x)", addr, len, status);
    return false;
  }
  return true;
}

bool Elero::write_cmd(uint8_t cmd) {
  uint8_t status;
  {
    SpiTransaction txn(this);
    status = this->transfer_byte(cmd);
  }  // CS released here
  delay_microseconds_safe(15);

  // Check SPI status byte state field (bits 6:4) for RXFIFO overflow
  if ((status & packet::cc1101_status::SPI_STATE_MASK) == packet::cc1101_status::SPI_STATE_RXFIFO_OVERFLOW) {
    ESP_LOGW(TAG, "SPI write_cmd 0x%02x: RXFIFO overflow (status=0x%02x)", cmd, status);
    return false;
  }
  return true;
}

bool Elero::wait_rx() {
  ESP_LOGVV(TAG, "wait_rx");
  uint8_t timeout = 200;
  while ((this->read_status(CC1101_MARCSTATE) != CC1101_MARCSTATE_RX) && (--timeout != 0)) {
    delay_microseconds_safe(200);
  }

  if (timeout > 0)
    return true;
  ESP_LOGE(TAG, "Timed out waiting for RX: 0x%02x", this->read_status(CC1101_MARCSTATE));
  return false;
}

// ─── Non-blocking TX State Machine ─────────────────────────────────────────

void Elero::recover_radio_() {
  ESP_LOGW(TAG, "recover_radio_: flushing and checking radio state");
  this->stat_tx_recover_.fetch_add(1, std::memory_order_relaxed);

  // 1. Flush FIFOs and return to RX
  this->flush_and_rx();

  // 2. Check if radio recovered
  uint8_t marc = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
  if (marc != CC1101_MARCSTATE_RX) {
    // Radio didn't recover — full reset + init
    ESP_LOGE(TAG, "Radio not in RX after flush (MARCSTATE=0x%02x), resetting chip", marc);
    this->reset();
    this->init();

    // Verify radio came back — read VERSION register to confirm SPI alive
    uint8_t version = this->read_status(CC1101_VERSION);
    if (version == packet::cc1101_status::VERSION_NOT_CONNECTED_LOW ||
        version == packet::cc1101_status::VERSION_NOT_CONNECTED_HIGH) {
      ESP_LOGE(TAG, "Radio failed to recover after reset (VERSION=0x%02x)", version);
    }
  }

  // 3. Return to IDLE state with failure
  this->tx_ctx_.state = TxState::IDLE;
  this->tx_pending_success_ = false;
  // NOTE: tx_owner_ is NOT cleared here — the rf_task_func_ loop detects
  // tx_ctx_.state == IDLE && tx_owner_ != nullptr and posts TxResult to
  // tx_done_queue_, which the main loop drains to call on_tx_complete().
}

// ─── Radio Health Watchdog ───────────────────────────────────────────────────
// Periodically checks MARCSTATE when idle to detect and recover from stuck
// CC1101 states that don't trigger interrupts. Covers:
// - RXFIFO overflow without ISR (deaf radio)
// - Stuck in IDLE (stopped listening)
// - Any other unexpected state
void Elero::check_radio_health_() {
  uint32_t now = millis();
  if (now - this->last_radio_check_ms_ < packet::timing::RADIO_WATCHDOG_INTERVAL) {
    return;
  }
  this->last_radio_check_ms_ = now;

  uint8_t marc = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;

  // RX is the expected idle state
  if (marc == CC1101_MARCSTATE_RX) {
    return;
  }

  // Transient calibration/synthesizer states — let them complete
  if (marc >= CC1101_MARCSTATE_VCOON_MC && marc <= CC1101_MARCSTATE_ENDCAL) {
    return;
  }
  // RX wind-down states are also transient
  if (marc == CC1101_MARCSTATE_RX_END || marc == CC1101_MARCSTATE_RX_RST) {
    return;
  }

  // RXFIFO overflow — radio is deaf until flushed
  if (marc == CC1101_MARCSTATE_RXFIFO_OFLOW) {
    ESP_LOGW(TAG, "Radio watchdog: RX FIFO overflow, flushing");
    this->stat_watchdog_recoveries_.fetch_add(1, std::memory_order_relaxed);
    this->flush_and_rx();
    return;
  }

  // Stuck in IDLE — radio stopped listening, one strobe restarts it
  if (marc == CC1101_MARCSTATE_IDLE) {
    ESP_LOGW(TAG, "Radio watchdog: stuck in IDLE, restarting RX");
    this->stat_watchdog_recoveries_.fetch_add(1, std::memory_order_relaxed);
    (void) this->write_cmd(CC1101_SRX);
    return;
  }

  // Anything else unexpected — full reset to known-good RX state
  ESP_LOGW(TAG, "Radio watchdog: unexpected MARCSTATE 0x%02x, reinitializing", marc);
  this->stat_watchdog_recoveries_.fetch_add(1, std::memory_order_relaxed);
  this->flush_and_rx();
}

void Elero::record_tx_(uint8_t counter) {
  this->tx_history_[this->tx_history_idx_] = counter;
  this->tx_history_idx_ = (this->tx_history_idx_ + 1) % TX_HISTORY_SIZE;
}

bool Elero::is_own_echo_(uint8_t counter) const {
  for (size_t i = 0; i < TX_HISTORY_SIZE; i++) {
    if (this->tx_history_[i] == counter) {
      return true;
    }
  }
  return false;
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

bool Elero::start_transmit() {
  if (this->tx_ctx_.state != TxState::IDLE) {
    return false;  // Already transmitting
  }

  ESP_LOGVV(TAG, "start_transmit called for %d data bytes", this->msg_tx_[0]);

  // Transition to PREPARE — synchronous pre-TX sequence
  this->tx_ctx_.state = TxState::PREPARE;
  this->tx_ctx_.state_enter_time = millis();
  this->tx_pending_success_ = false;

  return true;
}

void Elero::handle_tx_state_(uint32_t now) {
  if (this->tx_ctx_.state == TxState::IDLE) {
    return;
  }

  uint32_t elapsed = now - this->tx_ctx_.state_enter_time;
  uint8_t marcstate;

  switch (this->tx_ctx_.state) {
    case TxState::PREPARE: {
      // Synchronous pre-TX sequence (~1ms total)

      // 1. SIDLE
      (void) this->write_cmd(CC1101_SIDLE);

      // 2. Poll MARCSTATE == IDLE, max ~1ms
      bool reached_idle = false;
      for (int i = 0; i < 20; ++i) {
        esp_rom_delay_us(50);
        marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
        if (marcstate == CC1101_MARCSTATE_IDLE) {
          reached_idle = true;
          break;
        }
      }
      if (!reached_idle) {
        marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
        ESP_LOGW(TAG, "PREPARE: SIDLE failed, MARCSTATE=0x%02x", marcstate);
        this->tx_ctx_.state = TxState::RECOVER;
        this->tx_ctx_.state_enter_time = now;
        return;
      }

      // 3. Flush TX FIFO
      (void) this->write_cmd(CC1101_SFTX);
      esp_rom_delay_us(100);

      // 4. Load TX FIFO
      if (!this->write_burst(CC1101_TXFIFO, this->msg_tx_, this->msg_tx_[0] + 1)) {
        ESP_LOGW(TAG, "PREPARE: FIFO write failed");
        this->tx_ctx_.state = TxState::RECOVER;
        this->tx_ctx_.state_enter_time = now;
        return;
      }

      // 5. Clear received_ so we can detect TX-end interrupt
      this->received_.store(false);

      // 6. Trigger TX
      (void) this->write_cmd(CC1101_STX);

      // 7. Poll MARCSTATE == TX, max ~1ms (covers ~700µs calibration)
      for (int i = 0; i < 20; ++i) {
        esp_rom_delay_us(50);
        marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
        if (marcstate == CC1101_MARCSTATE_TX) {
          this->tx_ctx_.state = TxState::WAIT_TX;
          this->tx_ctx_.state_enter_time = now;
          return;
        }
      }

      // STX didn't reach TX state
      marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
      ESP_LOGW(TAG, "PREPARE: STX failed, MARCSTATE=0x%02x", marcstate);
      this->tx_ctx_.state = TxState::RECOVER;
      this->tx_ctx_.state_enter_time = now;
      break;
    }

    case TxState::WAIT_TX:
      // Interrupt-driven with MARCSTATE polling fallback
      if (this->received_.load()) {
        // GDO0 interrupt fired — TX likely complete, verify FIFO empty
        esp_rom_delay_us(50);
        uint8_t txbytes = this->read_status_reliable_(CC1101_TXBYTES) & packet::cc1101_status::BYTE_COUNT_MASK;
        if (txbytes == 0) {
          ESP_LOGV(TAG, "TX successful");
          this->flush_and_rx();
          this->tx_pending_success_ = true;
          this->tx_ctx_.state = TxState::IDLE;
          return;
        }
        // Grace window — GDO0 may fire slightly before FIFO fully drains
        esp_rom_delay_us(100);
        txbytes = this->read_status_reliable_(CC1101_TXBYTES) & packet::cc1101_status::BYTE_COUNT_MASK;
        if (txbytes == 0) {
          ESP_LOGV(TAG, "TX successful (after grace)");
          this->flush_and_rx();
          this->tx_pending_success_ = true;
          this->tx_ctx_.state = TxState::IDLE;
          return;
        }
        ESP_LOGE(TAG, "FIFO not empty after TX interrupt, txbytes=%u", txbytes);
        this->tx_ctx_.state = TxState::RECOVER;
        this->tx_ctx_.state_enter_time = now;
        return;
      }

      // MARCSTATE polling fallback — detect TX completion if GDO0 was missed
      marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
      if (marcstate == CC1101_MARCSTATE_IDLE || marcstate == CC1101_MARCSTATE_RX) {
        uint8_t txbytes = this->read_status_reliable_(CC1101_TXBYTES) & packet::cc1101_status::BYTE_COUNT_MASK;
        if (txbytes == 0) {
          ESP_LOGV(TAG, "TX successful (MARCSTATE fallback)");
          this->flush_and_rx();
          this->tx_pending_success_ = true;
          this->tx_ctx_.state = TxState::IDLE;
          return;
        }
      }

      // Timeout
      if (elapsed > TxContext::STATE_TIMEOUT_MS) {
        ESP_LOGE(TAG, "TX timeout in WAIT_TX after %ums", elapsed);
        this->tx_ctx_.state = TxState::RECOVER;
        this->tx_ctx_.state_enter_time = now;
      }
      break;

    case TxState::RECOVER:
      this->recover_radio_();
      break;

    case TxState::IDLE:
      break;
  }
}

uint8_t Elero::read_reg(uint8_t addr, bool *ok) {
  // read_reg() uses CC1101_READ_SINGLE which does NOT set the 0x40 bit needed
  // for status registers (addr > 0x2E). Use read_status() for those.
  if (addr > CC1101_TEST0) {
    ESP_LOGW(TAG, "read_reg(0x%02x): addr > 0x2E is a status register, use read_status() instead", addr);
  }

  uint8_t status;
  uint8_t data;
  {
    SpiTransaction txn(this);
    status = this->transfer_byte(addr | CC1101_READ_SINGLE);
    data = this->read_byte();
  }  // CS released here
  delay_microseconds_safe(15);

  // Check SPI status byte state field (bits 6:4) for RXFIFO overflow
  if ((status & packet::cc1101_status::SPI_STATE_MASK) == packet::cc1101_status::SPI_STATE_RXFIFO_OVERFLOW) {
    ESP_LOGW(TAG, "SPI read_reg 0x%02x: RXFIFO overflow (status=0x%02x)", addr, status);
    if (ok != nullptr) {
      *ok = false;
    }
  } else if (ok != nullptr) {
    *ok = true;
  }
  return data;
}

uint8_t Elero::read_status(uint8_t addr) {
  // CC1101 datasheet section 10.4: status registers (addr > 0x2E) require the
  // burst access bit (0x40) to be set. CC1101_READ_BURST (0xC0) includes this
  // bit, so this works correctly for both status registers and burst reads.
  uint8_t data;
  {
    SpiTransaction txn(this);
    this->write_byte(addr | CC1101_READ_BURST);
    data = this->read_byte();
  }  // CS released here
  delay_microseconds_safe(15);
  return data;
}

uint8_t Elero::read_status_reliable_(uint8_t addr) {
  // CC1101 errata: RXBYTES and TXBYTES can return incorrect values on a single read.
  // Workaround (per RadioLib): read twice until both values match.
  uint8_t a, b;
  do {
    a = this->read_status(addr);
    b = this->read_status(addr);
  } while (a != b);
  return a;
}

void Elero::read_buf(uint8_t addr, uint8_t *buf, uint8_t len) {
  {
    SpiTransaction txn(this);
    this->write_byte(addr | CC1101_READ_BURST);
    for (uint8_t i = 0; i < len; ++i) {
      buf[i] = this->read_byte();
    }
  }  // CS released here
  delay_microseconds_safe(15);
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
  bool echo = is_cmd && this->is_own_echo_(r.counter);

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
  pkt.echo = echo;
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
             "{\"ts_ms\":%lu,\"dir\":\"rx\",\"blind\":\"%s\",\"cmd_name\":\"%s\",\"echo\":%s,"
             "\"len\":%d,\"cnt\":%d,\"type\":\"0x%02x\",\"type2\":\"0x%02x\",\"hop\":\"0x%02x\","
             "\"channel\":%d,\"src\":\"0x%06x\",\"dst\":\"0x%06x\",\"command\":\"0x%02x\","
             "\"rssi\":%.1f,\"lqi\":%d,\"crc_ok\":%s}",
             (unsigned long) pkt.timestamp_ms, blind_buf, elero_command_to_string(pkt.command),
             pkt.echo ? "true" : "false",
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

  // Notify direct RF callback (web server → WebSocket broadcast)
  if (this->on_rf_packet_) {
    this->on_rf_packet_(pkt);
  }

  // Update RSSI sensor for any message from a known blind
#ifdef USE_SENSOR
  {
    auto rssi_it = this->address_to_rssi_sensor_.find(pkt.src);
    if (rssi_it != this->address_to_rssi_sensor_.end()) {
      rssi_it->second->publish_state(pkt.rssi);
    }
  }
#endif

  // Update text sensor for status packets
#ifdef USE_TEXT_SENSOR
  if (is_status_pkt) {
    auto text_it = this->address_to_text_sensor_.find(pkt.src);
    if (text_it != this->address_to_text_sensor_.end()) {
      text_it->second->publish_state(elero_state_to_string(pkt.state));
    }
  }

#endif

  // Update problem binary sensor for status packets
#ifdef USE_BINARY_SENSOR
  if (is_status_pkt) {
    auto problem_it = this->address_to_problem_sensor_.find(pkt.src);
    if (problem_it != this->address_to_problem_sensor_.end()) {
      problem_it->second->publish_state(is_problem_state(pkt.state));
    }
  }
#endif

#ifdef USE_ESP32
  int64_t dispatch_us = esp_timer_get_time() - dispatch_start_us;
  int64_t queue_transit_us = (pkt.decoded_at_us > 0) ? (dispatch_start_us - pkt.decoded_at_us) : 0;
  // Log timing: dispatch cost + queue transit (decode→dispatch latency)
  ESP_LOGD(TAG, "dispatch: %lldus, queue_transit: %lldus (cnt=%d)",
           dispatch_us, queue_transit_us, pkt.cnt);

  // Update stats counters (Core 1 only)
  this->stat_rx_packets_++;
  this->stat_last_rx_ms_ = pkt.timestamp_ms;
  this->stat_dispatch_latency_us_ = static_cast<float>(dispatch_us);
  this->stat_queue_transit_us_ = static_cast<float>(queue_transit_us);
#endif
}

#ifdef USE_SENSOR
void Elero::register_rssi_sensor(uint32_t address, sensor::Sensor *sensor) {
  this->address_to_rssi_sensor_[address] = sensor;
}
#endif

#ifdef USE_TEXT_SENSOR
void Elero::register_text_sensor(uint32_t address, text_sensor::TextSensor *sensor) {
  this->address_to_text_sensor_[address] = sensor;
}

#endif

#ifdef USE_BINARY_SENSOR
void Elero::register_problem_sensor(uint32_t address, binary_sensor::BinarySensor *sensor) {
  this->address_to_problem_sensor_[address] = sensor;
}
#endif

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
