#include "sx1276_driver.h"
#include "cc1101_compat.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cstring>

namespace esphome {
namespace elero {

static const char *const TAG = "elero.sx1276";

// ─── RadioDriver Interface ─────────────��──────────────────────────────────

bool Sx1276Driver::init() {
  this->spi_setup();

  // Hardware reset via RST pin
  this->reset();

  // Must enter sleep mode to switch to FSK (LongRangeMode can only be set in sleep).
  // Write 0x00: sleep + FSK mode (bit 7 = 0 for FSK, bits 2:0 = 000 for sleep).
  this->write_reg_(sx1276::REG_OP_MODE, sx1276::MODE_SLEEP);
  delay(10);

  // Verify chip version
  uint8_t version = this->read_reg_(sx1276::REG_VERSION);
  if (version != sx1276::EXPECTED_VERSION) {
    ESP_LOGE(TAG, "SX1276 version mismatch: got 0x%02x, expected 0x%02x", version,
             sx1276::EXPECTED_VERSION);
    // SX1277/78 may report different versions — warn but continue
    if (version == 0x00 || version == 0xFF) {
      ESP_LOGE(TAG, "SPI communication failure — check wiring");
      return false;
    }
  }

  // Enter standby for configuration
  this->set_standby_();

  // Configure FSK modulation + packet format
  this->configure_fsk_();

  // Set frequency
  this->set_frequency_();

  // Configure PA
  this->set_pa_config_();

  // Configure DIO mapping for RX
  this->set_dio_for_rx_();

  // Flush FIFO and enter RX
  this->flush_fifo_();
  this->set_rx_();

  // Verify mode
  uint8_t mode = this->read_reg_(sx1276::REG_OP_MODE);
  uint8_t irq1 = this->read_reg_(sx1276::REG_IRQ_FLAGS1);
  ESP_LOGI(TAG, "SX1276 init: opmode=0x%02x irq1=0x%02x (ModeReady=%d)",
           mode, irq1, (irq1 & sx1276::IRQ1_MODE_READY) ? 1 : 0);

  if (!(irq1 & sx1276::IRQ1_MODE_READY)) {
    ESP_LOGE(TAG, "SX1276 not ready after init!");
  }

  uint32_t freq_reg = this->freq_reg_from_cc1101_regs_();
  ESP_LOGI(TAG, "SX1276 initialized, FSK mode, freq_reg=0x%06x, version=0x%02x",
           freq_reg, version);

  return true;
}

void Sx1276Driver::reset() {
  if (!this->rst_pin_) {
    return;
  }
  this->rst_pin_->setup();
  this->rst_pin_->digital_write(false);
  delay(1);
  this->rst_pin_->digital_write(true);
  delay(10);  // Datasheet: 5ms after reset
}

bool Sx1276Driver::load_and_transmit(const uint8_t *pkt_buf, size_t len) {
  if (this->tx_in_progress_) {
    return false;
  }

  // Hub provides: [length_byte | data...] (unwhitened CC1101 format).
  // CC1101 receivers expect: whitened(length + data + CRC16).
  // 1. Copy raw packet
  // 2. Compute CC1101 CRC-16 over the raw bytes
  // 3. Append 2 CRC bytes
  // 4. IBM PN9 whiten everything (length + data + CRC)
  // 5. Transmit
  uint8_t data_len = pkt_buf[0];
  size_t raw_total = 1 + data_len;       // length byte + payload
  size_t tx_total = raw_total + 2;       // + 2 CRC bytes

  uint8_t tx_buf[sx1276::FIFO_SIZE];
  if (tx_total > sizeof(tx_buf)) {
    return false;
  }
  memcpy(tx_buf, pkt_buf, raw_total);

  // CC1101 CRC-16 over length byte + data bytes
  uint16_t crc = cc1101_crc16(tx_buf, raw_total);
  tx_buf[raw_total] = static_cast<uint8_t>(crc >> 8);      // CRC MSB first
  tx_buf[raw_total + 1] = static_cast<uint8_t>(crc & 0xFF);

  ESP_LOGD(TAG, "TX raw [%d]: %s", static_cast<int>(tx_total),
           format_hex_pretty(tx_buf, tx_total).c_str());

  // IBM PN9 whiten everything (length + data + CRC)
  cc1101_pn9_whiten(tx_buf, tx_total);

  // Enter standby for FIFO access
  this->set_standby_();

  // Set payload length for TX
  this->write_reg_(sx1276::REG_PAYLOAD_LENGTH, static_cast<uint8_t>(tx_total));

  // Flush FIFO (clear any stale data)
  this->flush_fifo_();

  // Load FIFO via burst write
  this->write_burst_(sx1276::REG_FIFO, tx_buf, tx_total);

  // Configure DIO0 for PacketSent
  this->set_dio_for_tx_();

  // Clear IRQ flags
  this->read_reg_(sx1276::REG_IRQ_FLAGS1);
  this->read_reg_(sx1276::REG_IRQ_FLAGS2);

  // Clear atomic TX done flag before TX
  if (this->tx_done_) {
    this->tx_done_->store(false, std::memory_order_release);
  }

  // Enter TX mode
  this->RadioDriver::mode_ = RadioMode::TX;
  this->set_mode_(sx1276::MODE_TX);

  this->tx_in_progress_ = true;
  this->tx_start_ms_ = millis();
  this->tx_pending_success_ = false;

  return true;
}

TxPollResult Sx1276Driver::poll_tx() {
  if (!this->tx_in_progress_) {
    return this->tx_pending_success_ ? TxPollResult::SUCCESS : TxPollResult::FAILED;
  }

  // Check DIO0 interrupt (PacketSent in TX mode)
  bool irq_fired = this->tx_done_ && this->tx_done_->load(std::memory_order_acquire);
  if (irq_fired) {
    // Verify PacketSent flag
    uint8_t irq2 = this->read_reg_(sx1276::REG_IRQ_FLAGS2);
    if (irq2 & sx1276::IRQ2_PACKET_SENT) {
      ESP_LOGD(TAG, "TX done irq2=0x%02x elapsed=%ums", irq2,
               millis() - this->tx_start_ms_);
      this->tx_in_progress_ = false;
      this->tx_pending_success_ = true;
      this->restore_rx_();
      return TxPollResult::SUCCESS;
    }
  }

  // Also check register directly as fallback (in case IRQ was missed)
  uint8_t irq2 = this->read_reg_(sx1276::REG_IRQ_FLAGS2);
  if (irq2 & sx1276::IRQ2_PACKET_SENT) {
    ESP_LOGD(TAG, "TX done (poll fallback) irq2=0x%02x", irq2);
    this->tx_in_progress_ = false;
    this->tx_pending_success_ = true;
    this->restore_rx_();
    return TxPollResult::SUCCESS;
  }

  // Timeout check
  if (millis() - this->tx_start_ms_ > TX_TIMEOUT_MS) {
    ESP_LOGE(TAG, "TX timeout after %ums", TX_TIMEOUT_MS);
    this->tx_in_progress_ = false;
    this->tx_pending_success_ = false;
    this->stat_tx_recover_.fetch_add(1, std::memory_order_relaxed);
    this->set_standby_();
    this->restore_rx_();
    return TxPollResult::FAILED;
  }

  return TxPollResult::PENDING;
}

void Sx1276Driver::abort_tx() {
  if (this->tx_in_progress_) {
    this->tx_in_progress_ = false;
    this->tx_pending_success_ = false;
    this->stat_tx_recover_.fetch_add(1, std::memory_order_relaxed);
  }
  this->set_standby_();
  this->restore_rx_();
}

bool Sx1276Driver::has_data() {
  if (this->RadioDriver::mode_ != RadioMode::RX) return false;
  return this->rx_ready_ != nullptr && this->rx_ready_->load(std::memory_order_acquire);
}

size_t Sx1276Driver::read_fifo(uint8_t *buf, size_t max_len) {
  // Check PayloadReady flag
  uint8_t irq2 = this->read_reg_(sx1276::REG_IRQ_FLAGS2);

  if (irq2 & sx1276::IRQ2_FIFO_OVERRUN) {
    // FIFO overflow — flush and recover
    ESP_LOGW(TAG, "FIFO overrun detected");
    this->stat_fifo_overflows_.fetch_add(1, std::memory_order_relaxed);
    this->flush_fifo_();
    // Re-enter RX (overrun stops reception)
    this->set_rx_();
    return 0;
  }

  if (!(irq2 & sx1276::IRQ2_PAYLOAD_READY)) {
    return 0;
  }

  // Read fixed-length payload from FIFO
  uint8_t raw[sx1276::RX_FIXED_LEN];
  this->read_burst_(sx1276::REG_FIFO, raw, sx1276::RX_FIXED_LEN);

  // Log raw buffer
  ESP_LOGD(TAG, "RX raw [%d]: %s", sx1276::RX_FIXED_LEN,
           format_hex_pretty(raw, sx1276::RX_FIXED_LEN).c_str());

  // Apply CC1101 IBM PN9 de-whitening in software.
  // The SX1276 hardware whitening is incompatible with CC1101 PN9.
  cc1101_pn9_whiten(raw, sx1276::RX_FIXED_LEN);

  // First de-whitened byte = Elero packet length.
  uint8_t pkt_len = raw[0];

  if (pkt_len < 0x1B || pkt_len > 0x1E) {
    ESP_LOGD(TAG, "bad length 0x%02x after de-whiten", raw[0]);
    return 0;
  }

  // Hub format: [length | data... | RSSI | LQI|CRC_OK]
  size_t total = 1 + pkt_len + 2;
  if (total > max_len || (1 + pkt_len) > sx1276::RX_FIXED_LEN) {
    return 0;
  }

  buf[0] = pkt_len;
  memcpy(buf + 1, raw + 1, pkt_len);

  // Read RSSI (available while in RX or immediately after PayloadReady)
  uint8_t rssi_raw = this->read_reg_(sx1276::REG_RSSI_VALUE);
  // SX1276:  rssi_dbm = -rssi_raw / 2  (HF port, >862 MHz)
  // CC1101:  rssi_dbm = cc1101_byte / 2 - 74   (if cc1101_byte < 128)
  //          rssi_dbm = (cc1101_byte - 256) / 2 - 74  (if cc1101_byte >= 128)
  // Equating: cc1101_byte = -rssi_raw + 148
  // Example:  rssi_raw=200 → -100 dBm → cc1101_byte=-52 → +256=204 → (204-256)/2-74=-100 dBm ✓
  int cc1101_rssi = -static_cast<int>(rssi_raw) + 148;
  if (cc1101_rssi < 0) cc1101_rssi += 256;
  buf[1 + pkt_len] = static_cast<uint8_t>(cc1101_rssi);
  buf[1 + pkt_len + 1] = 0x80;  // LQI=0, CRC_OK=1

  return total;
}

RadioHealth Sx1276Driver::check_health() {
  uint32_t now = millis();
  if (now - this->last_radio_check_ms_ < packet::timing::RADIO_WATCHDOG_INTERVAL) {
    return RadioHealth::OK;
  }
  this->last_radio_check_ms_ = now;

  uint8_t opmode = this->read_reg_(sx1276::REG_OP_MODE);
  uint8_t irq1 = this->read_reg_(sx1276::REG_IRQ_FLAGS1);
  uint8_t irq2 = this->read_reg_(sx1276::REG_IRQ_FLAGS2);
  uint8_t current_mode = opmode & sx1276::MODE_MASK;

  ESP_LOGD(TAG, "health: mode=0x%02x irq1=0x%02x irq2=0x%02x", current_mode, irq1, irq2);

  // Check for FIFO overrun
  if (irq2 & sx1276::IRQ2_FIFO_OVERRUN) {
    ESP_LOGW(TAG, "Radio watchdog: FIFO overrun");
    this->stat_fifo_overflows_.fetch_add(1, std::memory_order_relaxed);
    return RadioHealth::FIFO_OVERFLOW;
  }

  // Should be in RX mode
  if (current_mode == sx1276::MODE_RX) {
    return RadioHealth::OK;
  }
  if (current_mode == sx1276::MODE_TX) {
    return RadioHealth::OK;  // Transient
  }

  // Stuck in standby, sleep, or FS
  ESP_LOGW(TAG, "Radio watchdog: mode=0x%02x, expected RX (0x%02x)", current_mode, sx1276::MODE_RX);
  this->stat_watchdog_recoveries_.fetch_add(1, std::memory_order_relaxed);
  return RadioHealth::STUCK;
}

void Sx1276Driver::recover() {
  ESP_LOGW(TAG, "recover: re-entering RX mode");
  this->set_standby_();

  // Clear IRQ flags
  if (this->rx_ready_) {
    this->rx_ready_->store(false, std::memory_order_release);
  }
  if (this->tx_done_) {
    this->tx_done_->store(false, std::memory_order_release);
  }

  // Restore RX config (also flushes FIFO, clearing overrun flag)
  this->restore_rx_();

  // Verify recovery
  uint8_t irq1 = this->read_reg_(sx1276::REG_IRQ_FLAGS1);
  if (irq1 & sx1276::IRQ1_MODE_READY) {
    return;  // Recovery succeeded
  }

  // Escalate: full hardware reset + re-init
  ESP_LOGE(TAG, "recover: soft recovery failed (irq1=0x%02x), doing full reset", irq1);
  this->reset();
  this->init();
}

void Sx1276Driver::set_frequency_regs(uint8_t f2, uint8_t f1, uint8_t f0) {
  this->freq2_ = f2;
  this->freq1_ = f1;
  this->freq0_ = f0;

  this->set_standby_();
  this->set_frequency_();
  this->restore_rx_();
  ESP_LOGI(TAG, "SX1276 re-initialised: freq2=0x%02x freq1=0x%02x freq0=0x%02x", f2, f1, f0);
}

void Sx1276Driver::dump_config() {
  ESP_LOGCONFIG(TAG, "  Radio: SX1276");
  ESP_LOGCONFIG(TAG, "  freq2: 0x%02x, freq1: 0x%02x, freq0: 0x%02x",
                this->freq2_, this->freq1_, this->freq0_);
  ESP_LOGCONFIG(TAG, "  PA power: %d dBm", this->pa_power_);
  LOG_PIN("  RST Pin: ", this->rst_pin_);
}

// ─── SPI Communication ───────────────────────────────────────────────────

void Sx1276Driver::write_reg_(uint8_t addr, uint8_t val) {
  this->enable();
  this->transfer_byte(addr | sx1276::SPI_WRITE);
  this->transfer_byte(val);
  this->disable();
}

uint8_t Sx1276Driver::read_reg_(uint8_t addr) {
  this->enable();
  this->transfer_byte(addr & 0x7F);  // MSB=0 for read
  uint8_t val = this->transfer_byte(0x00);
  this->disable();
  return val;
}

void Sx1276Driver::write_burst_(uint8_t addr, const uint8_t *data, size_t len) {
  this->enable();
  this->transfer_byte(addr | sx1276::SPI_WRITE);
  for (size_t i = 0; i < len; ++i) {
    this->transfer_byte(data[i]);
  }
  this->disable();
}

void Sx1276Driver::read_burst_(uint8_t addr, uint8_t *data, size_t len) {
  this->enable();
  this->transfer_byte(addr & 0x7F);
  for (size_t i = 0; i < len; ++i) {
    data[i] = this->transfer_byte(0x00);
  }
  this->disable();
}

// ─── Mode Control ───────��─────────────────────────────────────────────────

void Sx1276Driver::set_mode_(uint8_t mode) {
  uint8_t reg = this->read_reg_(sx1276::REG_OP_MODE);
  reg = (reg & ~sx1276::MODE_MASK) | (mode & sx1276::MODE_MASK);
  this->write_reg_(sx1276::REG_OP_MODE, reg);
}

void Sx1276Driver::set_mode_and_wait_(uint8_t mode) {
  this->set_mode_(mode);
  uint32_t start = millis();
  while (!(this->read_reg_(sx1276::REG_IRQ_FLAGS1) & sx1276::IRQ1_MODE_READY)) {
    if (millis() - start > sx1276::MODE_SWITCH_TIMEOUT_MS) {
      ESP_LOGW(TAG, "Mode switch timeout (target=0x%02x)", mode);
      return;
    }
    delay_microseconds_safe(100);
  }
}

void Sx1276Driver::set_standby_() {
  this->set_mode_and_wait_(sx1276::MODE_STANDBY);
}

void Sx1276Driver::set_rx_() {
  this->set_mode_(sx1276::MODE_RX);
}

// ─── Radio Configuration ──────────────────────────────────────────────────

void Sx1276Driver::configure_fsk_() {
  // ── Bitrate: 76800 baud ────────────────────────────────────────────────
  // BitRate = FXOSC / (RegBitrate + RegBitrateFrac/16)
  // 32e6 / (0x01A0 + 0x0B/16) = 32e6 / 416.6875 = 76800.0 bps
  this->write_reg_(sx1276::REG_BITRATE_MSB, sx1276::ELERO_BITRATE_MSB);
  this->write_reg_(sx1276::REG_BITRATE_LSB, sx1276::ELERO_BITRATE_LSB);
  this->write_reg_(sx1276::REG_BITRATE_FRAC, sx1276::ELERO_BITRATE_FRAC);

  // ── Frequency deviation: ~34.9 kHz ────────────────────────────────────
  // Fdev = Fstep * RegFdev = 61.035 * 572 = 34912 Hz
  this->write_reg_(sx1276::REG_FDEV_MSB, sx1276::ELERO_FDEV_MSB);
  this->write_reg_(sx1276::REG_FDEV_LSB, sx1276::ELERO_FDEV_LSB);

  // ── RX bandwidth ──────────────────────────────────────────────────────
  this->write_reg_(sx1276::REG_RX_BW, sx1276::ELERO_RX_BW);
  this->write_reg_(sx1276::REG_AFC_BW, sx1276::ELERO_AFC_BW);

  // ── RX config ───��─────────────────────────────────────────────────────
  // AgcAutoOn=1, AfcAutoOn=1, RxTrigger=PreambleDetect (110)
  this->write_reg_(sx1276::REG_RX_CONFIG, 0x1E);

  // ── RSSI config: 16-sample smoothing ──────────────────────────────────
  this->write_reg_(sx1276::REG_RSSI_CONFIG, 0x03);

  // ── LNA: max gain, LNA boost for HF band ─────���───────────────────────
  this->write_reg_(sx1276::REG_LNA, 0x23);

  // ── Preamble: 12 bytes (96 bits, matches CC1101) ���─────────────────────
  this->write_reg_(sx1276::REG_PREAMBLE_MSB, 0x00);
  this->write_reg_(sx1276::REG_PREAMBLE_LSB, 0x0C);

  // ── Preamble detector: ON, 2-byte, 10 chips tolerance ────────────────
  this->write_reg_(sx1276::REG_PREAMBLE_DETECT, 0xAA);

  // ── Sync word config ──��───────────────────────────────────────────────
  // AutoRestartRxMode=01 (wait for PLL), PreamblePolarity=0xAA, SyncOn=1, SyncSize=3 (4 bytes)
  // CC1101 SYNC_MODE=011 uses a 32-bit sync word: D3 91 D3 91
  this->write_reg_(sx1276::REG_SYNC_CONFIG, 0x53);  // AutoRestart=01, SyncOn=1, SyncSize=3 (4 bytes)
  this->write_reg_(sx1276::REG_SYNC_VALUE1, 0xD3);
  this->write_reg_(sx1276::REG_SYNC_VALUE2, 0x91);
  this->write_reg_(sx1276::REG_SYNC_VALUE3, 0xD3);
  this->write_reg_(sx1276::REG_SYNC_VALUE4, 0x91);

  // ── Packet config ──────────────────────────���──────────────────────────
  // PacketFormat=0 (fixed length), DcFree=00 (off — we do PN9 in software),
  // CrcOn=0 (off — we compute CC1101 CRC in software), AddressFiltering=00
  this->write_reg_(sx1276::REG_PACKET_CONFIG1, 0x00);

  // DataMode=1 (packet), IoHomeOn=0, BeaconOn=0
  this->write_reg_(sx1276::REG_PACKET_CONFIG2, 0x40);

  // ── Payload length (RX fixed length) ──────────────────────────────────
  this->write_reg_(sx1276::REG_PAYLOAD_LENGTH, sx1276::RX_FIXED_LEN);

  // ── FIFO threshold ────────────────────────────────────────────────────
  // TxStartCondition=1 (start on FifoNotEmpty), FifoThreshold=15
  this->write_reg_(sx1276::REG_FIFO_THRESH, 0x8F);
}

void Sx1276Driver::set_frequency_() {
  // CC1101: freq_hz = 26e6 * FREQ / 2^16
  // SX1276: Frf = freq_hz / Fstep = freq_hz * 2^19 / 32e6
  // Combined: Frf = FREQ * 26e6 * 2^19 / (2^16 * 32e6) = FREQ * 26 * 8 / 32 = FREQ * 6.5
  // Use integer math: Frf = FREQ * 13 / 2
  uint32_t cc1101_freq = (static_cast<uint32_t>(this->freq2_) << 16) |
                          (static_cast<uint32_t>(this->freq1_) << 8) |
                          static_cast<uint32_t>(this->freq0_);
  uint32_t frf = static_cast<uint32_t>((static_cast<uint64_t>(cc1101_freq) * 13ULL) / 2ULL);

  this->write_reg_(sx1276::REG_FRF_MSB, static_cast<uint8_t>((frf >> 16) & 0xFF));
  this->write_reg_(sx1276::REG_FRF_MID, static_cast<uint8_t>((frf >> 8) & 0xFF));
  this->write_reg_(sx1276::REG_FRF_LSB, static_cast<uint8_t>(frf & 0xFF));
}

void Sx1276Driver::set_pa_config_() {
  // SX1276 PA configuration:
  // - PA_BOOST pin: OutputPower = 17 - (15 - OutputPower) = 2 + OutputPower [dBm], range 2-17
  // - With PA_DAC boost: up to +20 dBm
  // - RFO pin: OutputPower = Pmax - (15 - OutputPower), Pmax = 10.8 + 0.6 * MaxPower
  if (this->pa_power_ > 17) {
    // +20 dBm mode: PA_BOOST + PA_DAC
    this->write_reg_(sx1276::REG_PA_CONFIG, sx1276::PA_SELECT_BOOST | 0x0F);
    this->write_reg_(sx1276::REG_PA_DAC, sx1276::PA_DAC_BOOST);
    // OCP at 240mA for +20 dBm
    this->write_reg_(sx1276::REG_OCP, 0x3F);
  } else if (this->pa_power_ >= 2) {
    // PA_BOOST: OutputPower = 2 + RegPaConfig[3:0]
    uint8_t output_power = static_cast<uint8_t>(this->pa_power_ - 2);
    this->write_reg_(sx1276::REG_PA_CONFIG, sx1276::PA_SELECT_BOOST | (output_power & 0x0F));
    this->write_reg_(sx1276::REG_PA_DAC, sx1276::PA_DAC_DEFAULT);
    // OCP at 100mA (default)
    this->write_reg_(sx1276::REG_OCP, 0x2B);
  } else {
    // RFO pin for low power: MaxPower=7, OutputPower = 15 - (15 - pa_power_)
    uint8_t output_power = static_cast<uint8_t>(this->pa_power_ + 1);  // RFO range: -1 to +14
    this->write_reg_(sx1276::REG_PA_CONFIG, 0x70 | (output_power & 0x0F));
    this->write_reg_(sx1276::REG_PA_DAC, sx1276::PA_DAC_DEFAULT);
    this->write_reg_(sx1276::REG_OCP, 0x2B);
  }

  // PA ramp: 40us, Gaussian BT=1.0 to match CC1101 MDMCFG2 MOD_FORMAT
  // Bits [6:5]: 00=no shaping, 01=BT=1.0, 10=BT=0.5, 11=BT=0.3
  this->write_reg_(sx1276::REG_PA_RAMP, 0x29);  // bits[6:5]=01 (BT=1.0 Gaussian), ramp=40us
}

void Sx1276Driver::set_dio_for_rx_() {
  // DIO0 = PayloadReady (mapping 00 in RX = PayloadReady)
  // DIO1 = FifoLevel (mapping 00)
  // DIO2 = SyncAddress (mapping 11)
  // DIO3 = FifoEmpty (mapping 00)
  this->write_reg_(sx1276::REG_DIO_MAPPING1, 0x0C);  // DIO0=00, DIO1=00, DIO2=11, DIO3=00

  // DIO4 = Preamble (mapping 11), DIO5 = ModeReady (mapping 11)
  // Bit 0: MapPreambleDetect = 1 (use PreambleDetect instead of RSSI on DIO)
  this->write_reg_(sx1276::REG_DIO_MAPPING2, 0xF1);
}

void Sx1276Driver::set_dio_for_tx_() {
  // DIO0 = PacketSent (mapping 00 in TX = PacketSent)
  // Same register value as RX — DIO0 auto-selects based on TX/RX mode
  this->write_reg_(sx1276::REG_DIO_MAPPING1, 0x0C);
  this->write_reg_(sx1276::REG_DIO_MAPPING2, 0xF1);
}

void Sx1276Driver::flush_fifo_() {
  // Write 1 to FifoOverrun bit to clear FIFO (this also clears any overrun condition)
  this->write_reg_(sx1276::REG_IRQ_FLAGS2, sx1276::IRQ2_FIFO_OVERRUN);
}

void Sx1276Driver::restore_rx_() {
  this->RadioDriver::mode_ = RadioMode::RX;
  this->write_reg_(sx1276::REG_PAYLOAD_LENGTH, sx1276::RX_FIXED_LEN);
  this->set_dio_for_rx_();
  this->flush_fifo_();
  this->set_rx_();
}

uint32_t Sx1276Driver::freq_reg_from_cc1101_regs_() const {
  uint32_t cc1101_freq = (static_cast<uint32_t>(this->freq2_) << 16) |
                          (static_cast<uint32_t>(this->freq1_) << 8) |
                          static_cast<uint32_t>(this->freq0_);
  return static_cast<uint32_t>((static_cast<uint64_t>(cc1101_freq) * 13ULL) / 2ULL);
}

}  // namespace elero
}  // namespace esphome
