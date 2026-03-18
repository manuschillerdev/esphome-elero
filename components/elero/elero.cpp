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
  const uint32_t now = millis();

  // Progress TX state machine (non-blocking, must run every loop)
  if (this->tx_ctx_.state != TxState::IDLE) {
    this->handle_tx_state_(now);

    // Check if TX just completed (for non-blocking API)
    if (this->tx_ctx_.state == TxState::IDLE && this->tx_owner_ != nullptr) {
      bool success = this->tx_pending_success_;
      ESP_LOGV(TAG, "TX complete, notifying owner, success=%d", success);
      this->notify_tx_owner_(success);
    }
    // Dispatch queued RX packets even during TX (don't let them stale)
    this->dispatch_queued_packets_();
    return;
  }

  // Periodic radio health check (detect stuck CC1101 when idle)
  this->check_radio_health_();

  // Phase 1 — FAST: drain FIFO, decode, push to ring buffer
  bool was_received = this->received_.exchange(false);
  if (was_received) {
    this->drain_fifo_();
  }

  // Phase 2 — SLOW: dispatch buffered packets (logging, registry, sensors)
  this->dispatch_queued_packets_();

  // New architecture: process device registry loop (polling, timeouts, command queues)
  if (this->registry_ != nullptr) {
    this->registry_->loop(millis());
  }
}

// ─── FIFO Drain: read all bytes, decode multiple packets ─────────────────────
void Elero::drain_fifo_() {
  ESP_LOGVV(TAG, "drain_fifo_");
  uint8_t len = this->read_status_reliable_(CC1101_RXBYTES);

  // Overflow — FIFO data unreliable, flush and bail
  if (len & packet::cc1101_status::RXBYTES_OVERFLOW_BIT) {
    ESP_LOGV(TAG, "Rx overflow, flushing FIFOs");
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
      if (!this->rx_queue_.push(*pkt)) {
        ESP_LOGW(TAG, "RX ring buffer full, dropping packet cnt=%d", pkt->cnt);
      }
      ++pkt_count;
    }

    offset += total;
  }

  if (pkt_count > 1) {
    ESP_LOGD(TAG, "Decoded %d packets from single FIFO read (%d bytes)", pkt_count, fifo_count);
  }
}

// ─── Dispatch queued packets (slow path) ─────────────────────────────────────
// NOTE: This is safe to call during TX (from loop() line 55). Registry callbacks
// that call send_command()/request_tx() will be rejected because tx_owner_ != nullptr
// or tx_ctx_.state != IDLE. The CommandSender retries on the next loop iteration.
void Elero::dispatch_queued_packets_() {
  RfPacketInfo pkt{};
  while (this->rx_queue_.pop(pkt)) {
    this->dispatch_packet(pkt);
  }
}

void IRAM_ATTR Elero::interrupt(Elero *arg) {
  arg->set_received();
}

void IRAM_ATTR Elero::set_received() {
  this->received_.store(true, std::memory_order_release);
}

void Elero::dump_config() {
  ESP_LOGCONFIG(TAG, "Elero CC1101:");
  ESP_LOGCONFIG(TAG, "  Version: %s", this->version_);
  LOG_PIN("  GDO0 Pin: ", this->gdo0_pin_);
  ESP_LOGCONFIG(TAG, "  freq2: 0x%02x, freq1: 0x%02x, freq0: 0x%02x", this->freq2_, this->freq1_, this->freq0_);
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
}

void Elero::reinit_frequency(uint8_t freq2, uint8_t freq1, uint8_t freq0) {
  // Abort any pending TX first (notifies owner with failure)
  if (this->tx_owner_ != nullptr || this->tx_ctx_.state != TxState::IDLE) {
    ESP_LOGW(TAG, "reinit_frequency: aborting pending TX");
    this->abort_tx_();
  }

  this->received_.store(false);
  this->freq2_ = freq2;
  this->freq1_ = freq1;
  this->freq0_ = freq0;
  this->reset();
  this->init();
  ESP_LOGI(TAG, "CC1101 re-initialised: freq2=0x%02x freq1=0x%02x freq0=0x%02x", freq2, freq1, freq0);
}

