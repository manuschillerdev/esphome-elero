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
  }
  if (this->fem_enable_pin_) {
    this->fem_enable_pin_->setup();
    this->fem_enable_pin_->digital_write(true);   // Enable FEM chip (CSD=1)
    delay(1);  // Heltec reference: 1ms settle after CSD enable
  }
  if (this->fem_pa_pin_) {
    this->fem_pa_pin_->setup();
    this->fem_pa_pin_->digital_write(false);  // Start in RX/LNA mode (CPS=0)
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
    (void) this->write_register_(sx1262::REG_SYNCWORD, &test_val, 1);
    uint8_t readback = 0;
    (void) this->read_register_(sx1262::REG_SYNCWORD, &readback, 1);
    if (readback != test_val) {
      ESP_LOGE(TAG, "SPI write verification failed: wrote 0x%02x to SYNCWORD, read 0x%02x", test_val, readback);
      ESP_LOGE(TAG, "  Check MOSI wiring (MISO may still work)");
      this->failed_ = true;
      return false;
    }
  }

  // ── Init sequence follows RadioLib's proven order ──────────────────────

  // 1. Standby
  (void) this->set_standby_(sx1262::STDBY_RC);

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
  (void) this->wait_busy_();

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
  (void) this->wait_busy_();

  // 13. PA config + TX params + errata fixes
  this->set_pa_config_();
  this->apply_errata_pa_clamping_();
  uint8_t tx_params[2] = {static_cast<uint8_t>(this->pa_power_), sx1262::PA_RAMP_200US};
  this->write_opcode_(sx1262::SET_TX_PARAMS, tx_params, 2);

  // 14. Sensitivity/modulation register fix (RadioLib fixSensitivity)
  this->apply_errata_sensitivity_();

  // 15. Current limit (OCP)
  uint8_t ocp = 0x38;  // 140mA (RadioLib default for SX1262)
  this->write_register_(sx1262::REG_OCP, &ocp, 1);

  // 16. Boosted RX gain
  uint8_t rx_gain = 0x96;
  this->write_register_(sx1262::REG_RX_GAIN, &rx_gain, 1);

  // 17. Enter RX mode
  this->set_dio_irq_for_rx_();
  this->set_rx_();
  (void) this->wait_busy_();

  // Verify init — chip should be in RX (0x05)
  uint8_t chip_mode = this->read_chip_mode_();
  ESP_LOGI(TAG, "SX1262 init: chip_mode=0x%02x (expect 0x05=RX)", chip_mode);

  if (chip_mode != 0x05) {
    ESP_LOGE(TAG, "Failed to enter RX mode!");
  }

  ESP_LOGI(TAG, "SX1262 initialized, FSK mode, freq_reg=0x%08x",
           this->freq_reg_from_cc1101_regs_());

  // Verify sync word register wasn't corrupted by calibration/init
  {
    uint8_t sw[4] = {};
    this->read_register_(sx1262::REG_SYNCWORD, sw, 4);
    ESP_LOGI(TAG, "Sync word readback: %02x %02x %02x %02x (expect D3 91 D3 91)",
             sw[0], sw[1], sw[2], sw[3]);
  }

  // ── TX diagnostic (DISABLED — TX confirmed working 2026-03-28) ──────────
  // All 5 test variants ran; Test A (hw_sync + sw_pn9) produced crc_ok:true on CC1101.
  // Root cause was 16-bit vs 32-bit sync word (CC1101 SYNC_MODE=011 doubles it).
