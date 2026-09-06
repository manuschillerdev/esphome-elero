#include "cc1101_driver.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cstring>

#ifdef USE_ESP32
#include <esp_timer.h>
#endif

namespace esphome {
namespace elero {

static const char *const TAG = "elero.cc1101";

CC1101Driver::CC1101Driver() : tx_fsm_(*this) {}

// ─── SpiTransaction RAII Implementation ───────────────────────────────────
SpiTransaction::SpiTransaction(CC1101Driver *driver) : driver_(driver) {
  driver_->enable();
}

SpiTransaction::~SpiTransaction() {
  driver_->disable();
}

// ─── RadioDriver Interface ────────────────────────────────────────────────

bool CC1101Driver::init() {
  this->spi_setup();
  this->reset();

  // Wait for crystal oscillator to stabilize after reset (CC1101 datasheet: ~1ms typical)
  delay(5);

  // Verify CC1101 is responding — known versions: 0x04, 0x14, 0x82 (per RadioLib)
  uint8_t partnum = this->read_status(CC1101_PARTNUM);
  uint8_t version = this->read_status(CC1101_VERSION);
  if (version == packet::cc1101_status::VERSION_NOT_CONNECTED_LOW ||
      version == packet::cc1101_status::VERSION_NOT_CONNECTED_HIGH) {
    // Run SPI diagnostics to help users identify wiring issues
    this->diagnose_spi_failure_(partnum, version);
    return false;
  }
  ESP_LOGI(TAG, "CC1101 detected: PARTNUM=0x%02x VERSION=0x%02x", partnum, version);

  // Verify SPI write path — catches MOSI wiring issues where reads work but writes don't
  if (!this->verify_spi_write_()) {
    ESP_LOGE(TAG, "SPI write verification failed — check MOSI wiring");
    return false;
  }

  return this->init_registers();
}

void CC1101Driver::reset() {
  this->rx_packet_length_ = 0;
  // Software reset — we can't read the MISO pin directly.
  this->enable();
  this->write_byte(CC1101_SRES);
  delay_microseconds_safe(50);
  this->write_byte(CC1101_SIDLE);
  delay_microseconds_safe(50);
  this->disable();
}

bool CC1101Driver::load_and_transmit(const uint8_t *pkt_buf, size_t len) {
  if (!this->tx_fsm_.is_idle()) {
    ESP_LOGW(TAG, "TX start rejected: FSM busy");
    return false;
  }

  if (pkt_buf == nullptr || len == 0 || len > CC1101_FIFO_LENGTH) {
    ESP_LOGW(TAG, "TX start rejected: invalid buffer ptr=%p len=%u", pkt_buf,
             static_cast<unsigned>(len));
    return false;
  }

  memcpy(this->tx_buf_, pkt_buf, len);
  this->tx_len_ = len;

  ESP_LOGVV(TAG, "load_and_transmit: %d data bytes", this->tx_buf_[0]);
  uint32_t now = millis();
  if (!this->tx_fsm_.Start(now)) {
    ESP_LOGW(TAG, "TX start rejected: FSM refused start");
    return false;
  }

  // Preserve the RadioDriver contract: load_and_transmit() performs the
  // synchronous prepare phase and returns false on immediate hardware failure.
  this->tx_fsm_.Poll(now);
  if (this->tx_fsm_.is_idle()) {
    return this->tx_terminal_result_ == Cc1101TxTerminalResult::Success;
  }

  return true;
}

TxPollResult CC1101Driver::poll_tx() {
  if (this->tx_fsm_.is_idle()) {
    return this->tx_terminal_result_ == Cc1101TxTerminalResult::Success
               ? TxPollResult::SUCCESS
               : TxPollResult::FAILED;
  }

  this->tx_fsm_.Poll(millis());
  if (!this->tx_fsm_.is_idle()) {
    return TxPollResult::PENDING;
  }
  return this->tx_terminal_result_ == Cc1101TxTerminalResult::Success
             ? TxPollResult::SUCCESS
             : TxPollResult::FAILED;
}

void CC1101Driver::abort_tx() {
  this->tx_fsm_.Abort(millis());
}

bool CC1101Driver::has_data() {
  if (this->RadioDriver::mode() != RadioMode::RX) return false;
  // Poll as well as observing GDO0: a packet can finish during the TX/RX
  // handoff, or a second packet can already be queued behind the first.
  if (this->rx_packet_length_ != 0) return true;
  if (this->rx_ready_ && this->rx_ready_->load(std::memory_order_acquire)) return true;
  const uint8_t count = this->read_status_reliable_(CC1101_RXBYTES);
  return (count & packet::cc1101_status::RXBYTES_OVERFLOW_BIT) || count >= 2;
}

size_t CC1101Driver::read_fifo(uint8_t *buf, size_t max_len) {
  if (buf == nullptr || max_len == 0) return 0;
  const uint8_t status = this->read_status_reliable_(CC1101_RXBYTES);
  if (status & packet::cc1101_status::RXBYTES_OVERFLOW_BIT) {
    ESP_LOGW(TAG, "RX overflow, discarding incomplete frame");
    this->stat_fifo_overflows_.fetch_add(1, std::memory_order_relaxed);
    if (!this->flush_and_rx()) this->recover();
    return 0;
  }
  size_t available = status & packet::cc1101_status::BYTE_COUNT_MASK;
  if (this->rx_packet_length_ == 0) {
    // Never empty the FIFO while an unfinished packet is arriving. In
    // particular, leave a lone length byte until another byte has arrived.
    if (available < 2) return 0;
    this->read_buf(CC1101_RXFIFO, &this->rx_packet_length_, 1);
    --available;
    this->rx_packet_started_ms_ = millis();
    if (this->rx_packet_length_ == 0 ||
        this->rx_packet_length_ + packet::PACKET_TOTAL_OVERHEAD > CC1101_FIFO_LENGTH) {
      ESP_LOGW(TAG, "Invalid RX frame length %u", this->rx_packet_length_);
      if (!this->flush_and_rx()) this->recover();
      return 0;
    }
  }
  const size_t remaining = this->rx_packet_length_ + packet::CC1101_APPEND_SIZE;
  if (available < remaining) {
    if (millis() - this->rx_packet_started_ms_ > RX_PACKET_TIMEOUT_MS) {
      ESP_LOGW(TAG, "Truncated RX frame timed out (length=%u)", this->rx_packet_length_);
      if (!this->flush_and_rx()) this->recover();
    }
    return 0;
  }
  if (remaining + 1 > max_len) {
    ESP_LOGW(TAG, "RX output buffer too small (%u)", static_cast<unsigned>(max_len));
    if (!this->flush_and_rx()) this->recover();
    return 0;
  }
  buf[0] = this->rx_packet_length_;
  this->read_buf(CC1101_RXFIFO, buf + 1, remaining);
  this->rx_packet_length_ = 0;
  if (!(buf[remaining] & packet::cc1101_status::CRC_OK_BIT)) {
    ESP_LOGD(TAG, "RX frame rejected: CRC mismatch");
    return 0;
  }
  return remaining + 1;
}

RadioHealth CC1101Driver::check_health() {
  uint32_t now = millis();
  if (now - this->last_radio_check_ms_ < packet::timing::RADIO_WATCHDOG_INTERVAL) {
    return RadioHealth::OK;
  }
  this->last_radio_check_ms_ = now;

  uint8_t marc = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;

  // Log RSSI for TX hardware test (read CC1101 RSSI register in RX mode)
  uint8_t rssi_raw = this->read_status(CC1101_RSSI);
  int16_t rssi_dbm = (rssi_raw >= 128) ? ((rssi_raw - 256) / 2 - 74) : (rssi_raw / 2 - 74);
  ESP_LOGW(TAG, "health: MARCSTATE=0x%02x RSSI=%d dBm (raw=0x%02x)", marc, rssi_dbm, rssi_raw);

  // RX is the expected idle state
  if (marc == CC1101_MARCSTATE_RX) {
    return RadioHealth::OK;
  }

  // Transient calibration/wind-down states — will reach RX on their own
  if (marcstate_is_transient(marc)) {
    return RadioHealth::OK;
  }

  // RXFIFO overflow — radio is deaf until flushed
  if (marc == CC1101_MARCSTATE_RXFIFO_OFLOW) {
    ESP_LOGW(TAG, "Radio watchdog: RX FIFO overflow, flushing");
    return RadioHealth::FIFO_OVERFLOW;
  }

  // Stuck in IDLE — radio stopped listening
  if (marc == CC1101_MARCSTATE_IDLE) {
    ESP_LOGW(TAG, "Radio watchdog: stuck in IDLE, restarting RX");
    return RadioHealth::STUCK;
  }

  // Anything else unexpected
  ESP_LOGW(TAG, "Radio watchdog: unexpected MARCSTATE 0x%02x, reinitializing", marc);
  return RadioHealth::UNRECOVERABLE;
}

void CC1101Driver::clear_recovery_failures_() {
  this->failed_recoveries_ = 0;
  this->failed_resets_ = 0;
}

void CC1101Driver::recover() {
  this->stat_watchdog_recoveries_.fetch_add(1, std::memory_order_relaxed);
  if (this->flush_and_rx()) {
    this->clear_recovery_failures_();
    return;
  }

  if (this->failed_recoveries_ < RECOVERIES_BEFORE_RESET) ++this->failed_recoveries_;
  if (this->failed_recoveries_ < RECOVERIES_BEFORE_RESET) return;

  this->reset();
  if (this->init_registers()) {
    this->clear_recovery_failures_();
    return;
  }

  if (++this->failed_resets_ >= RESETS_BEFORE_FAILED) {
    ESP_LOGE(TAG, "RX recovery failed after %u consecutive resets", this->failed_resets_);
    this->failed_ = true;
  }
}

void CC1101Driver::set_frequency_regs(uint8_t f2, uint8_t f1, uint8_t f0) {
  this->freq2_ = f2;
  this->freq1_ = f1;
  this->freq0_ = f0;
  this->reset();
  if (!this->init_registers()) {
    this->recover();
    return;
  }
  this->clear_recovery_failures_();
  ESP_LOGI(TAG, "CC1101 re-initialised: freq2=0x%02x freq1=0x%02x freq0=0x%02x", f2, f1, f0);
}

void CC1101Driver::dump_config() {
  ESP_LOGCONFIG(TAG, "  Radio: CC1101");
  ESP_LOGCONFIG(TAG, "  freq2: 0x%02x, freq1: 0x%02x, freq0: 0x%02x",
                this->freq2_, this->freq1_, this->freq0_);
}

// ─── Boot Diagnostics ────────────────────────────────────────────────────

void CC1101Driver::diagnose_spi_failure_(uint8_t partnum, uint8_t version) {
  if (partnum == 0x00 && version == 0x00) {
    ESP_LOGE(TAG, "SPI reads all zeros — MISO stuck low or chip not powered");
    ESP_LOGE(TAG, "  Check: VCC connected? MISO wired correctly? CS reaching chip?");
  } else if (partnum == 0xFF && version == 0xFF) {
    ESP_LOGE(TAG, "SPI reads all ones — MISO stuck high or chip not connected");
    ESP_LOGE(TAG, "  Check: CC1101 module powered? SPI wiring intact?");
  } else {
    ESP_LOGE(TAG, "CC1101 not responding (PARTNUM=0x%02x VERSION=0x%02x)", partnum, version);
    ESP_LOGE(TAG, "  Expected PARTNUM=0x00, VERSION=0x04 or 0x14");
  }
}

bool CC1101Driver::verify_spi_write_() {
  // Write the operational value to FSCTRL1 and read it back.
  // 0x08 uses only bits [4:0] — the writable portion of FSCTRL1.
  // This doubles as early initialization — init_registers() writes the same value.
  static constexpr uint8_t FSCTRL1_VALUE = 0x08;
  (void) this->write_reg(CC1101_FSCTRL1, FSCTRL1_VALUE);
  bool ok = false;
  uint8_t readback = this->read_reg(CC1101_FSCTRL1, &ok);
  if (!ok || readback != FSCTRL1_VALUE) {
    ESP_LOGE(TAG, "SPI write check failed: wrote 0x%02x to FSCTRL1, read back 0x%02x",
             FSCTRL1_VALUE, readback);
    return false;
  }
  return true;
}

// ─── Register Initialization ──────────────────────────────────────────────

bool CC1101Driver::init_registers() {
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
  // Keep bad-CRC frames until the driver consumes their status trailer. With
  // early length reads, automatic CRC flushing could erase a pending body and
  // splice the next packet onto its saved length byte.
  (void) this->write_reg(CC1101_PKTCTRL1, 0x84);
  (void) this->write_reg(CC1101_PKTCTRL0, 0x45);
  (void) this->write_reg(CC1101_ADDR, 0x00);
  (void) this->write_reg(CC1101_PKTLEN, 0x3C);
  (void) this->write_reg(CC1101_SYNC1, 0xD3);
  (void) this->write_reg(CC1101_SYNC0, 0x91);
  (void) this->write_burst(CC1101_PATABLE, patable_data, 8);

  (void) this->write_cmd(CC1101_SRX);
  return this->wait_rx();
}

// ─── TX State Machine ─────────────────────────────────────────────────────

bool CC1101Driver::tx_prepare_for_fsm() {
  (void) this->write_cmd(CC1101_SIDLE);

  bool reached_idle = false;
  for (uint8_t i = 0; i < TX_START_PROOF_POLLS; ++i) {
    esp_rom_delay_us(50);
    uint8_t marcstate =
        this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
    if (marcstate == CC1101_MARCSTATE_IDLE) {
      reached_idle = true;
      break;
    }
  }
  if (!reached_idle) {
    uint8_t marcstate =
        this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
    ESP_LOGW(TAG, "Prepare: SIDLE failed, MARCSTATE=0x%02x", marcstate);
    return false;
  }

  // Complete RX frames are serviced by the hub before requesting TX.
  // SIDLE may interrupt a frame still on air: discard its prefix now, before
  // sending, so it cannot corrupt the response received after this TX.
  this->rx_packet_length_ = 0;
  (void) this->write_cmd(CC1101_SFRX);
  if (this->rx_ready_) this->rx_ready_->store(false, std::memory_order_release);
  (void) this->write_cmd(CC1101_SFTX);
  esp_rom_delay_us(100);

  if (!this->write_burst(CC1101_TXFIFO, this->tx_buf_, static_cast<uint8_t>(this->tx_len_))) {
    ESP_LOGW(TAG, "Prepare: FIFO write failed");
    return false;
  }

  if (this->tx_done_ != nullptr) {
    this->tx_done_->store(false, std::memory_order_release);
  }

  (void) this->write_cmd(CC1101_STX);
  return true;
}

Cc1101TxPhaseResult CC1101Driver::tx_wait_started_for_fsm() {
  for (uint8_t i = 0; i < TX_START_PROOF_POLLS; ++i) {
    esp_rom_delay_us(50);
    uint8_t marcstate =
        this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
    if (marcstate == CC1101_MARCSTATE_TX) {
      return Cc1101TxPhaseResult::Succeeded;
    }
  }

  uint8_t marcstate =
      this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
  ESP_LOGW(TAG, "WaitTxStarted: STX failed, MARCSTATE=0x%02x", marcstate);
  return Cc1101TxPhaseResult::Failed;
}

Cc1101TxPhaseResult CC1101Driver::tx_wait_done_for_fsm() {
  switch (this->tx_check_done_result_()) {
    case TxDoneCheckResult::Pending:
      return Cc1101TxPhaseResult::Pending;
    case TxDoneCheckResult::Succeeded:
      return Cc1101TxPhaseResult::Succeeded;
    case TxDoneCheckResult::FailedTxBytesNotEmpty:
    case TxDoneCheckResult::FailedTimeout:
      return Cc1101TxPhaseResult::Failed;
  }
  return Cc1101TxPhaseResult::Failed;
}

CC1101Driver::TxDoneCheckResult CC1101Driver::tx_check_done_result_() {
  bool irq_fired = this->tx_done_ != nullptr && this->tx_done_->load(std::memory_order_acquire);
  if (irq_fired) {
    esp_rom_delay_us(50);
    uint8_t txbytes =
        this->read_status_reliable_(CC1101_TXBYTES) & packet::cc1101_status::BYTE_COUNT_MASK;
    if (txbytes == 0) {
      ESP_LOGV(TAG, "TX successful");
      return TxDoneCheckResult::Succeeded;
    }

    if (this->tx_verify_retry_count_ == 0) {
      ++this->tx_verify_retry_count_;
      esp_rom_delay_us(100);
      txbytes = this->read_status_reliable_(CC1101_TXBYTES) &
                packet::cc1101_status::BYTE_COUNT_MASK;
      if (txbytes == 0) {
        ESP_LOGV(TAG, "TX successful (after grace)");
        return TxDoneCheckResult::Succeeded;
      }
    }

    ESP_LOGE(TAG, "FIFO not empty after TX interrupt, txbytes=%u", txbytes);
    return TxDoneCheckResult::FailedTxBytesNotEmpty;
  }

  uint8_t marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
  if (marcstate == CC1101_MARCSTATE_IDLE || marcstate == CC1101_MARCSTATE_RX) {
    uint8_t txbytes =
        this->read_status_reliable_(CC1101_TXBYTES) & packet::cc1101_status::BYTE_COUNT_MASK;
    if (txbytes == 0) {
      ESP_LOGV(TAG, "TX successful (MARCSTATE fallback)");
      return TxDoneCheckResult::Succeeded;
    }
  }

  uint32_t elapsed = millis() - this->tx_state_enter_time_;
  if (elapsed > TX_DONE_TIMEOUT_MS) {
    ESP_LOGE(TAG, "TX timeout in WaitTxDone after %ums", elapsed);
    return TxDoneCheckResult::FailedTimeout;
  }
  return TxDoneCheckResult::Pending;
}

Cc1101TxPhaseResult CC1101Driver::tx_return_to_rx_for_fsm() {
  uint32_t elapsed = millis() - this->tx_state_enter_time_;
  uint8_t marcstate =
      this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
  if (marcstate == CC1101_MARCSTATE_IDLE) {
    (void) this->write_cmd(CC1101_SRX);
    esp_rom_delay_us(100);
    marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
  }

  if (marcstate != CC1101_MARCSTATE_RX) {
    if (marcstate_is_transient(marcstate) && elapsed <= RX_READY_TIMEOUT_MS) {
      ESP_LOGD(TAG, "ReturnToRx: waiting for RX MARCSTATE=0x%02x after %ums", marcstate,
               elapsed);
      return Cc1101TxPhaseResult::Pending;
    }
    ESP_LOGW(TAG, "ReturnToRx: radio not RX-ready, MARCSTATE=0x%02x after %ums", marcstate,
             elapsed);
    return Cc1101TxPhaseResult::Failed;
  }

  uint8_t rxbytes = this->read_status_reliable_(CC1101_RXBYTES);
  if (rxbytes & packet::cc1101_status::RXBYTES_OVERFLOW_BIT) {
    ESP_LOGW(TAG, "ReturnToRx: RX FIFO overflow after TX, flushing");
    if (!this->flush_and_rx()) this->recover();
  } else if ((rxbytes & packet::cc1101_status::BYTE_COUNT_MASK) > 0) {
    ESP_LOGV(TAG, "ReturnToRx: RX FIFO has %d byte(s) after TX, preserving for RX drain",
             rxbytes & packet::cc1101_status::BYTE_COUNT_MASK);
    if (this->rx_ready_ != nullptr) {
      this->rx_ready_->store(true, std::memory_order_release);
    }
  }

  marcstate = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
  if (marcstate != CC1101_MARCSTATE_RX) {
    if (marcstate_is_transient(marcstate) && elapsed <= RX_READY_TIMEOUT_MS) {
      ESP_LOGD(TAG, "ReturnToRx: waiting for RX restore MARCSTATE=0x%02x after %ums",
               marcstate, elapsed);
      return Cc1101TxPhaseResult::Pending;
    }
    ESP_LOGW(TAG, "ReturnToRx: RX restore failed, MARCSTATE=0x%02x after %ums", marcstate,
             elapsed);
    return Cc1101TxPhaseResult::Failed;
  }

  this->RadioDriver::mode_.store(RadioMode::RX, std::memory_order_release);
  return Cc1101TxPhaseResult::Succeeded;
}

void CC1101Driver::tx_on_state_enter_for_fsm(Cc1101TxState state, uint32_t now) {
  this->tx_state_enter_time_ = now;
  if (state != Cc1101TxState::WaitTxDone) {
    this->tx_verify_retry_count_ = 0;
  }
}

void CC1101Driver::tx_set_terminal_result_for_fsm(Cc1101TxTerminalResult result) {
  this->tx_terminal_result_ = result;
}

void CC1101Driver::tx_set_mode_for_fsm(RadioMode mode) {
  this->RadioDriver::mode_.store(mode, std::memory_order_release);
}

void CC1101Driver::tx_recover_for_fsm() {
  this->stat_tx_recover_.fetch_add(1, std::memory_order_relaxed);

  this->recover();
  this->tx_set_terminal_result_for_fsm(Cc1101TxTerminalResult::Failed);
}

// ─── Radio Control ────────────────────────────────────────────────────────

bool CC1101Driver::flush_and_rx() {
  this->rx_packet_length_ = 0;
  ESP_LOGVV(TAG, "flush_and_rx");

  // 1. Force IDLE
  (void) this->write_cmd(CC1101_SIDLE);
  esp_rom_delay_us(100);

  // 2. Clear stale IRQ flags while the radio is definitely idle.
  if (this->rx_ready_) {
    this->rx_ready_->store(false, std::memory_order_release);
  }
  if (this->tx_done_) {
    this->tx_done_->store(false, std::memory_order_release);
  }

  // 3. Flush both FIFOs
  (void) this->write_cmd(CC1101_SFRX);
  (void) this->write_cmd(CC1101_SFTX);

  // 4. Switch software routing back to RX before re-enabling hardware RX.
  // This avoids misrouting a fast post-recovery packet to tx_done_.
  this->RadioDriver::mode_.store(RadioMode::RX, std::memory_order_release);
  (void) this->write_cmd(CC1101_SRX);

  // Transient calibration states are not proof of readiness. Wait boundedly.
  return this->wait_rx();
}

// ─── SPI Communication ───────────────────────────────────────────────────

bool CC1101Driver::write_reg(uint8_t addr, uint8_t data) {
  uint8_t status;
  {
    SpiTransaction txn(this);
    status = this->transfer_byte(addr);
    this->write_byte(data);
  }  // CS released here
  delay_microseconds_safe(15);

  if ((status & packet::cc1101_status::SPI_STATE_MASK) == packet::cc1101_status::SPI_STATE_RXFIFO_OVERFLOW) {
    ESP_LOGW(TAG, "SPI write_reg 0x%02x: RXFIFO overflow (status=0x%02x)", addr, status);
    return false;
  }
  return true;
}

bool CC1101Driver::write_burst(uint8_t addr, uint8_t *data, uint8_t len) {
  uint8_t status;
  {
    SpiTransaction txn(this);
    status = this->transfer_byte(addr | CC1101_WRITE_BURST);
    for (int i = 0; i < len; ++i) {
      this->write_byte(data[i]);
    }
  }  // CS released here
  delay_microseconds_safe(15);

  if ((status & packet::cc1101_status::SPI_STATE_MASK) == packet::cc1101_status::SPI_STATE_RXFIFO_OVERFLOW) {
    ESP_LOGW(TAG, "SPI write_burst 0x%02x (%d bytes): RXFIFO overflow (status=0x%02x)", addr, len, status);
    return false;
  }
  return true;
}

bool CC1101Driver::write_cmd(uint8_t cmd) {
  uint8_t status;
  {
    SpiTransaction txn(this);
    status = this->transfer_byte(cmd);
  }  // CS released here
  delay_microseconds_safe(15);

  if ((status & packet::cc1101_status::SPI_STATE_MASK) == packet::cc1101_status::SPI_STATE_RXFIFO_OVERFLOW) {
    ESP_LOGW(TAG, "SPI write_cmd 0x%02x: RXFIFO overflow (status=0x%02x)", cmd, status);
    return false;
  }
  return true;
}

bool CC1101Driver::wait_rx() {
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

uint8_t CC1101Driver::read_reg(uint8_t addr, bool *ok) {
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

uint8_t CC1101Driver::read_status(uint8_t addr) {
  uint8_t data;
  {
    SpiTransaction txn(this);
    this->write_byte(addr | CC1101_READ_BURST);
    data = this->read_byte();
  }  // CS released here
  delay_microseconds_safe(15);
  return data;
}

uint8_t CC1101Driver::read_status_reliable_(uint8_t addr) {
  // CC1101 errata: RXBYTES and TXBYTES can return incorrect values on a single read.
  // Workaround (per RadioLib): read twice until both values match.
  uint8_t a, b;
  int tries = 0;
  do {
    a = this->read_status(addr);
    b = this->read_status(addr);
  } while (a != b && ++tries < 10);
  return a;
}

void CC1101Driver::read_buf(uint8_t addr, uint8_t *buf, uint8_t len) {
  {
    SpiTransaction txn(this);
    this->write_byte(addr | CC1101_READ_BURST);
    for (uint8_t i = 0; i < len; ++i) {
      buf[i] = this->read_byte();
    }
  }  // CS released here
  delay_microseconds_safe(15);
}

}  // namespace elero
}  // namespace esphome
