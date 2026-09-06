#include "sx1262_driver.h"
#include "cc1101_compat.h"
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

  // ── Power up FEM before SX1262 reset/calibration ──────────────────────────
  // The GC1109/KCT8103L FEM must be powered and enabled BEFORE the SX1262
  // calibrates, so it sees the correct antenna impedance during image cal.
  // Heltec V4.2 GC1109: GPIO7=power, GPIO2=CSD(enable), GPIO46=CPS(PA mode)
  if (this->fem_power_pin_) {
    this->fem_power_pin_->setup();
    this->fem_power_pin_->digital_write(true);   // Power FEM LDO
    ESP_LOGD(TAG, "FEM power pin set HIGH");
  }
  if (this->fem_enable_pin_) {
    this->fem_enable_pin_->setup();
    this->fem_enable_pin_->digital_write(true);   // Enable FEM chip (CSD=1)
    ESP_LOGD(TAG, "FEM enable pin set HIGH");
    delay(1);  // Heltec reference: 1ms settle after CSD enable
  }
  if (this->fem_pa_pin_) {
    this->fem_pa_pin_->setup();
    this->fem_pa_pin_->digital_write(false);  // Start in RX/LNA mode (CPS=0)
    ESP_LOGD(TAG, "FEM PA pin set LOW (RX/LNA mode)");
  }

  // Hardware reset via RST pin
  this->reset();

  // Wait for crystal oscillator to stabilize
  delay(10);

  // ── Verify SPI communication ────────────────────────────────────────────
  // Write a known value to the sync word register and read it back.
  // Catches MOSI wiring issues where reads work (MISO OK) but writes don't.
  {
    uint8_t test_val = 0xA5;
    if (!this->write_register_(sx1262::REG_SYNCWORD, &test_val, 1)) return false;
    uint8_t readback = 0;
    if (!this->read_register_(sx1262::REG_SYNCWORD, &readback, 1)) return false;
    if (readback != test_val) {
      ESP_LOGE(TAG, "SPI write verification failed: wrote 0x%02x to SYNCWORD, read 0x%02x", test_val, readback);
      ESP_LOGE(TAG, "  Check MOSI wiring (MISO may still work)");
      this->failed_ = true;
      return false;
    }
  }

  // ── Init sequence follows RadioLib's proven order ──────────────────────

  // 1. Standby
  if (!this->set_standby_(sx1262::STDBY_RC)) return false;

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
    if (!this->write_opcode_(sx1262::SET_DIO3_AS_TCXO_CTRL, tcxo_params, 4)) return false;
    delay(10);

    uint8_t zeros[2] = {0x00, 0x00};
    if (!this->write_opcode_(sx1262::CLR_DEVICE_ERRORS, zeros, 2)) return false;
  }

  // 3. Buffer base addresses (both at 0, like RadioLib)
  uint8_t buf_addr[2] = {0x00, 0x00};
  if (!this->write_opcode_(sx1262::SET_BUFFER_BASE_ADDRESS, buf_addr, 2)) return false;

  // 4. Set packet type to GFSK
  uint8_t pkt_type = sx1262::PACKET_TYPE_GFSK;
  if (!this->write_opcode_(sx1262::SET_PACKET_TYPE, &pkt_type, 1)) return false;

  // 5. Set RX/TX fallback mode to STDBY_RC (RadioLib does this)
  uint8_t fallback = 0x20;  // STDBY_RC
  if (!this->write_opcode_(sx1262::SET_RX_TX_FALLBACK_MODE, &fallback, 1)) return false;

  // 6. Clear IRQs and disable all IRQ routing initially
  {
    uint8_t clear_all[2] = {0xFF, 0xFF};
    if (!this->write_opcode_(sx1262::CLR_IRQ_STATUS, clear_all, 2)) return false;
    uint8_t no_irq[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (!this->write_opcode_(sx1262::SET_DIO_IRQ_PARAMS, no_irq, 8)) return false;
  }

  // 7. Calibrate all blocks (with TCXO running)
  uint8_t cal_mask = 0x7F;
  if (!this->write_opcode_(sx1262::CALIBRATE, &cal_mask, 1)) return false;
  if (!this->wait_busy_()) return false;

  // 8. Regulator mode (DC-DC)
  uint8_t reg_mode = 0x01;
  if (!this->write_opcode_(sx1262::SET_REGULATOR_MODE, &reg_mode, 1)) return false;

  // 9. DIO2 as RF switch (before modulation config)
  if (this->rf_switch_) {
    uint8_t enable = 0x01;
    if (!this->write_opcode_(sx1262::SET_DIO2_AS_RF_SWITCH, &enable, 1)) return false;
  }

  // 10. Configure FSK modulation + packet format
  if (!this->configure_fsk_()) return false;

  // 11. Set frequency
  if (!this->set_frequency_()) return false;

  // 12. Calibrate image for 863-870 MHz band
  uint8_t cal_freq[2] = {0xD7, 0xDB};
  if (!this->write_opcode_(sx1262::CALIBRATE_IMAGE, cal_freq, 2)) return false;
  if (!this->wait_busy_()) return false;

  // 13. PA config + TX params + errata fixes
  if (!this->set_pa_config_()) return false;
  if (!this->apply_errata_pa_clamping_()) return false;
  uint8_t tx_params[2] = {static_cast<uint8_t>(this->pa_power_), sx1262::PA_RAMP_200US};
  if (!this->write_opcode_(sx1262::SET_TX_PARAMS, tx_params, 2)) return false;

  // 14. Sensitivity/modulation register fix (RadioLib fixSensitivity)
  if (!this->apply_errata_sensitivity_()) return false;

  // 15. Current limit (OCP)
  uint8_t ocp = 0x38;  // 140mA (RadioLib default for SX1262)
  if (!this->write_register_(sx1262::REG_OCP, &ocp, 1)) return false;

  // 16. Boosted RX gain
  uint8_t rx_gain = 0x96;
  if (!this->write_register_(sx1262::REG_RX_GAIN, &rx_gain, 1)) return false;

  // 17. Enter RX mode
  if (!this->set_dio_irq_for_rx_()) return false;
  if (!this->set_rx_()) return false;
  if (!this->wait_busy_()) return false;

  // Verify init — chip should be in RX (0x05)
  uint8_t chip_mode = this->read_chip_mode_();
  ESP_LOGI(TAG, "SX1262 init: chip_mode=0x%02x (expect 0x05=RX)", chip_mode);

  if (chip_mode != 0x05) {
    ESP_LOGE(TAG, "Failed to enter RX mode!");
    return false;
  }

  ESP_LOGI(TAG, "SX1262 initialized, FSK mode, freq_reg=0x%08x",
           this->freq_reg_from_cc1101_regs_());

  // Verify sync word register wasn't corrupted by calibration/init
  {
    uint8_t sw[4] = {};
    if (!this->read_register_(sx1262::REG_SYNCWORD, sw, 4)) return false;
    ESP_LOGI(TAG, "Sync word readback: %02x %02x %02x %02x (expect D3 91 D3 91)",
             sw[0], sw[1], sw[2], sw[3]);
  }

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

// cc1101_crc16() and cc1101_pn9_whiten() are in cc1101_compat.h (inline, testable on host)

bool Sx1262Driver::load_and_transmit(const uint8_t *pkt_buf, size_t len) {
  if (!this->tx_fsm_.is_idle()) {
    ESP_LOGW(TAG, "TX start rejected: FSM busy");
    return false;
  }

  if (pkt_buf == nullptr || len == 0 || len > sizeof(this->tx_buf_)) {
    ESP_LOGW(TAG, "TX start rejected: invalid buffer ptr=%p len=%u", pkt_buf,
             static_cast<unsigned>(len));
    return false;
  }

  memcpy(this->tx_buf_, pkt_buf, len);
  this->tx_len_ = len;

  uint32_t now = millis();
  if (!this->tx_fsm_.Start(now)) {
    ESP_LOGW(TAG, "TX start rejected: FSM refused start");
    return false;
  }

  // Preserve the RadioDriver contract: load_and_transmit() performs the
  // synchronous prepare phase and returns false on immediate hardware failure.
  this->tx_fsm_.Poll(now);
  if (this->tx_fsm_.is_idle()) {
    return this->tx_terminal_result_ == SemtechTxTerminalResult::Success;
  }

  return true;
}

TxPollResult Sx1262Driver::poll_tx() {
  if (this->tx_fsm_.is_idle()) {
    return this->tx_terminal_result_ == SemtechTxTerminalResult::Success
               ? TxPollResult::SUCCESS
               : TxPollResult::FAILED;
  }

  this->tx_fsm_.Poll(millis());
  if (!this->tx_fsm_.is_idle()) {
    return TxPollResult::PENDING;
  }

  return this->tx_terminal_result_ == SemtechTxTerminalResult::Success
             ? TxPollResult::SUCCESS
             : TxPollResult::FAILED;
}

void Sx1262Driver::abort_tx() {
  this->tx_fsm_.Abort(millis());
}

bool Sx1262Driver::has_data() {
  if (this->RadioDriver::mode() != RadioMode::RX) return false;
  if (this->rx_capture_active_) return true;
  // Poll latched flags too: an edge can be coalesced during TX/RX handoff.
  uint8_t irq[2]{};
  if (!this->read_opcode_(sx1262::GET_IRQ_STATUS, irq, sizeof(irq))) return false;
  return ((static_cast<uint16_t>(irq[0]) << 8) | irq[1]) != 0;
}

size_t Sx1262Driver::read_fifo(uint8_t *buf, size_t max_len) {
  if (!this->rx_capture_active_) {
    uint8_t irq[2]{};
    if (!this->read_opcode_(sx1262::GET_IRQ_STATUS, irq, sizeof(irq))) return 0;
    const uint16_t flags = (static_cast<uint16_t>(irq[0]) << 8) | irq[1];
    if (!this->write_opcode_(sx1262::CLR_IRQ_STATUS, irq, sizeof(irq))) return 0;
    if (!(flags & (sx1262::IRQ_SYNCWORD_VALID | sx1262::IRQ_RX_DONE))) return 0;
    uint8_t packet_status[3]{};
    if (!this->read_opcode_(sx1262::GET_PACKET_STATUS, packet_status, 3)) {
      this->recover();
      return 0;
    }
    this->rx_capture_rssi_ = packet_status[1];  // RssiSync, latched at sync detection.
    this->rx_capture_active_ = true;
    this->rx_capture_started_ms_ = millis();
    // DONE is sufficient; otherwise use a bounded capture from sync detection.
    if (!(flags & sx1262::IRQ_RX_DONE)) return 0;
  } else if (millis() - this->rx_capture_started_ms_ < RX_CAPTURE_MS) {
    return 0;
  }

  // Freeze the buffer before reading. Single RX always starts at base zero;
  // the buffer was cleared before arming so truncated packets cannot reuse an
  // old packet's CRC. RSSI was captured at sync, before a short frame ends.
  const uint8_t rssi = this->rx_capture_rssi_;
  if (!this->set_standby_()) {
    this->rx_capture_active_ = false;
    this->recover();
    return 0;
  }
  uint8_t raw[sx1262::RX_FIXED_LEN]{};
  const bool got_frame = this->read_fifo_(0, raw, sizeof(raw));
  this->rx_capture_active_ = false;
  if (!this->clear_irq_status_() || !this->set_rx_()) {
    this->recover();
    return 0;
  }
  if (!got_frame) return 0;
  cc1101_pn9_whiten(raw, sizeof(raw));
  if (!cc1101_frame_valid(raw, sizeof(raw))) {
    ESP_LOGD(TAG, "RX frame rejected: invalid length or CRC");
    return 0;
  }
  const size_t total = raw[0] + 3u;
  if (buf == nullptr || total > max_len) return 0;
  memcpy(buf, raw, total - 2);
  buf[total - 2] = static_cast<uint8_t>(148 - static_cast<int>(rssi));
  buf[total - 1] = 0x80;  // Software CRC verified; LQI unavailable.
  return total;
}

RadioHealth Sx1262Driver::check_health() {
  uint32_t now = millis();
  if (now - this->last_radio_check_ms_ < packet::timing::RADIO_WATCHDOG_INTERVAL) {
    return RadioHealth::OK;
  }
  this->last_radio_check_ms_ = now;

  // ── Read chip status ──────────────────────────────────────────────────────
  uint8_t chip_mode = this->read_chip_mode_();
  if (chip_mode == 0xFF) {
    ESP_LOGE(TAG, "health: SPI failure reading status");
    return RadioHealth::STUCK;
  }

  // ── Read IRQ status for diagnostics ───────────────────────────────────────
  uint8_t irq_buf[2] = {};
  if (!this->read_opcode_(sx1262::GET_IRQ_STATUS, irq_buf, 2)) return RadioHealth::STUCK;
  uint16_t irq_status = (static_cast<uint16_t>(irq_buf[0]) << 8) | irq_buf[1];

  // Single RX returns to standby on completion. A packet can finish between
  // the hub's drain and this check; preserve it for the next drain.
  if (irq_status & (sx1262::IRQ_SYNCWORD_VALID | sx1262::IRQ_RX_DONE)) {
    return RadioHealth::OK;
  }

  // ── Read device errors — SX1262-specific health signal ────────────────────
  // Bits: [0] RC64k cal error, [1] RC13M cal error, [2] PLL cal error,
  //       [3] ADC conversion error, [4] image cal error, [5] XOSC start error,
  //       [6] PLL lock error
  // PLL lock and XOSC start errors indicate the radio can't maintain its
  // frequency reference — it's effectively deaf/mute until recovered.
  uint8_t err_buf[2] = {};
  if (!this->read_opcode_(sx1262::GET_DEVICE_ERRORS, err_buf, 2)) return RadioHealth::STUCK;
  uint16_t dev_errors = (static_cast<uint16_t>(err_buf[0]) << 8) | err_buf[1];
  if (dev_errors != 0) {
    uint8_t clear[2] = {0x00, 0x00};
    if (!this->write_opcode_(sx1262::CLR_DEVICE_ERRORS, clear, 2)) return RadioHealth::STUCK;
  }

  ESP_LOGD(TAG, "health: mode=0x%x irq=0x%04x err=0x%04x", chip_mode, irq_status, dev_errors);

  // Critical device errors → STUCK (needs recovery, not just a warning)
  if (dev_errors & sx1262::ELERO_CRITICAL_ERRORS) {
    ESP_LOGW(TAG, "health: critical device error 0x%04x (PLL/XOSC), needs recovery", dev_errors);
    return RadioHealth::STUCK;
  }

  // chip_mode: 0x02=STBY_RC, 0x03=STBY_XOSC, 0x04=FS, 0x05=RX, 0x06=TX
  if (chip_mode == 0x05) {
    return RadioHealth::OK;  // In RX — expected
  }
  if (chip_mode == 0x06) {
    return RadioHealth::OK;  // In TX — transient
  }

  // Chip is in standby or FS — stuck, needs recovery
  ESP_LOGW(TAG, "health: chip_mode=0x%02x, expected RX", chip_mode);
  return RadioHealth::STUCK;
}

void Sx1262Driver::recover() {
  this->stat_watchdog_recoveries_.fetch_add(1, std::memory_order_relaxed);

  // ── Windowed escalation tracking ──────────────────────────────────────────
  uint32_t now = millis();
  if (now - this->recovery_window_start_ms_ > RECOVERY_WINDOW_MS) {
    this->recovery_window_start_ms_ = now;
    this->recoveries_in_window_ = 0;
    this->resets_in_window_ = 0;
  }
  ++this->recoveries_in_window_;

  // ── Level 1: Soft recovery (standby → clear → RX) ────────────────────────
  ESP_LOGW(TAG, "recover: soft attempt %d in current window", this->recoveries_in_window_);
  (void) this->set_standby_();

  (void) this->clear_irq_status_();
  if (this->rx_ready_) {
    this->rx_ready_->store(false, std::memory_order_release);
  }
  if (this->tx_done_) {
    this->tx_done_->store(false, std::memory_order_release);
  }

  (void) this->restore_rx_packet_params_();
  (void) this->set_dio_irq_for_rx_();
  (void) this->set_rx_();

  // Verify: did we reach RX?
  uint8_t chip_mode = this->read_chip_mode_();
  if (chip_mode == 0x05) {
    return;  // In RX — soft recovery succeeded
  }

  // Soft recovery already failed: reset immediately instead of waiting for more
  // watchdog periods while the radio stays deaf.
  ++this->resets_in_window_;
  ESP_LOGE(TAG, "recover: soft recovery failed, RST reset (%d/%d in window, mode=0x%x)",
           this->resets_in_window_, RESETS_BEFORE_FAILED, chip_mode);
  this->reset();
  if (!this->init()) {
    ESP_LOGE(TAG, "recover: init failed after RST reset");
  }
  this->RadioDriver::mode_.store(RadioMode::RX, std::memory_order_release);

  uint8_t reset_chip_mode = this->read_chip_mode_();
  if (reset_chip_mode == 0x05) {
    return;
  }

  if (this->resets_in_window_ >= RESETS_BEFORE_FAILED) {
    ESP_LOGE(TAG, "recover: unrecoverable after %d resets, marking failed", this->resets_in_window_);
    this->failed_ = true;
    return;
  }

  ESP_LOGW(TAG, "recover: reset completed but radio still not in RX (mode=0x%x)", reset_chip_mode);
}

void Sx1262Driver::set_frequency_regs(uint8_t f2, uint8_t f1, uint8_t f0) {
  this->freq2_ = f2;
  this->freq1_ = f1;
  this->freq0_ = f0;

  if (!this->set_standby_()) {
    this->failed_ = true;
    return;
  }
  if (!this->set_frequency_()) {
    this->failed_ = true;
    return;
  }

  // Re-calibrate image for the (potentially) new frequency
  uint8_t cal_freq[2] = {0xD7, 0xDB};  // 850-900 MHz
  if (!this->write_opcode_(sx1262::CALIBRATE_IMAGE, cal_freq, 2)) {
    this->failed_ = true;
    return;
  }

  if (!this->set_dio_irq_for_rx_()) {
    this->failed_ = true;
    return;
  }
  if (!this->set_rx_()) {
    this->failed_ = true;
    return;
  }
  ESP_LOGI(TAG, "SX1262 re-initialised: freq2=0x%02x freq1=0x%02x freq0=0x%02x", f2, f1, f0);
}

bool Sx1262Driver::tx_prepare_for_fsm() {
  if (this->tx_len_ == 0) {
    ESP_LOGW(TAG, "Prepare: no buffered packet");
    return false;
  }

  uint8_t data_len = this->tx_buf_[0];
  size_t raw_total = static_cast<size_t>(data_len) + 1;
  if (raw_total != this->tx_len_) {
    ESP_LOGW(TAG, "Prepare: buffered len mismatch pkt=%u buf=%u", data_len,
             static_cast<unsigned>(this->tx_len_));
    return false;
  }

  size_t tx_total = raw_total + 2;
  if (tx_total > sizeof(this->tx_buf_)) {
    ESP_LOGW(TAG, "Prepare: tx_total=%u exceeds buffer", static_cast<unsigned>(tx_total));
    return false;
  }

  if (!this->set_standby_(sx1262::STDBY_XOSC)) {
    ESP_LOGW(TAG, "Prepare: failed to enter STDBY_XOSC");
    return false;
  }

  uint16_t crc = cc1101_crc16(this->tx_buf_, raw_total);
  this->tx_buf_[raw_total] = static_cast<uint8_t>(crc >> 8);
  this->tx_buf_[raw_total + 1] = static_cast<uint8_t>(crc & 0xFF);

  ESP_LOGD(TAG, "TX raw [%d]: %s", static_cast<int>(tx_total),
           format_hex_pretty(this->tx_buf_, tx_total).c_str());

  this->apply_pn9_(this->tx_buf_, tx_total);

  if (!this->set_pa_config_()) {
    ESP_LOGW(TAG, "Prepare: SET_PA_CONFIG failed");
    return false;
  }
  if (!this->apply_errata_pa_clamping_()) {
    ESP_LOGW(TAG, "Prepare: PA clamping errata write failed");
    return false;
  }
  uint8_t tx_params[2] = {static_cast<uint8_t>(this->pa_power_), sx1262::PA_RAMP_200US};
  if (!this->write_opcode_(sx1262::SET_TX_PARAMS, tx_params, 2)) {
    ESP_LOGW(TAG, "Prepare: SET_TX_PARAMS failed");
    return false;
  }
  if (!this->apply_errata_sensitivity_()) {
    ESP_LOGW(TAG, "Prepare: sensitivity errata write failed");
    return false;
  }

  uint8_t pkt_params[9] = {
      0x00, 0x60,
      0x00,
      0x20,
      0x00,
      0x00,
      static_cast<uint8_t>(tx_total),
      0x01,
      0x00,
  };
  if (!this->write_opcode_(sx1262::SET_PACKET_PARAMS, pkt_params, 9)) {
    ESP_LOGW(TAG, "Prepare: SET_PACKET_PARAMS failed");
    return false;
  }

  uint8_t buf_addr[2] = {0x00, 0x00};
  if (!this->write_opcode_(sx1262::SET_BUFFER_BASE_ADDRESS, buf_addr, 2)) {
    ESP_LOGW(TAG, "Prepare: SET_BUFFER_BASE_ADDRESS failed");
    return false;
  }

  if (!this->write_fifo_(0x00, this->tx_buf_, tx_total)) {
    ESP_LOGW(TAG, "Prepare: WRITE_BUFFER failed");
    return false;
  }

  if (!this->clear_irq_status_()) {
    ESP_LOGW(TAG, "Prepare: CLR_IRQ_STATUS failed");
    return false;
  }
  if (this->tx_done_) {
    this->tx_done_->store(false, std::memory_order_release);
  }
  if (!this->set_dio_irq_for_tx_()) {
    ESP_LOGW(TAG, "Prepare: SET_DIO_IRQ_PARAMS for TX failed");
    return false;
  }

  if (this->fem_pa_pin_) {
    this->fem_pa_pin_->digital_write(true);
  }

  if (!this->set_tx_()) {
    ESP_LOGW(TAG, "Prepare: SET_TX failed");
    if (this->fem_pa_pin_) {
      this->fem_pa_pin_->digital_write(false);
    }
    return false;
  }
  uint8_t tx_chip_mode = this->read_chip_mode_();
  if (tx_chip_mode != 0xFF && tx_chip_mode != 0x06) {
    ESP_LOGD(TAG, "Prepare: transient post-SET_TX mode=0x%02x", tx_chip_mode);
  }

  ESP_LOGD(TAG, "TX start: mode=0x%x fem=%d", tx_chip_mode, this->fem_pa_pin_ ? 1 : 0);
  this->RadioDriver::mode_.store(RadioMode::TX, std::memory_order_release);
  return true;
}

SemtechTxPhaseResult Sx1262Driver::tx_wait_done_for_fsm() {
  uint8_t irq_buf[2] = {};
  if (!this->read_opcode_(sx1262::GET_IRQ_STATUS, irq_buf, 2)) {
    ESP_LOGE(TAG, "TX poll: SPI failure reading IRQ status");
    return SemtechTxPhaseResult::Failed;
  }

  uint16_t irq_status = (static_cast<uint16_t>(irq_buf[0]) << 8) | irq_buf[1];
  if (irq_status & sx1262::IRQ_TX_DONE) {
    (void) this->write_opcode_(sx1262::CLR_IRQ_STATUS, irq_buf, 2);
    if (this->tx_done_) {
      this->tx_done_->store(false, std::memory_order_release);
    }

    uint32_t elapsed = millis() - this->phase_started_ms_;
    ESP_LOGD(TAG, "TX done irq=0x%04x %ums", irq_status, elapsed);
    return SemtechTxPhaseResult::Succeeded;
  }

  uint32_t elapsed = millis() - this->phase_started_ms_;
  if (elapsed > TX_TIMEOUT_MS) {
    ESP_LOGE(TAG, "TX timeout after %ums (irq=0x%04x)", TX_TIMEOUT_MS, irq_status);
    return SemtechTxPhaseResult::Failed;
  }

  return SemtechTxPhaseResult::Pending;
}

bool Sx1262Driver::tx_return_to_rx_for_fsm() {
  if (this->fem_pa_pin_) {
    this->fem_pa_pin_->digital_write(false);
  }

  if (!this->restore_rx_packet_params_()) {
    ESP_LOGW(TAG, "ReturnToRx: restore RX packet params failed");
    return false;
  }
  if (!this->set_dio_irq_for_rx_()) {
    ESP_LOGW(TAG, "ReturnToRx: SET_DIO_IRQ_PARAMS for RX failed");
    return false;
  }
  if (!this->set_rx_()) {
    ESP_LOGW(TAG, "ReturnToRx: SET_RX failed");
    return false;
  }
  return true;
}

SemtechTxPhaseResult Sx1262Driver::tx_wait_rx_ready_for_fsm() {
  // A received sync already proves RX was entered. A complete single capture
  // may have returned the chip to standby before this TX phase is polled.
  if (this->rx_capture_active_) return SemtechTxPhaseResult::Succeeded;

  uint32_t elapsed = millis() - this->phase_started_ms_;
  if (elapsed < RX_SETTLE_MS) {
    return SemtechTxPhaseResult::Pending;
  }

  uint8_t chip_mode = this->read_chip_mode_();
  if (chip_mode == 0x05) {
    return SemtechTxPhaseResult::Succeeded;
  }
  if (chip_mode == 0xFF) {
    ESP_LOGW(TAG, "WaitRxReady: SPI failure reading chip mode");
    return SemtechTxPhaseResult::Failed;
  }

  uint8_t err_buf[2] = {};
  if (!this->read_opcode_(sx1262::GET_DEVICE_ERRORS, err_buf, 2)) {
    ESP_LOGW(TAG, "WaitRxReady: SPI failure reading device errors");
    return SemtechTxPhaseResult::Failed;
  }

  uint16_t dev_errors = (static_cast<uint16_t>(err_buf[0]) << 8) | err_buf[1];
  if (dev_errors & sx1262::ELERO_CRITICAL_ERRORS) {
    ESP_LOGW(TAG, "WaitRxReady: critical device error 0x%04x", dev_errors);
    return SemtechTxPhaseResult::Failed;
  }
  if (dev_errors != 0) {
    uint8_t clear[2] = {0x00, 0x00};
    (void) this->write_opcode_(sx1262::CLR_DEVICE_ERRORS, clear, 2);
  }

  if (elapsed <= RX_READY_TIMEOUT_MS) {
    ESP_LOGD(TAG, "WaitRxReady: waiting for RX mode=0x%02x after %ums", chip_mode,
             elapsed);
    return SemtechTxPhaseResult::Pending;
  }

  ESP_LOGW(TAG, "WaitRxReady: RX restore timeout mode=0x%02x after %ums", chip_mode,
           elapsed);
  return SemtechTxPhaseResult::Failed;
}

void Sx1262Driver::tx_on_state_enter_for_fsm(SemtechTxState /*state*/, uint32_t now) {
  this->phase_started_ms_ = now;
}

void Sx1262Driver::tx_set_terminal_result_for_fsm(SemtechTxTerminalResult result) {
  this->tx_terminal_result_ = result;
}

void Sx1262Driver::tx_recover_for_fsm() {
  this->stat_tx_recover_.fetch_add(1, std::memory_order_relaxed);

  if (this->fem_pa_pin_) {
    this->fem_pa_pin_->digital_write(false);
  }

  (void) this->set_standby_();
  (void) this->clear_irq_status_();
  if (this->rx_ready_) {
    this->rx_ready_->store(false, std::memory_order_release);
  }
  if (this->tx_done_) {
    this->tx_done_->store(false, std::memory_order_release);
  }

  (void) this->restore_rx_packet_params_();
  (void) this->set_dio_irq_for_rx_();
  (void) this->set_rx_();

  uint8_t chip_mode = this->read_chip_mode_();
  if (chip_mode != 0x05) {
    ESP_LOGW(TAG, "RecoverTx: direct RX restore failed (mode=0x%x), escalating", chip_mode);
    this->recover();
    return;
  }

  this->RadioDriver::mode_.store(RadioMode::RX, std::memory_order_release);
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

bool Sx1262Driver::wait_busy_() {
  if (!this->busy_pin_) {
    delay_microseconds_safe(100);  // Fallback delay if no BUSY pin
    return true;
  }
  uint32_t start = millis();
  while (this->busy_pin_->digital_read()) {
    if (millis() - start > sx1262::BUSY_TIMEOUT_MS) {
      ESP_LOGE(TAG, "BUSY pin timeout (%ums) — chip unresponsive", sx1262::BUSY_TIMEOUT_MS);
      return false;
    }
    delay_microseconds_safe(10);
  }
  return true;
}

bool Sx1262Driver::write_opcode_(uint8_t opcode, const uint8_t *data, size_t len) {
  if (!this->wait_busy_()) return false;
  this->enable();
  this->transfer_byte(opcode);
  for (size_t i = 0; i < len; ++i) {
    this->transfer_byte(data[i]);
  }
  this->disable();
  return true;
}

bool Sx1262Driver::read_opcode_(uint8_t opcode, uint8_t *data, size_t len) {
  if (!this->wait_busy_()) return false;
  this->enable();
  this->transfer_byte(opcode);
  this->transfer_byte(0x00);  // NOP (status byte)
  for (size_t i = 0; i < len; ++i) {
    data[i] = this->transfer_byte(0x00);
  }
  this->disable();
  return true;
}

bool Sx1262Driver::write_register_(uint16_t addr, const uint8_t *data, size_t len) {
  if (!this->wait_busy_()) return false;
  this->enable();
  this->transfer_byte(sx1262::WRITE_REGISTER);
  this->transfer_byte(static_cast<uint8_t>(addr >> 8));
  this->transfer_byte(static_cast<uint8_t>(addr & 0xFF));
  for (size_t i = 0; i < len; ++i) {
    this->transfer_byte(data[i]);
  }
  this->disable();
  return true;
}

bool Sx1262Driver::read_register_(uint16_t addr, uint8_t *data, size_t len) {
  if (!this->wait_busy_()) return false;
  this->enable();
  this->transfer_byte(sx1262::READ_REGISTER);
  this->transfer_byte(static_cast<uint8_t>(addr >> 8));
  this->transfer_byte(static_cast<uint8_t>(addr & 0xFF));
  this->transfer_byte(0x00);  // NOP (status byte)
  for (size_t i = 0; i < len; ++i) {
    data[i] = this->transfer_byte(0x00);
  }
  this->disable();
  return true;
}

bool Sx1262Driver::write_fifo_(uint8_t offset, const uint8_t *data, size_t len) {
  if (!this->wait_busy_()) return false;
  this->enable();
  this->transfer_byte(sx1262::WRITE_BUFFER);
  this->transfer_byte(offset);
  for (size_t i = 0; i < len; ++i) {
    this->transfer_byte(data[i]);
  }
  this->disable();
  return true;
}

bool Sx1262Driver::read_fifo_(uint8_t offset, uint8_t *data, size_t len) {
  if (!this->wait_busy_()) return false;
  this->enable();
  this->transfer_byte(sx1262::READ_BUFFER);
  this->transfer_byte(offset);
  this->transfer_byte(0x00);  // NOP (status byte)
  for (size_t i = 0; i < len; ++i) {
    data[i] = this->transfer_byte(0x00);
  }
  this->disable();
  return true;
}

// ─── Radio Control ────────────────────────────────────────────────────────

uint8_t Sx1262Driver::read_chip_mode_() {
  if (!this->wait_busy_()) return 0xFF;
  this->enable();
  this->transfer_byte(sx1262::GET_STATUS);
  uint8_t status = this->transfer_byte(0x00);
  this->disable();
  return (status >> 4) & 0x07;
}

bool Sx1262Driver::set_standby_(uint8_t mode) {
  return this->write_opcode_(sx1262::SET_STANDBY, &mode, 1);
}

bool Sx1262Driver::set_rx_() {
  this->rx_capture_active_ = false;
  // A new capture must not inherit unread bytes from a previous short frame.
  uint8_t empty[sx1262::RX_FIXED_LEN]{};
  const uint8_t bases[2]{};
  if (!this->write_opcode_(sx1262::SET_BUFFER_BASE_ADDRESS, bases, sizeof(bases)) ||
      !this->write_fifo_(0, empty, sizeof(empty))) return false;
  // Re-apply boosted RX gain — this register resets to power-saving (0x94) on any
  // STDBY transition (SX1262 datasheet §9.6). Without this, sensitivity degrades ~3 dB.
  uint8_t rx_gain = 0x96;
  if (!this->write_register_(sx1262::REG_RX_GAIN, &rx_gain, 1)) {
    return false;
  }

  // Single RX: hold this frame until it is consumed and explicitly rearmed.
  uint8_t timeout[3] = {0, 0, 0};
  if (!this->write_opcode_(sx1262::SET_RX, timeout, 3)) {
    return false;
  }
  this->RadioDriver::mode_.store(RadioMode::RX, std::memory_order_release);
  return true;
}

bool Sx1262Driver::set_tx_() {
  // No timeout (0x000000) — we handle timeout ourselves
  uint8_t timeout[3] = {0x00, 0x00, 0x00};
  return this->write_opcode_(sx1262::SET_TX, timeout, 3);
}

bool Sx1262Driver::configure_fsk_() {
  // NOTE: SetPacketType is already called in init() before this function.

  // ── Modulation parameters ──────────────────────────────────────────────
  uint32_t br = sx1262::ELERO_BITRATE;
  uint32_t fdev = sx1262::ELERO_FDEV;
  uint8_t mod_params[8] = {
      static_cast<uint8_t>((br >> 16) & 0xFF),
      static_cast<uint8_t>((br >> 8) & 0xFF),
      static_cast<uint8_t>(br & 0xFF),
      0x09,                     // Gaussian BT=0.5 (GFSK, matches CC1101 MOD_FORMAT=GFSK)
      sx1262::BW_FSK_156200,    // RX bandwidth 156.2 kHz (just above Carson's 147 kHz, ~1.8 dB better SNR)
      static_cast<uint8_t>((fdev >> 16) & 0xFF),
      static_cast<uint8_t>((fdev >> 8) & 0xFF),
      static_cast<uint8_t>(fdev & 0xFF),
  };
  if (!this->write_opcode_(sx1262::SET_MODULATION_PARAMS, mod_params, 8)) return false;

  // ── fixGFSK: undocumented Semtech register writes (from RadioLib) ─────
  // Required for correct FSK operation at all non-special bitrates.
  {
    uint8_t val;
    if (!this->read_register_(sx1262::REG_GFSK_FIX_1, &val, 1)) return false;
    val = (val & 0xE7) | 0x08;  // bits[4:3] = 0x08
    if (!this->write_register_(sx1262::REG_GFSK_FIX_1, &val, 1)) return false;

    if (!this->read_register_(sx1262::REG_RSSI_AVG_WINDOW, &val, 1)) return false;
    val = val & 0xE3;  // bits[4:2] = 0x00
    if (!this->write_register_(sx1262::REG_RSSI_AVG_WINDOW, &val, 1)) return false;

    if (!this->read_register_(sx1262::REG_GFSK_FIX_3, &val, 1)) return false;
    val = (val & 0xEF) | 0x10;  // bits[4:4] = 0x10
    if (!this->write_register_(sx1262::REG_GFSK_FIX_3, &val, 1)) return false;

    if (!this->read_register_(sx1262::REG_GFSK_FIX_4, &val, 1)) return false;
    val = val & 0x8F;  // bits[6:4] = 0x00
    if (!this->write_register_(sx1262::REG_GFSK_FIX_4, &val, 1)) return false;
  }

  // ── Packet parameters ───────────────────────────────────────────────────
  // Hardware whitening stays OFF to preserve our tested software wire format.
  // CC1101 IBM PN9 de-whitening is applied in software.
  // Bounded 64-byte capture; the wire length locates the two CRC bytes.
  // Hardware CRC OFF: read_fifo verifies CC1101 CRC before payload decryption.
  // Preamble: 96 bits (12 bytes) — matches CC1101 MDMCFG1=0x52 for consistency.
  uint8_t pkt_params[9] = {
      0x00, 0x60,  // Preamble: 96 bits (12 bytes, matches CC1101)
      0x00,        // Preamble detector: OFF (rely on sync word only)
      0x20,        // Sync word: 32 bits (D3 91 D3 91)
      0x00,        // No address filtering
      0x00,        // Fixed length
      sx1262::RX_FIXED_LEN,
      0x01,        // CRC OFF
      0x00,        // Whitening OFF
  };
  if (!this->write_opcode_(sx1262::SET_PACKET_PARAMS, pkt_params, 9)) return false;

  // ── Sync word: 0xD391 ─────────────────────────────────────────────────
  // CC1101 SYNC_MODE=011 (30/32) uses a 32-bit sync word: SYNC1+SYNC0 repeated twice.
  // The CC1101 transmits AND expects D3 91 D3 91. We must match this.
  uint8_t sync_word[8] = {0xD3, 0x91, 0xD3, 0x91, 0x00, 0x00, 0x00, 0x00};
  if (!this->write_register_(sx1262::REG_SYNCWORD, sync_word, 8)) return false;
  return true;
}

bool Sx1262Driver::set_frequency_() {
  uint32_t freq_reg = this->freq_reg_from_cc1101_regs_();
  uint8_t freq_buf[4] = {
      static_cast<uint8_t>((freq_reg >> 24) & 0xFF),
      static_cast<uint8_t>((freq_reg >> 16) & 0xFF),
      static_cast<uint8_t>((freq_reg >> 8) & 0xFF),
      static_cast<uint8_t>(freq_reg & 0xFF),
  };
  if (!this->write_opcode_(sx1262::SET_RF_FREQUENCY, freq_buf, 4)) return false;
  return true;
}

bool Sx1262Driver::set_pa_config_() {
  // SX1262 PA config for +22 dBm max output
  // paDutyCycle=0x04, hpMax=0x07, deviceSel=0x00 (SX1262), paLut=0x01
  uint8_t pa_config[4] = {0x04, 0x07, 0x00, 0x01};
  return this->write_opcode_(sx1262::SET_PA_CONFIG, pa_config, 4);
}

bool Sx1262Driver::set_dio_irq_for_rx_() {
  // Sync starts a bounded capture; DONE is only an early completion hint.
  uint16_t irq_mask = sx1262::IRQ_SYNCWORD_VALID | sx1262::IRQ_RX_DONE | sx1262::IRQ_TIMEOUT;
  uint8_t dio_params[8] = {
      static_cast<uint8_t>(irq_mask >> 8), static_cast<uint8_t>(irq_mask & 0xFF),  // IRQ mask
      static_cast<uint8_t>(irq_mask >> 8), static_cast<uint8_t>(irq_mask & 0xFF),  // DIO1 mask
      0x00, 0x00,  // DIO2 mask (none)
      0x00, 0x00,  // DIO3 mask (none)
  };
  return this->write_opcode_(sx1262::SET_DIO_IRQ_PARAMS, dio_params, 8);
}

bool Sx1262Driver::set_dio_irq_for_tx_() {
  // Enable TX_DONE + TIMEOUT on DIO1
  uint16_t irq_mask = sx1262::IRQ_TX_DONE | sx1262::IRQ_TIMEOUT;
  uint8_t dio_params[8] = {
      static_cast<uint8_t>(irq_mask >> 8), static_cast<uint8_t>(irq_mask & 0xFF),  // IRQ mask
      static_cast<uint8_t>(irq_mask >> 8), static_cast<uint8_t>(irq_mask & 0xFF),  // DIO1 mask
      0x00, 0x00,
      0x00, 0x00,
  };
  return this->write_opcode_(sx1262::SET_DIO_IRQ_PARAMS, dio_params, 8);
}

bool Sx1262Driver::clear_irq_status_() {
  uint8_t clear_all[2] = {0xFF, 0xFF};
  return this->write_opcode_(sx1262::CLR_IRQ_STATUS, clear_all, 2);
}

bool Sx1262Driver::apply_errata_pa_clamping_() {
  // SX1262 errata: PA clamping circuit can reduce output power at >18 dBm.
  // RadioLib applies this in fixPaClamping() — register 0x08D8 bits [4:2].
  // For power > 18 dBm: set bits [4:2] = 0b111 to disable clamping.
  // For power <= 18 dBm: set bits [4:2] = 0b110 (default clamping OK).
  uint8_t clamp_cfg = 0;
  if (!this->read_register_(sx1262::REG_TX_CLAMP_CFG, &clamp_cfg, 1)) {
    return false;
  }
  if (this->pa_power_ > 18) {
    clamp_cfg |= 0x1C;                      // bits [4:2] = 111
  } else {
    clamp_cfg = (clamp_cfg & 0xE3) | 0x18;  // bits [4:2] = 110
  }
  return this->write_register_(sx1262::REG_TX_CLAMP_CFG, &clamp_cfg, 1);
}

bool Sx1262Driver::apply_errata_sensitivity_() {
  // SX1262 errata section 15.1: register 0x0889 bit 2 affects modulation quality.
  // RadioLib sets bit 2 = 1 for all modes except LoRa 500 kHz BW.
  // For GFSK: always set bit 2 to 1 for optimal modulation.
  uint8_t sens_cfg = 0;
  if (!this->read_register_(sx1262::REG_SENSITIVITY_CFG, &sens_cfg, 1)) {
    return false;
  }
  sens_cfg |= 0x04;  // Set bit 2
  return this->write_register_(sx1262::REG_SENSITIVITY_CFG, &sens_cfg, 1);
}

void Sx1262Driver::apply_pn9_(uint8_t *data, size_t len) {
  cc1101_pn9_whiten(data, len);
}

bool Sx1262Driver::restore_rx_packet_params_() {
  // Must match configure_fsk_() exactly: 96-bit preamble, detector OFF, fixed len, CRC/whitening OFF
  uint8_t pkt_params[9] = {
      0x00, 0x60,  // Preamble: 96 bits (12 bytes, matches CC1101)
      0x00,        // Preamble detector: OFF (must match configure_fsk_)
      0x20,        // Sync word: 32 bits (D3 91 D3 91)
      0x00,        // No address filtering
      0x00,        // Fixed length
      sx1262::RX_FIXED_LEN,
      0x01,        // CRC OFF
      0x00,        // Whitening OFF
  };
  return this->write_opcode_(sx1262::SET_PACKET_PARAMS, pkt_params, 9);
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