#if 0  // Enable for TX debugging only
  //
  // Known PN9 first 8 bytes: FF E1 1D 9A ED 85 33 24
  // CC1101 de-whitens by XOR with this sequence after sync word.

  auto tx_test = [this](const char *label, const uint8_t *buf, size_t len,
                         uint8_t sync_bits, uint8_t whiten) {
    ESP_LOGW(TAG, "TX TEST [%s]: %d bytes, sync=%d bits, whiten=%s",
             label, static_cast<int>(len), sync_bits, whiten ? "HW" : "OFF");

    this->set_standby_(sx1262::STDBY_XOSC);
    this->set_pa_config_();
    this->apply_errata_pa_clamping_();
    uint8_t tp[2] = {static_cast<uint8_t>(this->pa_power_), sx1262::PA_RAMP_200US};
    this->write_opcode_(sx1262::SET_TX_PARAMS, tp, 2);
    this->apply_errata_sensitivity_();

    uint8_t pp[9] = {
        0x00, 0x60,  // Preamble: 96 bits
        0x00,        // Preamble detector: OFF
        sync_bits,   // Sync word length (0x10=16 bits, 0x00=none)
        0x00,        // No address filtering
        0x00,        // Fixed length
        static_cast<uint8_t>(len),
        0x01,        // CRC OFF (hardware)
        whiten,      // 0x00=OFF, 0x01=ON
    };
    this->write_opcode_(sx1262::SET_PACKET_PARAMS, pp, 9);

    uint8_t ba[2] = {0x00, 0x00};
    this->write_opcode_(sx1262::SET_BUFFER_BASE_ADDRESS, ba, 2);
    this->write_fifo_(0x00, buf, len);

    this->clear_irq_status_();
    this->set_dio_irq_for_tx_();
    if (this->fem_pa_pin_) this->fem_pa_pin_->digital_write(true);

    uint8_t to[3] = {0x00, 0x00, 0x00};
    this->write_opcode_(sx1262::SET_TX, to, 3);

    uint32_t t0 = millis();
    while (millis() - t0 < 50) {
      if (this->tx_done_ && this->tx_done_->exchange(false)) break;
      delay(1);
    }
    if (this->fem_pa_pin_) this->fem_pa_pin_->digital_write(false);
    this->set_standby_();
    delay(20);  // gap between packets
  };

  // ── Test A: Hardware sync + software PN9 whitening + CRC (our normal TX path) ──
  // This is what load_and_transmit() does. If CC1101 receives → TX works!
  {
    // Build a simple CHECK command packet to 0x413238
    uint8_t raw[32];
    memset(raw, 0, sizeof(raw));
    raw[0] = 0x1D;  // length = 29
    raw[1] = 0xAA;  // counter (arbitrary)
    raw[2] = 0x6A;  // type = COMMAND
    raw[3] = 0x00;  // type2
    raw[4] = 0x0A;  // hop
    raw[5] = 0x01;  // sys
    raw[6] = 0x03;  // channel
    // src = 0x4FCA30
    raw[7] = 0x4F; raw[8] = 0xCA; raw[9] = 0x30;
    raw[10] = 0x4F; raw[11] = 0xCA; raw[12] = 0x30;  // bwd
    raw[13] = 0x4F; raw[14] = 0xCA; raw[15] = 0x30;  // fwd
    raw[16] = 0x01;  // dest count
    // dst = 0x413238
    raw[17] = 0x41; raw[18] = 0x32; raw[19] = 0x38;
    raw[20] = 0x00; raw[21] = 0x04;  // payload 1,2
    // Encrypted CHECK command (counter=0xAA)
    uint16_t code = (0x0000 - (0xAA * 0x708F)) & 0xFFFF;
    raw[22] = (code >> 8) & 0xFF;
    raw[23] = code & 0xFF;
    raw[24] = 0x00;  // CHECK command
    // Bytes 25-29: zeros (parity will be wrong but doesn't matter for this test)

    // Compute CRC-16 over [length + data] (30 bytes)
    uint16_t crc = cc1101_crc16(raw, 30);
    raw[30] = static_cast<uint8_t>(crc >> 8);
    raw[31] = static_cast<uint8_t>(crc & 0xFF);

    // Apply PN9 whitening
    this->apply_pn9_(raw, 32);

    for (int i = 0; i < 3; ++i)
      tx_test("A:hw_sync+sw_pn9", raw, 32, 0x10, 0x00);
  }

  // ── Test B: Hardware sync + NO whitening at all (raw bytes) ──
  // Chosen so CC1101 PN9 de-whitening produces valid length.
  // First byte 0xE2: 0xE2 ^ 0xFF(PN9) = 0x1D (length=29) → valid.
  // If CC1101 sees this with crc_ok:false → sync word detection works!
  {
    uint8_t raw[32];
    memset(raw, 0xAA, sizeof(raw));
    raw[0] = 0xE2;  // de-whitens to 0x1D

    for (int i = 0; i < 3; ++i)
      tx_test("B:hw_sync+raw", raw, 32, 0x10, 0x00);
  }

  // ── Test C: Hardware sync + SX1262 HARDWARE whitening (no software PN9) ──
  // SX1262 applies its own whitening (different from CC1101 PN9).
  // Sync word still added by hardware before whitening → CC1101 should detect sync.
  // Data will be double-garbled (SX1262 whiten → CC1101 PN9 de-whiten) but sync works.
  // First byte 0x1D: SX1262 whitens it → CC1101 PN9 de-whitens → some value.
  // We need the CC1101-side result ≤ 60. Unpredictable, so send 0x1D and hope.
  {
    uint8_t raw[32];
    memset(raw, 0x00, sizeof(raw));
    raw[0] = 0x1D;  // length (SX1262 will whiten this further)

    for (int i = 0; i < 3; ++i)
      tx_test("C:hw_sync+hw_whiten", raw, 32, 0x10, 0x01);
  }

  // ── Test D: Sync word in buffer (sync_length=0) + software PN9 ──
  // Same data as Test A but with D3 91 prepended and no hardware sync.
  {
    uint8_t raw[34];
    raw[0] = 0xD3; raw[1] = 0x91;  // sync word in buffer
    memset(raw + 2, 0, 32);
    raw[2] = 0x1D; raw[3] = 0xAA; raw[4] = 0x6A; raw[5] = 0x00;
    raw[6] = 0x0A; raw[7] = 0x01; raw[8] = 0x03;
    raw[9] = 0x4F; raw[10] = 0xCA; raw[11] = 0x30;
    raw[12] = 0x4F; raw[13] = 0xCA; raw[14] = 0x30;
    raw[15] = 0x4F; raw[16] = 0xCA; raw[17] = 0x30;
    raw[18] = 0x01; raw[19] = 0x41; raw[20] = 0x32; raw[21] = 0x38;
    raw[22] = 0x00; raw[23] = 0x04;
    uint16_t code = (0x0000 - (0xAA * 0x708F)) & 0xFFFF;
    raw[24] = (code >> 8) & 0xFF; raw[25] = code & 0xFF;
    raw[26] = 0x00;
    uint16_t crc = cc1101_crc16(raw + 2, 30);
    raw[32] = static_cast<uint8_t>(crc >> 8);
    raw[33] = static_cast<uint8_t>(crc & 0xFF);
    // Whiten only the data portion (after sync)
    this->apply_pn9_(raw + 2, 32);

    for (int i = 0; i < 3; ++i)
      tx_test("D:buf_sync+sw_pn9", raw, 34, 0x00, 0x00);
  }

  // ── Test E: INVERTED DEVIATION hypothesis ──────────────────────────────
  // If SX1262 maps 1→negative deviation (opposite CC1101), all bits arrive
  // complemented. Fix: complement all TX bytes + use complemented sync word.
  // Sync register: ~D3=2C, ~91=6E. Data: ~whitened(raw+CRC).
  {
    // Set complemented sync word
    uint8_t inv_sync[8] = {0x2C, 0x6E, 0x2C, 0x6E, 0x00, 0x00, 0x00, 0x00};
    this->write_register_(sx1262::REG_SYNCWORD, inv_sync, 8);

    // Build same packet as Test A but complement the whitened output
    uint8_t raw[32];
    memset(raw, 0, sizeof(raw));
    raw[0] = 0x1D; raw[1] = 0xAA; raw[2] = 0x6A; raw[3] = 0x00;
    raw[4] = 0x0A; raw[5] = 0x01; raw[6] = 0x03;
    raw[7] = 0x4F; raw[8] = 0xCA; raw[9] = 0x30;
    raw[10] = 0x4F; raw[11] = 0xCA; raw[12] = 0x30;
    raw[13] = 0x4F; raw[14] = 0xCA; raw[15] = 0x30;
    raw[16] = 0x01; raw[17] = 0x41; raw[18] = 0x32; raw[19] = 0x38;
    raw[20] = 0x00; raw[21] = 0x04;
    uint16_t code = (0x0000 - (0xAA * 0x708F)) & 0xFFFF;
    raw[22] = (code >> 8) & 0xFF; raw[23] = code & 0xFF;
    raw[24] = 0x00;
    uint16_t crc = cc1101_crc16(raw, 30);
    raw[30] = static_cast<uint8_t>(crc >> 8);
    raw[31] = static_cast<uint8_t>(crc & 0xFF);
    this->apply_pn9_(raw, 32);

    // Complement all bytes (inverted deviation fix)
    for (int i = 0; i < 32; ++i) raw[i] = ~raw[i];

    for (int i = 0; i < 3; ++i)
      tx_test("E:INVERTED_DEV", raw, 32, 0x10, 0x00);

    // Restore original sync word
    uint8_t orig_sync[8] = {0xD3, 0x91, 0xD3, 0x91, 0x00, 0x00, 0x00, 0x00};
    this->write_register_(sx1262::REG_SYNCWORD, orig_sync, 8);
  }

  ESP_LOGW(TAG, "TX TEST: all done — check CC1101 for received packets");

  // Restore RX config
  this->restore_rx_packet_params_();
  this->set_dio_irq_for_rx_();
  this->set_rx_();
  this->wait_busy_();