void Elero::flush_and_rx() {
  ESP_LOGVV(TAG, "flush_and_rx");

  // Check if there's RX data before flushing - don't discard valid packets!
  uint8_t rx_bytes = this->read_status_reliable_(CC1101_RXBYTES);
  if ((rx_bytes & packet::cc1101_status::BYTE_COUNT_MASK) > 0 &&
      !(rx_bytes & packet::cc1101_status::RXBYTES_OVERFLOW_BIT)) {
    // Valid data in RX FIFO (no overflow) - rescue via drain_fifo_
    ESP_LOGV(TAG, "flush_and_rx: rescuing %d bytes from RX FIFO",
             rx_bytes & packet::cc1101_status::BYTE_COUNT_MASK);
    this->drain_fifo_();
  }

  // 1. Force IDLE to stop any current radio activity
  (void) this->write_cmd(CC1101_SIDLE);

  // 2. Wait for IDLE with bounded timeout (no GDO0 edges possible in IDLE)
  uint32_t start = millis();
  bool reached_idle = false;
  while (true) {
    uint8_t marc = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
    if (marc == CC1101_MARCSTATE_IDLE) {
      reached_idle = true;
      break;
    }
    if (millis() - start > TxContext::STATE_TIMEOUT_MS) {
      ESP_LOGE(TAG, "flush_and_rx: wait_idle timeout, MARCSTATE=0x%02x", marc);
      break;
    }
    delay_microseconds_safe(50);
  }
  if (!reached_idle) {
    ESP_LOGW(TAG, "flush_and_rx: proceeding after timeout (best-effort recovery)");
  }

  // 3. Clear atomic flag while in IDLE (safe window - no new interrupts)
  this->received_.store(false, std::memory_order_release);

  // 4. Flush FIFOs
  (void) this->write_cmd(CC1101_SFRX);
  (void) this->write_cmd(CC1101_SFTX);

  // 5. Re-enable RX
  (void) this->write_cmd(CC1101_SRX);
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
  (void) this->write_reg(CC1101_FREQ2, this->freq2_);
  (void) this->write_reg(CC1101_FREQ1, this->freq1_);
  (void) this->write_reg(CC1101_FREQ0, this->freq0_);
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

bool Elero::wait_idle() {
  ESP_LOGVV(TAG, "wait_idle");
  uint8_t timeout = 200;
  while ((this->read_status(CC1101_MARCSTATE) != CC1101_MARCSTATE_IDLE) && (--timeout != 0)) {
    delay_microseconds_safe(200);
  }

  if (timeout > 0)
    return true;
  ESP_LOGE(TAG, "Timed out waiting for Idle: 0x%02x", this->read_status(CC1101_MARCSTATE));
  return false;
}

bool Elero::wait_tx() {
  ESP_LOGVV(TAG, "wait_tx");
  uint8_t timeout = 200;

  while ((this->read_status(CC1101_MARCSTATE) != CC1101_MARCSTATE_TX) && (--timeout != 0)) {
    delay_microseconds_safe(200);
  }

  if (timeout > 0)
    return true;
  ESP_LOGE(TAG, "Timed out waiting for TX: 0x%02x", this->read_status(CC1101_MARCSTATE));
  return false;
}

bool Elero::wait_tx_done() {
  ESP_LOGVV(TAG, "wait_tx_done");
  uint8_t timeout = 200;

  while ((!this->received_.load()) && (--timeout != 0)) {
    delay_microseconds_safe(200);
  }

  if (timeout > 0)
    return true;
  ESP_LOGE(TAG, "Timed out waiting for TX Done: 0x%02x", this->read_status(CC1101_MARCSTATE));
  return false;
}

bool Elero::transmit() {
  ESP_LOGVV(TAG, "transmit called for %d data bytes", this->msg_tx_[0]);

  // Go to IDLE first so the subsequent STX is not subject to CCA.
  // (STX from RX with MCSM1 CCA_MODE=3 requires a clear channel, which
  // fails when Elero motors are actively transmitting status replies.)
  if (!this->write_cmd(CC1101_SIDLE)) {
    this->flush_and_rx();
    return false;
  }
  if (!this->wait_idle()) {
    this->flush_and_rx();
    return false;
  }

  // Flush TX FIFO before loading new data (required from IDLE state)
  if (!this->write_cmd(CC1101_SFTX)) {
    this->flush_and_rx();
    return false;
  }
  delay_microseconds_safe(100);

  // Load TX FIFO
  if (!this->write_burst(CC1101_TXFIFO, this->msg_tx_, this->msg_tx_[0] + 1)) {
    this->flush_and_rx();
    return false;
  }

  // Clear received_ so wait_tx_done() waits for the actual TX-end GDO0
  // falling edge, not a stale flag left over from a previously received packet.
  this->received_.store(false);

  // Trigger TX — no CCA check when issuing STX from IDLE state
  if (!this->write_cmd(CC1101_STX)) {
    this->flush_and_rx();
    return false;
  }

  if (!this->wait_tx()) {
    this->flush_and_rx();
    return false;
  }
  if (!this->wait_tx_done()) {
    this->flush_and_rx();
    return false;
  }

  uint8_t bytes = this->read_status_reliable_(CC1101_TXBYTES) & packet::cc1101_status::BYTE_COUNT_MASK;
  if (bytes != 0) {
    ESP_LOGE(TAG, "Error transferring, %d bytes left in buffer", bytes);
    this->flush_and_rx();
    return false;
  }

  ESP_LOGV(TAG, "Transmission successful");
  this->flush_and_rx();  // return chip to clean RX state and clear received_
  return true;
}

// ─── Non-blocking TX State Machine ─────────────────────────────────────────

void Elero::defer_tx_(uint32_t now) {
  this->tx_ctx_.defer_count++;

  // Apply backoff if deferred multiple times
  if (this->tx_ctx_.defer_count > TxContext::DEFER_BACKOFF_THRESHOLD) {
    // Random backoff: scale with defer count for exponential-ish backoff
    uint32_t backoff_ms = (random_uint32() % TxContext::BACKOFF_MAX_MS) + 1;
    backoff_ms *= (this->tx_ctx_.defer_count - TxContext::DEFER_BACKOFF_THRESHOLD);
    if (backoff_ms > TxContext::BACKOFF_MAX_MS * 3) {
      backoff_ms = TxContext::BACKOFF_MAX_MS * 3;  // Cap at 150ms
    }
    this->tx_ctx_.backoff_until = now + backoff_ms;
    ESP_LOGD(TAG, "TX defer %d, backoff %ums", this->tx_ctx_.defer_count, backoff_ms);
  }

  // Go back to GOTO_IDLE to retry (best-effort, ignore failure)
  (void) this->write_cmd(CC1101_SIDLE);
  this->tx_ctx_.state = TxState::GOTO_IDLE;
  this->tx_ctx_.state_enter_time = now;
}

void Elero::abort_tx_() {
  if (this->tx_ctx_.state == TxState::IDLE) {
    return;  // Already idle, nothing to abort
  }
  uint8_t marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
  ESP_LOGW(TAG, "TX aborted in state %d, marcstate=0x%02x", static_cast<int>(this->tx_ctx_.state), marcstate);

  // Detect zombie state: 0x00 (SLEEP) or 0x1F (SPI failure/disconnected) are suspicious
  if (marcstate == 0x00 || marcstate == 0x1F) {
    // Retry read to filter transient SPI noise
    bool confirmed_zombie = true;
    for (int i = 0; i < 2; ++i) {
      delay_microseconds_safe(100);
      uint8_t marc2 = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
      if (marc2 != marcstate) {
        confirmed_zombie = false;
        break;
      }
    }
    if (confirmed_zombie) {
      // Rate-limit resets to prevent infinite loop under failing power supply
      uint32_t now = millis();
      if (now - this->last_chip_reset_ms_ > 10000) {  // 10s minimum gap
        this->last_chip_reset_ms_ = now;
        ESP_LOGE(TAG, "CC1101 zombie state confirmed (0x%02x), reinitializing chip", marcstate);
        this->reset();
        this->init();
      } else {
        ESP_LOGW(TAG, "CC1101 zombie state (0x%02x), skipping reset (rate-limited)", marcstate);
      }
    }
  }

  this->tx_ctx_.state = TxState::IDLE;
  this->tx_ctx_.reset();  // Reset defer count
  this->tx_pending_success_ = false;
  this->flush_and_rx();

  // Notify owner of failure (must happen after state cleanup)
  this->notify_tx_owner_(false);
}

void Elero::notify_tx_owner_(bool success) {
  if (this->tx_owner_ != nullptr) {
    TxClient *owner = this->tx_owner_;
    this->tx_owner_ = nullptr;  // Clear BEFORE callback (re-entrancy safe)
    owner->on_tx_complete(success);
  }
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
    this->flush_and_rx();
    return;
  }

  // Stuck in IDLE — radio stopped listening, one strobe restarts it
  if (marc == CC1101_MARCSTATE_IDLE) {
    ESP_LOGW(TAG, "Radio watchdog: stuck in IDLE, restarting RX");
    (void) this->write_cmd(CC1101_SRX);
    return;
  }

  // Anything else unexpected — full reset to known-good RX state
  ESP_LOGW(TAG, "Radio watchdog: unexpected MARCSTATE 0x%02x, reinitializing", marc);
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
  // Convert EleroCommand to TxParams
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

  // Use pure function to build packet
  packet::build_tx_packet(params, this->msg_tx_);
}

