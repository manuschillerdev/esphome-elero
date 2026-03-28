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
  uint8_t version = this->read_status(CC1101_VERSION);
  if (version == packet::cc1101_status::VERSION_NOT_CONNECTED_LOW ||
      version == packet::cc1101_status::VERSION_NOT_CONNECTED_HIGH) {
    ESP_LOGE(TAG, "CC1101 not found (VERSION=0x%02x). Check SPI wiring and CS pin.", version);
    return false;
  }
  ESP_LOGI(TAG, "CC1101 version: 0x%02x", version);

  this->init_registers();
  return true;
}

void CC1101Driver::reset() {
  // Software reset — we can't read the MISO pin directly.
  this->enable();
  this->write_byte(CC1101_SRES);
  delay_microseconds_safe(50);
  this->write_byte(CC1101_SIDLE);
  delay_microseconds_safe(50);
  this->disable();
}

bool CC1101Driver::load_and_transmit(const uint8_t *pkt_buf, size_t len) {
  if (this->tx_ctx_.state != TxState::IDLE) {
    return false;  // Already transmitting
  }

  // Copy packet to internal buffer
  size_t copy_len = (len <= CC1101_FIFO_LENGTH) ? len : CC1101_FIFO_LENGTH;
  memcpy(this->tx_buf_, pkt_buf, copy_len);
  this->tx_len_ = copy_len;

  ESP_LOGVV(TAG, "load_and_transmit: %d data bytes", this->tx_buf_[0]);

  // Transition to PREPARE — synchronous pre-TX sequence
  this->tx_ctx_.state = TxState::PREPARE;
  this->tx_ctx_.state_enter_time = millis();
  this->tx_pending_success_ = false;

  return true;
}

TxPollResult CC1101Driver::poll_tx() {
  if (this->tx_ctx_.state == TxState::IDLE) {
    return this->tx_pending_success_ ? TxPollResult::SUCCESS : TxPollResult::FAILED;
  }

  this->handle_tx_state_(millis());

  if (this->tx_ctx_.state == TxState::IDLE) {
    return this->tx_pending_success_ ? TxPollResult::SUCCESS : TxPollResult::FAILED;
  }
  return TxPollResult::PENDING;
}

void CC1101Driver::abort_tx() {
  this->recover_radio_();
}

bool CC1101Driver::has_data() {
  return this->irq_flag_ != nullptr && this->irq_flag_->load(std::memory_order_acquire);
}

size_t CC1101Driver::read_fifo(uint8_t *buf, size_t max_len) {
  uint8_t len = this->read_status_reliable_(CC1101_RXBYTES);

  // Overflow — FIFO data unreliable, flush and bail
  if (len & packet::cc1101_status::RXBYTES_OVERFLOW_BIT) {
    ESP_LOGV(TAG, "Rx overflow, flushing FIFOs");
    this->stat_fifo_overflows_.fetch_add(1, std::memory_order_relaxed);
    this->flush_and_rx();
    return 0;
  }

  uint8_t fifo_count = len & packet::cc1101_status::BYTE_COUNT_MASK;
  if (fifo_count == 0) {
    return 0;
  }

  // Clamp to buffer size and FIFO length
  if (fifo_count > max_len) {
    ESP_LOGV(TAG, "RXBYTES (%d) > buffer (%d), clamping", fifo_count, static_cast<int>(max_len));
    fifo_count = static_cast<uint8_t>(max_len);
  }
  if (fifo_count > CC1101_FIFO_LENGTH) {
    ESP_LOGV(TAG, "RXBYTES > FIFO length (%d), clamping", fifo_count);
    fifo_count = CC1101_FIFO_LENGTH;
  }

  this->read_buf(CC1101_RXFIFO, buf, fifo_count);
  return fifo_count;
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
    this->stat_watchdog_recoveries_.fetch_add(1, std::memory_order_relaxed);
    return RadioHealth::FIFO_OVERFLOW;
  }

  // Stuck in IDLE — radio stopped listening
  if (marc == CC1101_MARCSTATE_IDLE) {
    ESP_LOGW(TAG, "Radio watchdog: stuck in IDLE, restarting RX");
    this->stat_watchdog_recoveries_.fetch_add(1, std::memory_order_relaxed);
    return RadioHealth::STUCK;
  }

  // Anything else unexpected
  ESP_LOGW(TAG, "Radio watchdog: unexpected MARCSTATE 0x%02x, reinitializing", marc);
  this->stat_watchdog_recoveries_.fetch_add(1, std::memory_order_relaxed);
  return RadioHealth::UNRECOVERABLE;
}