#endif  // TX diagnostic

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
  if (this->tx_in_progress_) {
    return false;
  }

  if (!this->set_standby_(sx1262::STDBY_XOSC)) return false;

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

  // CC1101 CRC-16 over length byte + data bytes (per CC1101 datasheet section 15.2.3)
  uint16_t crc = cc1101_crc16(tx_buf, raw_total);
  tx_buf[raw_total] = static_cast<uint8_t>(crc >> 8);      // CRC MSB first
  tx_buf[raw_total + 1] = static_cast<uint8_t>(crc & 0xFF);

  // Log raw TX bytes before whitening
  ESP_LOGD(TAG, "TX raw [%d]: %s", static_cast<int>(tx_total),
           format_hex_pretty(tx_buf, tx_total).c_str());

  // IBM PN9 whiten everything (length + data + CRC)
  this->apply_pn9_(tx_buf, tx_total);

  // Re-apply PA config + errata fixes before TX (RadioLib does this on every TX)
  this->set_pa_config_();
  this->apply_errata_pa_clamping_();
  uint8_t tx_params[2] = {static_cast<uint8_t>(this->pa_power_), sx1262::PA_RAMP_200US};
  if (!this->write_opcode_(sx1262::SET_TX_PARAMS, tx_params, 2)) return false;
  this->apply_errata_sensitivity_();

  // Hardware sync word (16 bits from register 0x06C0 = D3 91).
  // SX1262 generates: [preamble] [D3 91] [buffer data]
  uint8_t pkt_params[9] = {
      0x00, 0x60,  // Preamble: 96 bits (12 bytes, matches CC1101)
      0x00,        // Preamble detector: OFF
      0x20,        // Sync word: 32 bits (D3 91 D3 91 — CC1101 SYNC_MODE=011 doubles it)
      0x00,        // No address filtering
      0x00,        // Fixed length
      static_cast<uint8_t>(tx_total),
      0x01,        // CRC OFF (we compute CC1101 CRC in software)
      0x00,        // Whitening OFF (IBM PN9 applied in software)
  };
  if (!this->write_opcode_(sx1262::SET_PACKET_PARAMS, pkt_params, 9)) return false;

  // Reset buffer base address (safety — RadioLib does this before every TX)
  uint8_t buf_addr[2] = {0x00, 0x00};
  if (!this->write_opcode_(sx1262::SET_BUFFER_BASE_ADDRESS, buf_addr, 2)) return false;

  if (!this->write_fifo_(0x00, tx_buf, tx_total)) return false;

  // Clear ALL IRQ flags in the SX1262 register AND the atomic flag.
  // Without this, stale IRQ bits (e.g., RX_DONE) can keep DIO1 high,
  // preventing the rising edge that signals TX_DONE. (RadioLib does this.)
  this->clear_irq_status_();
  if (this->tx_done_) {
    this->tx_done_->store(false, std::memory_order_release);
  }

  this->set_dio_irq_for_tx_();

  // Enable external FEM PA before TX
  if (this->fem_pa_pin_) {
    this->fem_pa_pin_->digital_write(true);
  }

  uint8_t timeout[3] = {0x00, 0x00, 0x00};
  if (!this->write_opcode_(sx1262::SET_TX, timeout, 3)) {
    if (this->fem_pa_pin_) this->fem_pa_pin_->digital_write(false);
    return false;
  }

  // Verify chip entered TX mode
  uint8_t tx_chip_mode = this->read_chip_mode_();
  if (tx_chip_mode == 0xFF) {
    if (this->fem_pa_pin_) this->fem_pa_pin_->digital_write(false);
    return false;
  }
  ESP_LOGD(TAG, "TX start: mode=0x%x fem=%d", tx_chip_mode, this->fem_pa_pin_ ? 1 : 0);

  this->tx_in_progress_ = true;
  this->tx_start_ms_ = millis();
  this->tx_pending_success_ = false;
  this->RadioDriver::mode_ = RadioMode::TX;

  return true;
}

