#include "sx1262_driver.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cstring>

#ifdef USE_ESP32
#include <esp_timer.h>
#endif

namespace esphome {
namespace elero {

static const char *const TAG = "elero.sx1262";



// ─── RadioDriver Interface ────────────────────────────────────────────────

bool Sx1262Driver::init() {
  this->spi_setup();

  // Setup BUSY pin as input
  if (this->busy_pin_) {
    this->busy_pin_->setup();
  }

  // Setup FEM PA pin (Heltec V4: GPIO46 controls external PA enable)
  if (this->fem_pa_pin_) {
    this->fem_pa_pin_->setup();
    this->fem_pa_pin_->digital_write(false);  // Start in RX mode (PA off)
  }

  // Hardware reset via RST pin
  this->reset();

  // Wait for crystal oscillator to stabilize
  delay(10);

  // ── Init sequence follows RadioLib's proven order ──────────────────────

  // 1. Standby
  this->set_standby_(sx1262::STDBY_RC);

  // 2. TCXO via DIO3 (must be before calibration)
  if (this->tcxo_voltage_ > 0.0f) {
    uint8_t tcxo_reg;
    if (this->tcxo_voltage_ <= 1.6f) tcxo_reg = 0x00;
    else if (this->tcxo_voltage_ <= 1.7f) tcxo_reg = 0x01;
    else if (this->tcxo_voltage_ <= 1.8f) tcxo_reg = 0x02;
    else if (this->tcxo_voltage_ <= 2.2f) tcxo_reg = 0x03;
    else if (this->tcxo_voltage_ <= 2.4f) tcxo_reg = 0x04;
    else if (this->tcxo_voltage_ <= 2.7f) tcxo_reg = 0x05;
    else if (this->tcxo_voltage_ <= 3.0f) tcxo_reg = 0x06;
    else tcxo_reg = 0x07;

    uint8_t tcxo_params[4] = {tcxo_reg, 0x00, 0x02, 0x80};  // 10ms timeout
    this->write_opcode_(sx1262::SET_DIO3_AS_TCXO_CTRL, tcxo_params, 4);
    delay(10);

    uint8_t zeros[2] = {0x00, 0x00};
    this->write_opcode_(sx1262::CLR_DEVICE_ERRORS, zeros, 2);
  }

  // 3. Buffer base addresses (both at 0, like RadioLib)
  uint8_t buf_addr[2] = {0x00, 0x00};
  this->write_opcode_(sx1262::SET_BUFFER_BASE_ADDRESS, buf_addr, 2);

  // 4. Set packet type to GFSK
  uint8_t pkt_type = sx1262::PACKET_TYPE_GFSK;
  this->write_opcode_(sx1262::SET_PACKET_TYPE, &pkt_type, 1);

  // 5. Set RX/TX fallback mode to STDBY_RC (RadioLib does this)
  uint8_t fallback = 0x20;  // STDBY_RC
  this->write_opcode_(sx1262::SET_RX_TX_FALLBACK_MODE, &fallback, 1);

  // 6. Clear IRQs and disable all IRQ routing initially
  {
    uint8_t clear_all[2] = {0xFF, 0xFF};
    this->write_opcode_(sx1262::CLR_IRQ_STATUS, clear_all, 2);
    uint8_t no_irq[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    this->write_opcode_(sx1262::SET_DIO_IRQ_PARAMS, no_irq, 8);
  }

  // 7. Calibrate all blocks (with TCXO running)
  uint8_t cal_mask = 0x7F;
  this->write_opcode_(sx1262::CALIBRATE, &cal_mask, 1);
  this->wait_busy_();

  // 8. Regulator mode (DC-DC)
  uint8_t reg_mode = 0x01;
  this->write_opcode_(sx1262::SET_REGULATOR_MODE, &reg_mode, 1);

  // 9. DIO2 as RF switch (before modulation config)
  if (this->rf_switch_) {
    uint8_t enable = 0x01;
    this->write_opcode_(sx1262::SET_DIO2_AS_RF_SWITCH, &enable, 1);
  }

  // 10. Configure FSK modulation + packet format
  this->configure_fsk_();

  // 11. Set frequency
  this->set_frequency_();

  // 12. Calibrate image for 863-870 MHz band
  uint8_t cal_freq[2] = {0xD7, 0xDB};
  this->write_opcode_(sx1262::CALIBRATE_IMAGE, cal_freq, 2);
  this->wait_busy_();

  // 13. PA config + TX params
  this->set_pa_config_();
  uint8_t tx_params[2] = {static_cast<uint8_t>(this->pa_power_), sx1262::PA_RAMP_200US};
  this->write_opcode_(sx1262::SET_TX_PARAMS, tx_params, 2);

  // 14. Current limit (OCP)
  uint8_t ocp = 0x38;  // 140mA (RadioLib default for SX1262)
  this->write_register_(sx1262::REG_OCP, &ocp, 1);

  // 15. Boosted RX gain
  uint8_t rx_gain = 0x96;
  this->write_register_(sx1262::REG_RX_GAIN, &rx_gain, 1);

  // 16. Enter RX mode
  this->set_dio_irq_for_rx_();
  this->set_rx_();
  this->wait_busy_();

  // Verify init
  this->wait_busy_();
  this->enable();
  uint8_t status = this->transfer_byte(sx1262::GET_STATUS);
  this->disable();
  uint8_t chip_mode = (status >> 4) & 0x07;
  uint8_t cmd_status = (status >> 1) & 0x07;
  ESP_LOGI(TAG, "SX1262 init: chip_mode=0x%02x cmd_status=0x%02x (status=0x%02x)",
           chip_mode, cmd_status, status);

  if (chip_mode != 0x05) {
    ESP_LOGE(TAG, "Failed to enter RX mode!");
  }

  // Read back whitening init registers to verify
  {
    uint8_t w_msb = 0, w_lsb = 0;
    this->read_register_(sx1262::REG_WHITENING_INIT_MSB, &w_msb, 1);
    this->read_register_(sx1262::REG_WHITENING_INIT_LSB, &w_lsb, 1);
    uint16_t w_init = ((w_msb & 0x01) << 8) | w_lsb;
    ESP_LOGI(TAG, "Whitening init: MSB_reg=0x%02x LSB_reg=0x%02x → 9-bit=0x%03x", w_msb, w_lsb, w_init);
  }

  ESP_LOGI(TAG, "SX1262 initialized, FSK mode, freq_reg=0x%08x",
           this->freq_reg_from_cc1101_regs_());

  // === TX HARDWARE TEST: 3s continuous wave on 868.35 MHz ===
  // If CC1101 sees RSSI spike → PA works. If not → hardware broken.
  ESP_LOGW(TAG, "TX TEST: transmitting CW on 868.35 MHz for 10 seconds...");
  this->set_standby_(sx1262::STDBY_XOSC);
  this->set_pa_config_();
  uint8_t tx_p[2] = {static_cast<uint8_t>(this->pa_power_), sx1262::PA_RAMP_200US};
  this->write_opcode_(sx1262::SET_TX_PARAMS, tx_p, 2);
  if (this->fem_pa_pin_) {
    this->fem_pa_pin_->digital_write(true);
  }
  // SET_TX_CONTINUOUS_WAVE = 0xD1
  this->write_opcode_(0xD1, nullptr, 0);
  delay(10000);
  this->set_standby_();
  if (this->fem_pa_pin_) {
    this->fem_pa_pin_->digital_write(false);
  }
  ESP_LOGW(TAG, "TX TEST: CW done. Check CC1101 RSSI logs.");
  // === END TX TEST ===

  // Re-enter RX
  this->set_dio_irq_for_rx_();
  this->set_rx_();
  this->wait_busy_();

  return true;
}

void Sx1262Driver::reset() {
  if (!this->rst_pin_) {
    return;
  }
  this->rst_pin_->setup();
  this->rst_pin_->digital_write(false);
  delay(1);
  this->rst_pin_->digital_write(true);
  delay(5);  // Wait for reset to complete
}

/// CC1101 CRC-16: polynomial 0x8005, init 0xFFFF (CRC-16/IBM/ANSI).
/// Computed over data bytes BEFORE whitening. CC1101 auto-appends this on TX;
/// we must compute it in software since the SX1262 has no compatible CRC.
static uint16_t cc1101_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x8005;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

bool Sx1262Driver::load_and_transmit(const uint8_t *pkt_buf, size_t len) {
  if (this->tx_in_progress_) {
    return false;
  }

  this->set_standby_(sx1262::STDBY_XOSC);

  // Hub provides: [length_byte | data...] (unwhitened CC1101 format).
  // CC1101 receivers expect: whitened(length + data + CRC16).
  // 1. Copy raw packet
  // 2. Compute CC1101 CRC-16 over the raw data (after length byte)
  // 3. Append 2 CRC bytes
  // 4. IBM PN9 whiten everything (length + data + CRC)
  // 5. Transmit
  uint8_t data_len = pkt_buf[0];
  size_t raw_total = 1 + data_len;       // length byte + payload
  size_t tx_total = raw_total + 2;       // + 2 CRC bytes

  uint8_t tx_buf[sx1262::MAX_PACKET_SIZE];
  if (tx_total > sizeof(tx_buf)) {
    return false;
  }
  memcpy(tx_buf, pkt_buf, raw_total);

  // CC1101 CRC-16 over length byte + data bytes (per CC1101 datasheet section 15.2.3:
  // "In variable-length mode, the CRC is calculated over the length byte and the payload")
  uint16_t crc = cc1101_crc16(tx_buf, raw_total);
  tx_buf[raw_total] = static_cast<uint8_t>(crc >> 8);      // CRC MSB first
  tx_buf[raw_total + 1] = static_cast<uint8_t>(crc & 0xFF);

  // Log raw TX bytes before whitening
  ESP_LOGD(TAG, "TX raw [%d]: %s", static_cast<int>(tx_total),
           format_hex_pretty(tx_buf, tx_total).c_str());

  // IBM PN9 whiten everything (length + data + CRC)
  this->whiten_fix_(tx_buf, tx_total);

  // Re-apply PA config before TX (some boards lose PA settings between RX/TX transitions)
  this->set_pa_config_();
  uint8_t tx_params[2] = {static_cast<uint8_t>(this->pa_power_), sx1262::PA_RAMP_200US};
  this->write_opcode_(sx1262::SET_TX_PARAMS, tx_params, 2);

  // Fixed-length TX: send all bytes (length + data + CRC).
  uint8_t pkt_params[9] = {
      0x00, 0x20,  // Preamble: 32 bits
      0x00,        // Preamble detector: OFF
      0x10,        // Sync word: 16 bits
      0x00,        // No address filtering
      0x00,        // Fixed length
      static_cast<uint8_t>(tx_total),
      0x01,        // CRC OFF (we compute CC1101 CRC in software)
      0x00,        // Whitening OFF (IBM PN9 applied in software)
  };
  this->write_opcode_(sx1262::SET_PACKET_PARAMS, pkt_params, 9);

  this->write_fifo_(0x00, tx_buf, tx_total);

  if (this->irq_flag_) {
    this->irq_flag_->store(false, std::memory_order_release);
  }

  this->set_dio_irq_for_tx_();

  // Enable external FEM PA before TX
  if (this->fem_pa_pin_) {
    this->fem_pa_pin_->digital_write(true);
  }

  uint8_t timeout[3] = {0x00, 0x00, 0x00};
  this->write_opcode_(sx1262::SET_TX, timeout, 3);

  this->tx_in_progress_ = true;
  this->tx_start_ms_ = millis();
  this->tx_pending_success_ = false;

  return true;
}

TxPollResult Sx1262Driver::poll_tx() {
  if (!this->tx_in_progress_) {
    return this->tx_pending_success_ ? TxPollResult::SUCCESS : TxPollResult::FAILED;
  }

  // Check DIO1 interrupt (TX_DONE)
  bool irq_fired = this->irq_flag_ && this->irq_flag_->load(std::memory_order_acquire);
  if (irq_fired) {
    // Read and clear IRQ status
    uint8_t irq_buf[2] = {};
    this->read_opcode_(sx1262::GET_IRQ_STATUS, irq_buf, 2);
    uint16_t irq_status = (static_cast<uint16_t>(irq_buf[0]) << 8) | irq_buf[1];

    // Clear all IRQ flags
    this->write_opcode_(sx1262::CLR_IRQ_STATUS, irq_buf, 2);

    if (irq_status & sx1262::IRQ_TX_DONE) {
      ESP_LOGD(TAG, "TX done irq=0x%04x len=%d", irq_status, static_cast<int>(this->tx_start_ms_ ? millis() - this->tx_start_ms_ : 0));
      this->tx_in_progress_ = false;
      this->tx_pending_success_ = true;

      // Disable external FEM PA after TX
      if (this->fem_pa_pin_) {
        this->fem_pa_pin_->digital_write(false);
      }

      // Restore RX packet params (TX changed payload length)
      this->restore_rx_packet_params_();
      this->set_dio_irq_for_rx_();
      this->set_rx_();
      return TxPollResult::SUCCESS;
    }
  }

  // Timeout check
  if (millis() - this->tx_start_ms_ > TX_TIMEOUT_MS) {
    ESP_LOGE(TAG, "TX timeout after %ums", TX_TIMEOUT_MS);
    this->tx_in_progress_ = false;
    this->tx_pending_success_ = false;
    this->stat_tx_recover_.fetch_add(1, std::memory_order_relaxed);

    // Disable external FEM PA on TX failure
    if (this->fem_pa_pin_) {
      this->fem_pa_pin_->digital_write(false);
    }

    // Recover: standby -> restore RX params -> re-enter RX
    this->set_standby_();
    this->restore_rx_packet_params_();
    this->set_dio_irq_for_rx_();
    this->set_rx_();
    return TxPollResult::FAILED;
  }

  return TxPollResult::PENDING;
}

void Sx1262Driver::abort_tx() {
  if (this->fem_pa_pin_) {
    this->fem_pa_pin_->digital_write(false);
  }
  if (this->tx_in_progress_) {
    this->tx_in_progress_ = false;
    this->tx_pending_success_ = false;
    this->stat_tx_recover_.fetch_add(1, std::memory_order_relaxed);
  }
  this->set_standby_();
  this->set_dio_irq_for_rx_();
  this->set_rx_();
}

bool Sx1262Driver::has_data() {
  return this->irq_flag_ != nullptr && this->irq_flag_->load(std::memory_order_acquire);
}

size_t Sx1262Driver::read_fifo(uint8_t *buf, size_t max_len) {
  // Read and clear IRQ status
  uint8_t irq_buf[2] = {};
  this->read_opcode_(sx1262::GET_IRQ_STATUS, irq_buf, 2);
  uint16_t irq_status = (static_cast<uint16_t>(irq_buf[0]) << 8) | irq_buf[1];
  this->write_opcode_(sx1262::CLR_IRQ_STATUS, irq_buf, 2);

  if (!(irq_status & sx1262::IRQ_RX_DONE)) {
    return 0;
  }

  // Get RX buffer status. In fixed-length mode, payload_len == RX_FIXED_LEN.
  // The buffer includes the CC1101 length byte as byte 0 (SX1262 doesn't strip it).
  uint8_t rx_status[2] = {};
  this->read_opcode_(sx1262::GET_RX_BUFFER_STATUS, rx_status, 2);
  uint8_t payload_len = rx_status[0];
  uint8_t rx_offset = rx_status[1];

  if (payload_len == 0 || payload_len > sx1262::RX_FIXED_LEN) {
    return 0;
  }

  // Read all fixed-length bytes from SX1262 buffer.
  uint8_t raw[sx1262::RX_FIXED_LEN];
  this->read_fifo_(rx_offset, raw, payload_len);

  // Log raw buffer
  {
    char hex[sx1262::RX_FIXED_LEN * 3 + 1];
    size_t pos = 0;
    for (size_t i = 0; i < payload_len && pos < sizeof(hex) - 3; ++i) {
      pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x ", raw[i]);
    }
    ESP_LOGD(TAG, "RX raw [%d]: %s", payload_len, hex);
  }

  // With whitening OFF, the SX1262 includes the 2-byte sync word (0xD391) at the
  // start of the buffer. Actual CC1101-whitened packet data starts at offset 2.
  // Bytes 3+ match CC1101 whitened data perfectly. Byte 2 has a 1-bit LSB error.
  static constexpr size_t SYNC_PREFIX = 2;
  if (payload_len <= SYNC_PREFIX) {
    return 0;
  }

  uint8_t *data = raw + SYNC_PREFIX;
  size_t data_len = payload_len - SYNC_PREFIX;

  // Apply CC1101 IBM PN9 de-whitening in software.
  this->whiten_fix_(data, data_len);

  // First de-whitened byte = Elero packet length.
  // The 1-bit LSB error at the sync/data boundary flips bit 0 (e.g., 0x1D→0x1C).
  // Correct by ORing bit 0 for known even→odd cases.
  uint8_t pkt_len = data[0];
  if (pkt_len == 0x1C || pkt_len == 0x1A) {
    pkt_len |= 0x01;  // 0x1C→0x1D, 0x1A→0x1B
  }

  if (pkt_len < 0x1B || pkt_len > 0x1E) {
    ESP_LOGD(TAG, "bad length 0x%02x after de-whiten (raw[2]=0x%02x)", data[0], raw[SYNC_PREFIX]);
    return 0;
  }

  // Hub format: [length | data... | RSSI | LQI|CRC_OK]
  size_t total = 1 + pkt_len + 2;
  if (total > max_len || (1 + pkt_len) > data_len) {
    return 0;
  }

  buf[0] = pkt_len;
  memcpy(buf + 1, data + 1, pkt_len);

  // Synthesize CC1101-format RSSI and LQI status bytes
  uint8_t pkt_status_buf[3] = {};
  this->read_opcode_(sx1262::GET_PACKET_STATUS, pkt_status_buf, 3);
  int rssi_dbm_x2 = -static_cast<int>(pkt_status_buf[2]);
  int cc1101_rssi = rssi_dbm_x2 + 148;
  if (cc1101_rssi < 0) cc1101_rssi += 256;
  buf[1 + pkt_len] = static_cast<uint8_t>(cc1101_rssi);
  buf[1 + pkt_len + 1] = 0x80;  // LQI=0, CRC_OK=1

  return total;
}

RadioHealth Sx1262Driver::check_health() {
  uint32_t now = millis();
  if (now - this->last_radio_check_ms_ < packet::timing::RADIO_WATCHDOG_INTERVAL) {
    return RadioHealth::OK;
  }
  this->last_radio_check_ms_ = now;

  // Read chip status via GetStatus
  // The SX1262 doesn't have the complex MARCSTATE of CC1101.
  // We just verify the chip is responsive by reading its status.
  this->wait_busy_();
  this->enable();
  uint8_t status = this->transfer_byte(sx1262::GET_STATUS);
  this->transfer_byte(0x00);  // NOP
  this->disable();

  // Status byte format: bits [6:4] = chip mode, bits [3:1] = command status
  uint8_t chip_mode = (status >> 4) & 0x07;

  // Also read IRQ status and IRQ flag for diagnostics
  uint8_t irq_buf[2] = {};
  this->read_opcode_(sx1262::GET_IRQ_STATUS, irq_buf, 2);
  uint16_t irq_status = (static_cast<uint16_t>(irq_buf[0]) << 8) | irq_buf[1];
  bool irq_flag = this->irq_flag_ && this->irq_flag_->load(std::memory_order_relaxed);

  // Read device errors
  uint8_t err_buf[2] = {};
  this->read_opcode_(sx1262::CLR_DEVICE_ERRORS, err_buf, 2);
  uint16_t dev_errors = (static_cast<uint16_t>(err_buf[0]) << 8) | err_buf[1];
  if (dev_errors != 0) {
    uint8_t clear[2] = {0x00, 0x00};
    this->write_opcode_(sx1262::CLR_DEVICE_ERRORS, clear, 2);
  }

  ESP_LOGD(TAG, "health: mode=%d irq=0x%04x flag=%d err=0x%04x",
           chip_mode, irq_status, irq_flag, dev_errors);

  // chip_mode: 0x02=STBY_RC, 0x03=STBY_XOSC, 0x04=FS, 0x05=RX, 0x06=TX
  if (chip_mode == 0x05) {
    return RadioHealth::OK;  // In RX — expected
  }
  if (chip_mode == 0x06) {
    return RadioHealth::OK;  // In TX — transient
  }

  // Chip is in standby or FS — stuck, needs recovery
  uint8_t cmd_status = (status >> 1) & 0x07;
  ESP_LOGW(TAG, "Radio watchdog: chip_mode=0x%02x cmd_status=0x%02x (status=0x%02x), expected RX",
           chip_mode, cmd_status, status);
  this->stat_watchdog_recoveries_.fetch_add(1, std::memory_order_relaxed);
  return RadioHealth::STUCK;
}

void Sx1262Driver::recover() {
  ESP_LOGW(TAG, "recover: re-entering RX mode");
  this->set_standby_();

  // Clear any pending IRQ
  uint8_t clear_all[2] = {0xFF, 0xFF};
  this->write_opcode_(sx1262::CLR_IRQ_STATUS, clear_all, 2);

  if (this->irq_flag_) {
    this->irq_flag_->store(false, std::memory_order_release);
  }

  this->set_dio_irq_for_rx_();
  this->set_rx_();
}

void Sx1262Driver::set_frequency_regs(uint8_t f2, uint8_t f1, uint8_t f0) {
  this->freq2_ = f2;
  this->freq1_ = f1;
  this->freq0_ = f0;

  this->set_standby_();
  this->set_frequency_();

  // Re-calibrate image for the (potentially) new frequency
  uint8_t cal_freq[2] = {0xD7, 0xDB};  // 850-900 MHz
  this->write_opcode_(sx1262::CALIBRATE_IMAGE, cal_freq, 2);

  this->set_dio_irq_for_rx_();
  this->set_rx_();
  ESP_LOGI(TAG, "SX1262 re-initialised: freq2=0x%02x freq1=0x%02x freq0=0x%02x", f2, f1, f0);
}

void Sx1262Driver::dump_config() {
  ESP_LOGCONFIG(TAG, "  Radio: SX1262");
  ESP_LOGCONFIG(TAG, "  freq2: 0x%02x, freq1: 0x%02x, freq0: 0x%02x",
                this->freq2_, this->freq1_, this->freq0_);
  ESP_LOGCONFIG(TAG, "  PA power: %d dBm", this->pa_power_);
  ESP_LOGCONFIG(TAG, "  RF switch (DIO2): %s", this->rf_switch_ ? "enabled" : "disabled");
  LOG_PIN("  BUSY Pin: ", this->busy_pin_);
  LOG_PIN("  RST Pin: ", this->rst_pin_);
}

// ─── SPI Communication ───────────────────────────────────────────────────

void Sx1262Driver::wait_busy_() {
  if (!this->busy_pin_) {
    delay_microseconds_safe(100);  // Fallback delay if no BUSY pin
    return;
  }
  uint32_t start = millis();
  while (this->busy_pin_->digital_read()) {
    if (millis() - start > sx1262::BUSY_TIMEOUT_MS) {
      ESP_LOGE(TAG, "BUSY pin timeout");
      return;
    }
    delay_microseconds_safe(10);
  }
}

void Sx1262Driver::write_opcode_(uint8_t opcode, const uint8_t *data, size_t len) {
  this->wait_busy_();
  this->enable();
  this->transfer_byte(opcode);
  for (size_t i = 0; i < len; ++i) {
    this->transfer_byte(data[i]);
  }
  this->disable();
}

void Sx1262Driver::read_opcode_(uint8_t opcode, uint8_t *data, size_t len) {
  this->wait_busy_();
  this->enable();
  this->transfer_byte(opcode);
  this->transfer_byte(0x00);  // NOP (status byte)
  for (size_t i = 0; i < len; ++i) {
    data[i] = this->transfer_byte(0x00);
  }
  this->disable();
}

void Sx1262Driver::write_register_(uint16_t addr, const uint8_t *data, size_t len) {
  this->wait_busy_();
  this->enable();
  this->transfer_byte(sx1262::WRITE_REGISTER);
  this->transfer_byte(static_cast<uint8_t>(addr >> 8));
  this->transfer_byte(static_cast<uint8_t>(addr & 0xFF));
  for (size_t i = 0; i < len; ++i) {
    this->transfer_byte(data[i]);
  }
  this->disable();
}

void Sx1262Driver::read_register_(uint16_t addr, uint8_t *data, size_t len) {
  this->wait_busy_();
  this->enable();
  this->transfer_byte(sx1262::READ_REGISTER);
  this->transfer_byte(static_cast<uint8_t>(addr >> 8));
  this->transfer_byte(static_cast<uint8_t>(addr & 0xFF));
  this->transfer_byte(0x00);  // NOP (status byte)
  for (size_t i = 0; i < len; ++i) {
    data[i] = this->transfer_byte(0x00);
  }
  this->disable();
}

void Sx1262Driver::write_fifo_(uint8_t offset, const uint8_t *data, size_t len) {
  this->wait_busy_();
  this->enable();
  this->transfer_byte(sx1262::WRITE_BUFFER);
  this->transfer_byte(offset);
  for (size_t i = 0; i < len; ++i) {
    this->transfer_byte(data[i]);
  }
  this->disable();
}

void Sx1262Driver::read_fifo_(uint8_t offset, uint8_t *data, size_t len) {
  this->wait_busy_();
  this->enable();
  this->transfer_byte(sx1262::READ_BUFFER);
  this->transfer_byte(offset);
  this->transfer_byte(0x00);  // NOP (status byte)
  for (size_t i = 0; i < len; ++i) {
    data[i] = this->transfer_byte(0x00);
  }
  this->disable();
}

// ─── Radio Control ────────────────────────────────────────────────────────

void Sx1262Driver::set_standby_(uint8_t mode) {
  this->write_opcode_(sx1262::SET_STANDBY, &mode, 1);
}

void Sx1262Driver::set_rx_() {
  // Continuous RX (timeout = 0xFFFFFF)
  uint8_t timeout[3] = {0xFF, 0xFF, 0xFF};
  this->write_opcode_(sx1262::SET_RX, timeout, 3);
}

void Sx1262Driver::set_tx_() {
  // No timeout (0x000000) — we handle timeout ourselves
  uint8_t timeout[3] = {0x00, 0x00, 0x00};
  this->write_opcode_(sx1262::SET_TX, timeout, 3);
}

void Sx1262Driver::configure_fsk_() {
  // NOTE: SetPacketType is already called in init() before this function.

  // ── Modulation parameters ──────────────────────────────────────────────
  uint32_t br = sx1262::ELERO_BITRATE;
  uint32_t fdev = sx1262::ELERO_FDEV;
  uint8_t mod_params[8] = {
      static_cast<uint8_t>((br >> 16) & 0xFF),
      static_cast<uint8_t>((br >> 8) & 0xFF),
      static_cast<uint8_t>(br & 0xFF),
      0x09,                     // Gaussian BT=0.5 (GFSK, matches CC1101 MOD_FORMAT=GFSK)
      sx1262::BW_FSK_234300,    // RX bandwidth 234.3 kHz (matches CC1101's 232 kHz)
      static_cast<uint8_t>((fdev >> 16) & 0xFF),
      static_cast<uint8_t>((fdev >> 8) & 0xFF),
      static_cast<uint8_t>(fdev & 0xFF),
  };
  this->write_opcode_(sx1262::SET_MODULATION_PARAMS, mod_params, 8);

  // ── fixGFSK: undocumented Semtech register writes (from RadioLib) ─────
  // Required for correct FSK operation at all non-special bitrates.
  {
    uint8_t val;
    this->read_register_(sx1262::REG_GFSK_FIX_1, &val, 1);
    val = (val & 0xE7) | 0x08;  // bits[4:3] = 0x08
    this->write_register_(sx1262::REG_GFSK_FIX_1, &val, 1);

    this->read_register_(sx1262::REG_RSSI_AVG_WINDOW, &val, 1);
    val = val & 0xE3;  // bits[4:2] = 0x00
    this->write_register_(sx1262::REG_RSSI_AVG_WINDOW, &val, 1);

    this->read_register_(sx1262::REG_GFSK_FIX_3, &val, 1);
    val = (val & 0xEF) | 0x10;  // bits[4:4] = 0x10
    this->write_register_(sx1262::REG_GFSK_FIX_3, &val, 1);

    this->read_register_(sx1262::REG_GFSK_FIX_4, &val, 1);
    val = val & 0x8F;  // bits[6:4] = 0x00
    this->write_register_(sx1262::REG_GFSK_FIX_4, &val, 1);
  }

  // ── Packet parameters ───────────────────────────────────────────────────
  // Whitening OFF: the SX1262's hardware "whitening" is a data-dependent scrambler
  // (not a standard PN9 whitener), incompatible with CC1101's IBM PN9.
  // With whitening OFF, the SX1262 includes the 2-byte sync word in the buffer
  // and has a 1-bit error at the first data byte. We handle both in read_fifo.
  // CC1101 IBM PN9 de-whitening is applied in software.
  // Fixed length: 32 bytes = 2 sync + 30 data (covers all Elero packet sizes).
  // CRC OFF: Elero CRC is verified after AES decryption in decode_packet.
  uint8_t pkt_params[9] = {
      0x00, 0x20,  // Preamble: 32 bits
      0x00,        // Preamble detector: OFF (rely on sync word only)
      0x10,        // Sync word: 16 bits
      0x00,        // No address filtering
      0x00,        // Fixed length
      sx1262::RX_FIXED_LEN,
      0x01,        // CRC OFF
      0x00,        // Whitening OFF
  };
  this->write_opcode_(sx1262::SET_PACKET_PARAMS, pkt_params, 9);

  // ── Sync word: 0xD391 ─────────────────────────────────────────────────
  uint8_t sync_word[8] = {0xD3, 0x91, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  this->write_register_(sx1262::REG_SYNCWORD, sync_word, 8);
}

void Sx1262Driver::set_frequency_() {
  uint32_t freq_reg = this->freq_reg_from_cc1101_regs_();
  uint8_t freq_buf[4] = {
      static_cast<uint8_t>((freq_reg >> 24) & 0xFF),
      static_cast<uint8_t>((freq_reg >> 16) & 0xFF),
      static_cast<uint8_t>((freq_reg >> 8) & 0xFF),
      static_cast<uint8_t>(freq_reg & 0xFF),
  };
  this->write_opcode_(sx1262::SET_RF_FREQUENCY, freq_buf, 4);
}

void Sx1262Driver::set_pa_config_() {
  // SX1262 PA config for +22 dBm max output
  // paDutyCycle=0x04, hpMax=0x07, deviceSel=0x00 (SX1262), paLut=0x01
  uint8_t pa_config[4] = {0x04, 0x07, 0x00, 0x01};
  this->write_opcode_(sx1262::SET_PA_CONFIG, pa_config, 4);
}

void Sx1262Driver::set_dio_irq_for_rx_() {
  // Enable ALL useful FSK IRQs on DIO1 for debugging
  // Only RX_DONE + TIMEOUT on DIO1. Do NOT include preamble/sync — they cause
  // spurious ISR fires that race with read_fifo and eat real RX_DONE events.
  uint16_t irq_mask = sx1262::IRQ_RX_DONE | sx1262::IRQ_TIMEOUT;
  uint8_t dio_params[8] = {
      static_cast<uint8_t>(irq_mask >> 8), static_cast<uint8_t>(irq_mask & 0xFF),  // IRQ mask
      static_cast<uint8_t>(irq_mask >> 8), static_cast<uint8_t>(irq_mask & 0xFF),  // DIO1 mask
      0x00, 0x00,  // DIO2 mask (none)
      0x00, 0x00,  // DIO3 mask (none)
  };
  this->write_opcode_(sx1262::SET_DIO_IRQ_PARAMS, dio_params, 8);
}

void Sx1262Driver::set_dio_irq_for_tx_() {
  // Enable TX_DONE + TIMEOUT on DIO1
  uint16_t irq_mask = sx1262::IRQ_TX_DONE | sx1262::IRQ_TIMEOUT;
  uint8_t dio_params[8] = {
      static_cast<uint8_t>(irq_mask >> 8), static_cast<uint8_t>(irq_mask & 0xFF),  // IRQ mask
      static_cast<uint8_t>(irq_mask >> 8), static_cast<uint8_t>(irq_mask & 0xFF),  // DIO1 mask
      0x00, 0x00,
      0x00, 0x00,
  };
  this->write_opcode_(sx1262::SET_DIO_IRQ_PARAMS, dio_params, 8);
}

void Sx1262Driver::whiten_fix_(uint8_t *data, size_t len) {
  // CC1101 IBM PN9 whitening/de-whitening (XOR is self-inverse).
  // Polynomial x^9 + x^5 + 1, seed 0x1FF, right-shifting LFSR.
  // Output bit 0 (LSB), assembled LSB-first into bytes.
  //
  // With SX1262 hardware whitening OFF, the received data is the raw CC1101-whitened
  // bitstream. Applying IBM PN9 recovers the original plaintext.
  uint16_t key = 0x1FF;
  for (size_t i = 0; i < len; ++i) {
    data[i] ^= key & 0xFF;
    for (int j = 0; j < 8; ++j) {
      uint16_t msb = ((key >> 5) ^ (key >> 0)) & 1;
      key = (key >> 1) | (msb << 8);
    }
  }
}

void Sx1262Driver::restore_rx_packet_params_() {
  // Fixed length, CRC OFF, whitening OFF (must match configure_fsk_)
  uint8_t pkt_params[9] = {
      0x00, 0x20, 0x04, 0x10, 0x00,
      0x00, sx1262::RX_FIXED_LEN, 0x01, 0x00,
  };
  this->write_opcode_(sx1262::SET_PACKET_PARAMS, pkt_params, 9);
}

uint32_t Sx1262Driver::freq_reg_from_cc1101_regs_() const {
  // CC1101: freq_hz = 26e6 * FREQ / 2^16
  // SX1262: RfFreq = freq_hz * 2^25 / 32e6
  // Combined: RfFreq = FREQ * 26 * 2^9 / 32 = FREQ * 416 (exact)
  uint32_t cc1101_freq = (static_cast<uint32_t>(this->freq2_) << 16) |
                          (static_cast<uint32_t>(this->freq1_) << 8) |
                          static_cast<uint32_t>(this->freq0_);
  // RfFreq = FREQ * 26e6 * 2^25 / (2^16 * 32e6) = FREQ * 26 * 16 = FREQ * 416
  return static_cast<uint32_t>(static_cast<uint64_t>(cc1101_freq) * 416ULL);
}

}  // namespace elero
}  // namespace esphome