bool Elero::request_tx(TxClient *client, const EleroCommand &cmd) {
  // Reject if already transmitting or another client owns the TX
  if (this->tx_owner_ != nullptr || this->tx_ctx_.state != TxState::IDLE) {
    ESP_LOGVV(TAG, "request_tx rejected: busy");
    return false;
  }

  // Build packet and record for echo detection
  this->build_tx_packet_(cmd);
  this->record_tx_(cmd.counter);

  // Look up device name by dst address (store string to avoid dangling pointer)
  std::string blind_name;
  if (this->registry_) {
    Device *dev = this->registry_->find(cmd.dst_addr);
    if (dev) {
      blind_name = dev->config.name;
    }
  }

  // JSON log for TX packet (machine-readable, tagged for WS forwarding)
  if (!blind_name.empty()) {
    ESP_LOGD(TAG_RF,
             "{\"ts_ms\":%lu,\"dir\":\"tx\",\"blind\":\"%s\",\"cmd_name\":\"%s\",\"len\":%d,\"cnt\":%d,"
             "\"type\":\"0x%02x\",\"type2\":\"0x%02x\",\"hop\":\"0x%02x\","
             "\"channel\":%d,\"src\":\"0x%06x\",\"dst\":\"0x%06x\",\"command\":\"0x%02x\"}",
             (unsigned long) millis(), blind_name.c_str(), elero_command_to_string(cmd.payload[4]),
             this->msg_tx_[0], cmd.counter, cmd.type, cmd.type2, cmd.hop,
             cmd.channel, cmd.src_addr, cmd.dst_addr, cmd.payload[4]);
  } else {
    ESP_LOGD(TAG_RF,
             "{\"ts_ms\":%lu,\"dir\":\"tx\",\"blind\":\"0x%06x\",\"cmd_name\":\"%s\",\"len\":%d,\"cnt\":%d,"
             "\"type\":\"0x%02x\",\"type2\":\"0x%02x\",\"hop\":\"0x%02x\","
             "\"channel\":%d,\"src\":\"0x%06x\",\"dst\":\"0x%06x\",\"command\":\"0x%02x\"}",
             (unsigned long) millis(), cmd.dst_addr, elero_command_to_string(cmd.payload[4]),
             this->msg_tx_[0], cmd.counter, cmd.type, cmd.type2, cmd.hop,
             cmd.channel, cmd.src_addr, cmd.dst_addr, cmd.payload[4]);
  }

  // Start non-blocking TX
  if (!this->start_transmit()) {
    ESP_LOGW(TAG, "request_tx: start_transmit failed");
    return false;
  }

  // Take ownership
  this->tx_owner_ = client;
  return true;
}