TxPollResult Sx1262Driver::poll_tx() {
  if (!this->tx_in_progress_) {
    return this->tx_pending_success_ ? TxPollResult::SUCCESS : TxPollResult::FAILED;
  }

  // ── Primary: read the hardware IRQ register (authoritative) ───────────────
  // The ISR flag is a fast wake signal, but GetIrqStatus() is the source of truth.
  // This leverages the SX1262's distinct TX_DONE/RX_DONE bits — no mode-based
  // routing ambiguity like CC1101's shared GDO0 pin.
  uint8_t irq_buf[2] = {};
  if (!this->read_opcode_(sx1262::GET_IRQ_STATUS, irq_buf, 2)) {
    // SPI failure during TX — abort
    ESP_LOGE(TAG, "TX poll: SPI failure reading IRQ status");
    this->tx_in_progress_ = false;
    this->tx_pending_success_ = false;
    this->stat_tx_recover_.fetch_add(1, std::memory_order_relaxed);
    if (this->fem_pa_pin_) this->fem_pa_pin_->digital_write(false);
    return TxPollResult::FAILED;
  }
  uint16_t irq_status = (static_cast<uint16_t>(irq_buf[0]) << 8) | irq_buf[1];

  if (irq_status & sx1262::IRQ_TX_DONE) {
    // Clear all hardware IRQ flags + atomic flag
    (void) this->write_opcode_(sx1262::CLR_IRQ_STATUS, irq_buf, 2);
    if (this->tx_done_) this->tx_done_->store(false, std::memory_order_release);

    uint32_t elapsed = millis() - this->tx_start_ms_;
    ESP_LOGD(TAG, "TX done irq=0x%04x %ums", irq_status, elapsed);
    this->tx_in_progress_ = false;
    this->tx_pending_success_ = true;

    // Disable external FEM PA after TX
    if (this->fem_pa_pin_) {
      this->fem_pa_pin_->digital_write(false);
    }

    // Restore RX packet params (TX changed payload length) and re-enter RX
    this->restore_rx_packet_params_();
    this->set_dio_irq_for_rx_();
    this->set_rx_();

    // ── Post-TX RX verification (SX1262-specific) ────────────────────────
    // Unlike CC1101's MCSM1 auto-return-to-RX, the SX1262 falls back to
    // STDBY_RC after TX (SET_RX_TX_FALLBACK_MODE=0x20). We explicitly call
    // set_rx_() above, but verify it actually worked — if the chip is stuck
    // in STDBY, we'd be deaf until the 5s watchdog fires.
    uint8_t chip_mode = this->read_chip_mode_();
    if (chip_mode != 0x05 && chip_mode != 0xFF) {
      ESP_LOGW(TAG, "TX done but chip not in RX (mode=0x%x), recovering", chip_mode);
      this->recover();
    }

    return TxPollResult::SUCCESS;
  }

  // Timeout check
  if (millis() - this->tx_start_ms_ > TX_TIMEOUT_MS) {
    ESP_LOGE(TAG, "TX timeout after %ums (irq=0x%04x)", TX_TIMEOUT_MS, irq_status);
    this->tx_in_progress_ = false;
    this->tx_pending_success_ = false;
    this->stat_tx_recover_.fetch_add(1, std::memory_order_relaxed);

    // Disable external FEM PA on TX failure
    if (this->fem_pa_pin_) {
      this->fem_pa_pin_->digital_write(false);
    }

    // Recover via the escalating path (not just a simple standby→RX)
    this->recover();
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
  (void) this->set_standby_();
  this->set_dio_irq_for_rx_();
  this->set_rx_();
}

bool Sx1262Driver::has_data() {
  if (this->RadioDriver::mode_ != RadioMode::RX) return false;
  return this->rx_ready_ != nullptr && this->rx_ready_->load(std::memory_order_acquire);
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

  // With 32-bit sync word (D3 91 D3 91), the SX1262 strips the full sync word.
  // Buffer starts directly with the whitened payload (no sync prefix to skip).
  // Previous 16-bit sync caused the second D3 91 copy to appear in the buffer.
  uint8_t *data = raw;
  size_t data_len = payload_len;

  // Apply CC1101 IBM PN9 de-whitening in software.
  this->apply_pn9_(data, data_len);

  // First de-whitened byte = Elero packet length.
  uint8_t pkt_len = data[0];

  if (pkt_len < 0x1B || pkt_len > 0x1E) {
    ESP_LOGD(TAG, "bad length 0x%02x after de-whiten (raw[0]=0x%02x)", data[0], raw[0]);
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

  // ── Read chip status ──────────────────────────────────────────────────────
  uint8_t chip_mode = this->read_chip_mode_();
  if (chip_mode == 0xFF) {
    ESP_LOGE(TAG, "health: SPI failure reading status");
    return RadioHealth::STUCK;
  }

  // ── Read IRQ status for diagnostics ───────────────────────────────────────
  uint8_t irq_buf[2] = {};
  (void) this->read_opcode_(sx1262::GET_IRQ_STATUS, irq_buf, 2);
  uint16_t irq_status = (static_cast<uint16_t>(irq_buf[0]) << 8) | irq_buf[1];

  // ── Read device errors — SX1262-specific health signal ────────────────────
  // Bits: [0] RC64k cal error, [1] RC13M cal error, [2] PLL lock error,
  //       [3] ADC conversion error, [4] image cal error, [5] XOSC start error,
  //       [6] PLL cal error
  // PLL lock and XOSC start errors indicate the radio can't maintain its
  // frequency reference — it's effectively deaf/mute until recovered.
  uint8_t err_buf[2] = {};
  (void) this->read_opcode_(sx1262::GET_DEVICE_ERRORS, err_buf, 2);
  uint16_t dev_errors = (static_cast<uint16_t>(err_buf[0]) << 8) | err_buf[1];
  if (dev_errors != 0) {
    uint8_t clear[2] = {0x00, 0x00};
    (void) this->write_opcode_(sx1262::CLR_DEVICE_ERRORS, clear, 2);
  }

  ESP_LOGD(TAG, "health: mode=0x%x irq=0x%04x err=0x%04x", chip_mode, irq_status, dev_errors);

  // Critical device errors → STUCK (needs recovery, not just a warning)
  constexpr uint16_t CRITICAL_ERRORS = 0x0024;  // PLL lock (bit 2) | XOSC start (bit 5)
  if (dev_errors & CRITICAL_ERRORS) {
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
  ESP_LOGW(TAG, "recover: soft (%d/%d in window)", this->recoveries_in_window_, RECOVERIES_BEFORE_RESET);
  (void) this->set_standby_();

  this->clear_irq_status_();
  if (this->rx_ready_) {
    this->rx_ready_->store(false, std::memory_order_release);
  }
  if (this->tx_done_) {
    this->tx_done_->store(false, std::memory_order_release);
  }

  this->restore_rx_packet_params_();
  this->set_dio_irq_for_rx_();
  this->set_rx_();

  // Verify: did we reach RX?
  uint8_t chip_mode = this->read_chip_mode_();
  if (chip_mode == 0x05) {
    return;  // In RX — soft recovery succeeded
  }

  // ── Level 2: Hardware RST pin reset + full re-init ────────────────────────
  if (this->recoveries_in_window_ >= RECOVERIES_BEFORE_RESET) {
    ++this->resets_in_window_;
    ESP_LOGE(TAG, "recover: RST reset (%d/%d in window, mode=0x%x)",
             this->resets_in_window_, RESETS_BEFORE_FAILED, chip_mode);
    this->reset();
    this->init();  // init() ends in set_rx_() which sets mode_ = RX
    this->RadioDriver::mode_ = RadioMode::RX;  // explicit — don't depend on init() internals

    // ── Level 3: Mark failed — radio is unrecoverable ─────────────────────
    if (this->resets_in_window_ >= RESETS_BEFORE_FAILED) {
      ESP_LOGE(TAG, "recover: unrecoverable after %d resets, marking failed", this->resets_in_window_);
      this->failed_ = true;
    }
  }
}

void Sx1262Driver::set_frequency_regs(uint8_t f2, uint8_t f1, uint8_t f0) {
  this->freq2_ = f2;
  this->freq1_ = f1;
  this->freq0_ = f0;

  (void) this->set_standby_();
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
  uint8_t status = this->transfer_byte(sx1262::GET_STATUS);
  this->transfer_byte(0x00);
  this->disable();
  return (status >> 4) & 0x07;
}

bool Sx1262Driver::set_standby_(uint8_t mode) {
  return this->write_opcode_(sx1262::SET_STANDBY, &mode, 1);
}

void Sx1262Driver::set_rx_() {
  // Re-apply boosted RX gain — this register resets to power-saving (0x94) on any
  // STDBY transition (SX1262 datasheet §9.6). Without this, sensitivity degrades ~3 dB.
  uint8_t rx_gain = 0x96;
  this->write_register_(sx1262::REG_RX_GAIN, &rx_gain, 1);

  // Continuous RX (timeout = 0xFFFFFF)
  uint8_t timeout[3] = {0xFF, 0xFF, 0xFF};
  this->write_opcode_(sx1262::SET_RX, timeout, 3);
  this->RadioDriver::mode_ = RadioMode::RX;
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
      sx1262::BW_FSK_156200,    // RX bandwidth 156.2 kHz (just above Carson's 147 kHz, ~1.8 dB better SNR)
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
  this->write_opcode_(sx1262::SET_PACKET_PARAMS, pkt_params, 9);

  // ── Sync word: 0xD391 ─────────────────────────────────────────────────
  // CC1101 SYNC_MODE=011 (30/32) uses a 32-bit sync word: SYNC1+SYNC0 repeated twice.
  // The CC1101 transmits AND expects D3 91 D3 91. We must match this.
  // (This also explains the 2-byte "sync prefix" in RX: the SX1262 16-bit sync detector
  // matches the FIRST D3 91, capturing the SECOND copy as data in the buffer.)
  uint8_t sync_word[8] = {0xD3, 0x91, 0xD3, 0x91, 0x00, 0x00, 0x00, 0x00};
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

void Sx1262Driver::clear_irq_status_() {
  uint8_t clear_all[2] = {0xFF, 0xFF};
  this->write_opcode_(sx1262::CLR_IRQ_STATUS, clear_all, 2);
}

void Sx1262Driver::apply_errata_pa_clamping_() {
  // SX1262 errata: PA clamping circuit can reduce output power at >18 dBm.
  // RadioLib applies this in fixPaClamping() — register 0x08D8 bits [4:2].
  // For power > 18 dBm: set bits [4:2] = 0b111 to disable clamping.
  // For power <= 18 dBm: set bits [4:2] = 0b110 (default clamping OK).
  uint8_t clamp_cfg = 0;
  this->read_register_(sx1262::REG_TX_CLAMP_CFG, &clamp_cfg, 1);
  if (this->pa_power_ > 18) {
    clamp_cfg |= 0x1C;                      // bits [4:2] = 111
  } else {
    clamp_cfg = (clamp_cfg & 0xE3) | 0x18;  // bits [4:2] = 110
  }
  this->write_register_(sx1262::REG_TX_CLAMP_CFG, &clamp_cfg, 1);
}

void Sx1262Driver::apply_errata_sensitivity_() {
  // SX1262 errata section 15.1: register 0x0889 bit 2 affects modulation quality.
  // RadioLib sets bit 2 = 1 for all modes except LoRa 500 kHz BW.
  // For GFSK: always set bit 2 to 1 for optimal modulation.
  uint8_t sens_cfg = 0;
  this->read_register_(sx1262::REG_SENSITIVITY_CFG, &sens_cfg, 1);
  sens_cfg |= 0x04;  // Set bit 2
  this->write_register_(sx1262::REG_SENSITIVITY_CFG, &sens_cfg, 1);
}

void Sx1262Driver::apply_pn9_(uint8_t *data, size_t len) {
  cc1101_pn9_whiten(data, len);
}

void Sx1262Driver::restore_rx_packet_params_() {
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