void CC1101Driver::recover() {
  uint8_t marc = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;

  // Stuck in IDLE — one strobe restarts it
  if (marc == CC1101_MARCSTATE_IDLE) {
    (void) this->write_cmd(CC1101_SRX);
    return;
  }

  // Everything else — full flush and RX
  this->flush_and_rx();
}

void CC1101Driver::set_frequency_regs(uint8_t f2, uint8_t f1, uint8_t f0) {
  this->freq2_ = f2;
  this->freq1_ = f1;
  this->freq0_ = f0;
  this->reset();
  this->init_registers();
  ESP_LOGI(TAG, "CC1101 re-initialised: freq2=0x%02x freq1=0x%02x freq0=0x%02x", f2, f1, f0);
}

void CC1101Driver::dump_config() {
  ESP_LOGCONFIG(TAG, "  Radio: CC1101");
  ESP_LOGCONFIG(TAG, "  freq2: 0x%02x, freq1: 0x%02x, freq0: 0x%02x",
                this->freq2_, this->freq1_, this->freq0_);
}

// ─── Register Initialization ──────────────────────────────────────────────

void CC1101Driver::init_registers() {
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

// ─── TX State Machine ─────────────────────────────────────────────────────

void CC1101Driver::handle_tx_state_(uint32_t now) {
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
      if (!this->write_burst(CC1101_TXFIFO, this->tx_buf_, static_cast<uint8_t>(this->tx_len_))) {
        ESP_LOGW(TAG, "PREPARE: FIFO write failed");
        this->tx_ctx_.state = TxState::RECOVER;
        this->tx_ctx_.state_enter_time = now;
        return;
      }

      // 5. Clear IRQ flag so we can detect TX-end interrupt
      if (this->irq_flag_) {
        this->irq_flag_->store(false);
      }

      // 6. Trigger TX
      (void) this->write_cmd(CC1101_STX);

      // 7. Poll MARCSTATE == TX, max ~1ms (covers ~700us calibration)
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

    case TxState::WAIT_TX: {
      // Interrupt-driven with MARCSTATE polling fallback
      bool irq_fired = this->irq_flag_ && this->irq_flag_->load();
      if (irq_fired) {
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
    }

    case TxState::RECOVER:
      this->recover_radio_();
      break;

    case TxState::IDLE:
      break;
  }
}

void CC1101Driver::recover_radio_() {
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
    this->init_registers();

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
}

// ─── Radio Control ────────────────────────────────────────────────────────

void CC1101Driver::flush_and_rx() {
  ESP_LOGVV(TAG, "flush_and_rx");

  // 1. Force IDLE
  (void) this->write_cmd(CC1101_SIDLE);
  esp_rom_delay_us(100);

  // 2. Clear atomic flag (safe — radio is idle, no new interrupts)
  if (this->irq_flag_) {
    this->irq_flag_->store(false, std::memory_order_release);
  }

  // 3. Flush both FIFOs
  (void) this->write_cmd(CC1101_SFRX);
  (void) this->write_cmd(CC1101_SFTX);

  // 4. Re-enable RX
  (void) this->write_cmd(CC1101_SRX);

  // 5. Verify radio entered RX — caller (recover_radio_) handles escalation
  //    Calibration states are transient (~700us) — only warn on truly stuck states.
  uint8_t marc = this->read_status(CC1101_MARCSTATE) & packet::cc1101_status::MARCSTATE_MASK;
  if (marc != CC1101_MARCSTATE_RX && !marcstate_is_transient(marc)) {
    ESP_LOGW(TAG, "flush_and_rx: not in RX after SRX, MARCSTATE=0x%02x", marc);
  }
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
  do {
    a = this->read_status(addr);
    b = this->read_status(addr);
  } while (a != b);
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