bool Elero::start_transmit() {
  if (this->tx_ctx_.state != TxState::IDLE) {
    return false;  // Already transmitting
  }

  ESP_LOGVV(TAG, "start_transmit called for %d data bytes", this->msg_tx_[0]);

  // Begin state machine: go to IDLE first
  this->tx_ctx_.state = TxState::GOTO_IDLE;
  this->tx_ctx_.state_enter_time = millis();
  this->tx_pending_success_ = false;

  // Send SIDLE command
  if (!this->write_cmd(CC1101_SIDLE)) {
    this->abort_tx_();
    return false;
  }

  return true;
}

bool Elero::poll_tx_result() {
  if (this->tx_ctx_.state != TxState::IDLE) {
    return false;  // Still in progress
  }
  return this->tx_pending_success_;
}

void Elero::handle_tx_state_(uint32_t now) {
  if (this->tx_ctx_.state == TxState::IDLE) {
    return;
  }

  uint32_t elapsed = now - this->tx_ctx_.state_enter_time;
  uint8_t marcstate;

  switch (this->tx_ctx_.state) {
    case TxState::GOTO_IDLE:
      // Check backoff timer first
      if (now < this->tx_ctx_.backoff_until) {
        return;  // Still in backoff, wait
      }

      // Check defer limit
      if (this->tx_ctx_.defer_count >= TxContext::DEFER_MAX) {
        ESP_LOGW(TAG, "TX deferred %d times, channel busy, aborting", this->tx_ctx_.defer_count);
        this->abort_tx_();
        return;
      }

      marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
      if (marcstate == CC1101_MARCSTATE_IDLE) {
        // Transition to FLUSH_TX
        if (!this->write_cmd(CC1101_SFTX)) {
          this->abort_tx_();
          return;
        }
        this->tx_ctx_.state = TxState::FLUSH_TX;
        this->tx_ctx_.state_enter_time = now;
      } else if (elapsed > TxContext::STATE_TIMEOUT_MS) {
        ESP_LOGE(TAG, "TX timeout in GOTO_IDLE after %ums, marcstate=0x%02x", elapsed, marcstate);
        this->abort_tx_();
      }
      break;

    case TxState::FLUSH_TX:
      // Check timeout FIRST (elapsed >= 1 would always trigger before timeout otherwise)
      if (elapsed > TxContext::STATE_TIMEOUT_MS) {
        ESP_LOGE(TAG, "TX timeout in FLUSH_TX after %ums", elapsed);
        this->abort_tx_();
      } else if (elapsed >= 1) {
        // Brief settling time after SFTX (millis() has 1ms resolution)
        // Load TX FIFO
        if (!this->write_burst(CC1101_TXFIFO, this->msg_tx_, this->msg_tx_[0] + 1)) {
          this->abort_tx_();
          return;
        }
        this->tx_ctx_.state = TxState::LOAD_FIFO;
        this->tx_ctx_.state_enter_time = now;
      }
      break;

    case TxState::LOAD_FIFO:
      // Force IDLE to prevent starting new packet reception between our check and STX.
      // This is aggressive but necessary when the channel is flooded with status packets.
      (void) this->write_cmd(CC1101_SIDLE);

      // Clear received_ so we can detect TX-end interrupt
      this->received_.store(false);

      // Trigger TX immediately - no CCA check from IDLE state
      if (!this->write_cmd(CC1101_STX)) {
        this->abort_tx_();
        return;
      }

      // Poll MARCSTATE waiting for TX (deterministic: max 20 iterations × 5μs = 100μs)
      for (int i = 0; i < 20; i++) {
        delay_microseconds_safe(5);
        marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
        if (marcstate == CC1101_MARCSTATE_TX) {
          this->tx_ctx_.state = TxState::WAIT_TX_DONE;
          this->tx_ctx_.state_enter_time = now;
          return;
        }
        // RX states mean we lost the race - stop polling
        if (marcstate == CC1101_MARCSTATE_RX) {
          break;
        }
      }

      // Didn't reach TX - check final state
      marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
      if (marcstate == CC1101_MARCSTATE_TX) {
        this->tx_ctx_.state = TxState::WAIT_TX_DONE;
        this->tx_ctx_.state_enter_time = now;
      } else if (marcstate == CC1101_MARCSTATE_RX) {
        ESP_LOGD(TAG, "Radio went to RX instead of TX, deferring");
        this->defer_tx_(now);
      } else {
        // Transitional state (FSTXON, CALIBRATE, etc.) - continue via state machine
        this->tx_ctx_.state = TxState::TRIGGER_TX;
        this->tx_ctx_.state_enter_time = now;
      }
      break;

    case TxState::TRIGGER_TX:
      marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
      if (marcstate == CC1101_MARCSTATE_TX) {
        this->tx_ctx_.state = TxState::WAIT_TX_DONE;
        this->tx_ctx_.state_enter_time = now;
      } else if (marcstate == CC1101_MARCSTATE_TXFIFO_UFLOW) {
        ESP_LOGE(TAG, "TX FIFO underflow detected, aborting");
        this->abort_tx_();
      } else if (marcstate == CC1101_MARCSTATE_RXFIFO_OFLOW) {
        ESP_LOGE(TAG, "RX FIFO overflow during TX, aborting");
        this->abort_tx_();
      } else if (marcstate == CC1101_MARCSTATE_RX) {
        ESP_LOGD(TAG, "Radio in RX during TRIGGER_TX, deferring");
        this->defer_tx_(now);
      } else if (elapsed > TxContext::STATE_TIMEOUT_MS) {
        ESP_LOGE(TAG, "TX timeout in TRIGGER_TX after %ums, marcstate=0x%02x", elapsed, marcstate);
        this->abort_tx_();
      }
      // else: transitional state (FSTXON, CALIBRATE, IDLE), keep polling
      break;

    case TxState::WAIT_TX_DONE:
      if (this->received_.load()) {
        // GDO0 interrupt fired - TX complete
        this->tx_ctx_.state = TxState::VERIFY_DONE;
        this->tx_ctx_.state_enter_time = now;
      } else if (elapsed > TxContext::STATE_TIMEOUT_MS) {
        ESP_LOGE(TAG, "TX timeout in WAIT_TX_DONE after %ums", elapsed);
        this->abort_tx_();
      }
      break;

    case TxState::VERIFY_DONE: {
      // Poll TXBYTES - GDO0 may fire slightly before FIFO fully drains
      bool fifo_empty = false;
      for (int retry = 0; retry < 3; ++retry) {
        uint8_t bytes = this->read_status_reliable_(CC1101_TXBYTES) & packet::cc1101_status::BYTE_COUNT_MASK;
        if (bytes == 0) {
          fifo_empty = true;
          break;
        }
        delay_microseconds_safe(100);
      }

      if (fifo_empty) {
        ESP_LOGV(TAG, "TX successful (async)");
        this->tx_ctx_.state = TxState::RETURN_RX;
        this->tx_ctx_.state_enter_time = now;
      } else {
        // Capture state for diagnostics
        uint8_t marc = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
        uint8_t bytes_final = this->read_status_reliable_(CC1101_TXBYTES) & packet::cc1101_status::BYTE_COUNT_MASK;
        ESP_LOGE(TAG, "TX error: FIFO stuck, txbytes=%u, MARCSTATE=0x%02x", bytes_final, marc);
        this->abort_tx_();
      }
      break;
    }

    case TxState::RETURN_RX:
      // Return to RX state
      this->flush_and_rx();
      this->tx_pending_success_ = true;
      this->tx_ctx_.state = TxState::IDLE;
      this->tx_ctx_.reset();  // Reset defer count on success
      break;

    default:
      this->abort_tx_();
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
  const uint32_t dispatch_start = millis();

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

  uint32_t dispatch_duration = millis() - dispatch_start;
  if (dispatch_duration > 1) {
    ESP_LOGD(TAG, "dispatch_packet took %lums (cnt=%d)", (unsigned long) dispatch_duration, pkt.cnt);
  }
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

bool Elero::send_command(EleroCommand *cmd) {
  ESP_LOGVV(TAG, "send_command called");
  this->build_tx_packet_(*cmd);
  this->record_tx_(cmd->counter);

  // Look up device name by dst address
  std::string blind_name;
  if (this->registry_) {
    Device *dev = this->registry_->find(cmd->dst_addr);
    if (dev) {
      blind_name = dev->config.name;
    }
  }

  // JSON log for TX packet (machine-readable, tagged for WS forwarding)
  if (!blind_name.empty()) {
    ESP_LOGD(TAG_RF,
             "{\"ts_ms\":%lu,\"dir\":\"tx\",\"blind\":\"%s\",\"cmd_name\":\"%s\",\"len\":%d,\"cnt\":%d,"
             "\"type\":\"0x%02x\",\"type2\":\"0x%02x\",\"hop\":\"0x%02x\","
             "\"channel\":%d,\"src\":\"0x%06x\",\"dst\":\"0x%06x\",\"command\":\"0x%02x\"}",
             (unsigned long) millis(), blind_name.c_str(), elero_command_to_string(cmd->payload[4]),
             this->msg_tx_[0], cmd->counter, cmd->type, cmd->type2, cmd->hop,
             cmd->channel, cmd->src_addr, cmd->dst_addr, cmd->payload[4]);
  } else {
    ESP_LOGD(TAG_RF,
             "{\"ts_ms\":%lu,\"dir\":\"tx\",\"blind\":\"0x%06x\",\"cmd_name\":\"%s\",\"len\":%d,\"cnt\":%d,"
             "\"type\":\"0x%02x\",\"type2\":\"0x%02x\",\"hop\":\"0x%02x\","
             "\"channel\":%d,\"src\":\"0x%06x\",\"dst\":\"0x%06x\",\"command\":\"0x%02x\"}",
             (unsigned long) millis(), cmd->dst_addr, elero_command_to_string(cmd->payload[4]),
             this->msg_tx_[0], cmd->counter, cmd->type, cmd->type2, cmd->hop,
             cmd->channel, cmd->src_addr, cmd->dst_addr, cmd->payload[4]);
  }
  return transmit();
}

bool Elero::send_raw_command(uint32_t dst_addr, uint32_t src_addr, uint8_t channel, uint8_t command,
                              uint8_t payload_1, uint8_t payload_2, uint8_t type, uint8_t type2, uint8_t hop) {
  // Use a static counter for raw commands (persists across calls)
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

  // WARNING: Blocking TX — only for debugging unknown addresses.
  // For known entities, callers should use perform_command() instead,
  // which routes through the entity's non-blocking CommandSender.
  bool success = this->send_command(&cmd);
  if (success) {
    ++raw_msg_cnt;
    if (raw_msg_cnt > packet::limits::COUNTER_MAX)
      raw_msg_cnt = 1;
  }
  return success;
}

}  // namespace elero
}  // namespace esphome
