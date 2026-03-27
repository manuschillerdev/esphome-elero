#pragma once

/// @file sx1262_driver.h
/// @brief SX1262 radio driver — implements RadioDriver for Semtech SX1262 868 MHz transceiver.
///
/// Implements the same RadioDriver interface as CC1101Driver, configured for FSK mode
/// matching the Elero protocol parameters (868.35 MHz, 2-FSK, ~9.6 kbaud).
/// All methods are called from the RF task (Core 0) only, except init() which
/// is called once from setup() (Core 1) before the RF task starts.

#include "radio_driver.h"
#include "elero_packet.h"
#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/hal.h"
#include <atomic>
#include <cstdint>

namespace esphome {
namespace elero {

// ═══════════════════════════════════════════════════════════════════════════════
// SX1262 OPCODES
// ═══════════════════════════════════════════════════════════════════════════════

namespace sx1262 {

// ── Operational commands ─────────────────────────────────────────────────────
constexpr uint8_t SET_SLEEP = 0x84;
constexpr uint8_t SET_STANDBY = 0x80;
constexpr uint8_t SET_TX = 0x83;
constexpr uint8_t SET_RX = 0x82;
constexpr uint8_t SET_FS = 0xC1;
constexpr uint8_t CALIBRATE = 0x89;
constexpr uint8_t CALIBRATE_IMAGE = 0x98;

// ── Register/buffer commands ─────────────────────────────────────────────────
constexpr uint8_t WRITE_REGISTER = 0x0D;
constexpr uint8_t READ_REGISTER = 0x1D;
constexpr uint8_t WRITE_BUFFER = 0x0E;
constexpr uint8_t READ_BUFFER = 0x1E;

// ── Configuration commands ───────────────────────────────────────────────────
constexpr uint8_t SET_PACKET_TYPE = 0x8A;
constexpr uint8_t SET_RF_FREQUENCY = 0x86;
constexpr uint8_t SET_TX_PARAMS = 0x8E;
constexpr uint8_t SET_PA_CONFIG = 0x95;
constexpr uint8_t SET_BUFFER_BASE_ADDRESS = 0x8F;
constexpr uint8_t SET_MODULATION_PARAMS = 0x8B;
constexpr uint8_t SET_PACKET_PARAMS = 0x8C;
constexpr uint8_t SET_DIO_IRQ_PARAMS = 0x08;
constexpr uint8_t SET_DIO2_AS_RF_SWITCH = 0x9D;
constexpr uint8_t SET_DIO3_AS_TCXO_CTRL = 0x97;
constexpr uint8_t SET_REGULATOR_MODE = 0x96;
constexpr uint8_t SET_RX_TX_FALLBACK_MODE = 0x93;

// ── Status commands ──────────────────────────────────────────────────────────
constexpr uint8_t GET_STATUS = 0xC0;
constexpr uint8_t GET_IRQ_STATUS = 0x12;
constexpr uint8_t CLR_IRQ_STATUS = 0x02;
constexpr uint8_t GET_RX_BUFFER_STATUS = 0x13;
constexpr uint8_t GET_PACKET_STATUS = 0x14;
constexpr uint8_t GET_RSSI_INST = 0x15;
constexpr uint8_t GET_DEVICE_ERRORS = 0x17;
constexpr uint8_t CLR_DEVICE_ERRORS = 0x07;

// ── IRQ flags ────────────────────────────────────────────────────────────────
constexpr uint16_t IRQ_TX_DONE = 0x0001;
constexpr uint16_t IRQ_RX_DONE = 0x0002;
constexpr uint16_t IRQ_PREAMBLE_DETECTED = 0x0004;
constexpr uint16_t IRQ_SYNCWORD_VALID = 0x0008;
constexpr uint16_t IRQ_CRC_ERROR = 0x0040;
constexpr uint16_t IRQ_TIMEOUT = 0x0200;

// ── Packet type ──────────────────────────────────────────────────────────────
constexpr uint8_t PACKET_TYPE_GFSK = 0x00;

// ── Standby modes ────────────────────────────────────────────────────────────
constexpr uint8_t STDBY_RC = 0x00;
constexpr uint8_t STDBY_XOSC = 0x01;

// ── Register addresses ───────────────────────────────────────────────────────
constexpr uint16_t REG_SYNCWORD = 0x06C0;
constexpr uint16_t REG_WHITENING_INIT_MSB = 0x06B8;
constexpr uint16_t REG_WHITENING_INIT_LSB = 0x06B9;
constexpr uint16_t REG_CRC_INIT = 0x06BC;       // 2 bytes: MSB at 0x06BC, LSB at 0x06BD
constexpr uint16_t REG_CRC_POLYNOMIAL = 0x06BE;  // 2 bytes: MSB at 0x06BE, LSB at 0x06BF
constexpr uint16_t REG_RX_GAIN = 0x08AC;
constexpr uint16_t REG_OCP = 0x08E7;

// Undocumented registers from Semtech reference / RadioLib fixGFSK()
constexpr uint16_t REG_GFSK_FIX_1 = 0x06D1;
constexpr uint16_t REG_GFSK_FIX_3 = 0x08B8;
constexpr uint16_t REG_GFSK_FIX_4 = 0x06AC;
constexpr uint16_t REG_RSSI_AVG_WINDOW = 0x089B;

// Errata registers (SX1261/62 datasheet chapter 15)
constexpr uint16_t REG_TX_CLAMP_CFG = 0x08D8;       // PA clamping config (section 15.2)
constexpr uint16_t REG_SENSITIVITY_CFG = 0x0889;     // TX modulation quality (section 15.1)

// ── Elero FSK parameters ────────────────────────────────────────────────────
// CC1101 MDMCFG4=0x7B (DRATE_E=11), MDMCFG3=0x83 (DRATE_M=131):
// R_DATA = (256+131) * 2^11 * 26e6 / 2^28 = 76766.45 baud
// SX1262: BR = 32 * F_XTAL / R_DATA = 32 * 32e6 / 76766 ≈ 13337
constexpr uint32_t ELERO_BITRATE = 13337;  // 0x003419

// Frequency deviation: (deviation_hz << 25) / F_XTAL
// CC1101 DEVIATN=0x43: E=4, M=3 → (8+3)*2^4*26e6/2^17 = 34,912 Hz
// SX1262: (34912 * 2^25) / 32000000 ≈ 36602 = 0x008EEA
constexpr uint32_t ELERO_FDEV = 36602;  // ~34.9 kHz

// RX bandwidth: must be >= 2*deviation + bitrate = 2*34912 + 76766 = 146.6 kHz
// CC1101 uses 232 kHz. SX1262: use 234.3 kHz to match
constexpr uint8_t BW_FSK_234300 = 0x0A;

// Frequency: (freq_hz << 25) / F_XTAL
// 868.35 MHz: (868350000 * 2^25) / 32000000 = 910163149 = 0x364633CD
constexpr uint32_t FREQ_868_35 = 0x364633CDU;
// 868.95 MHz: recalculated for alternate variant
constexpr uint32_t FREQ_868_95 = 0x3648A666U;

// SX1262 buffer size (shared 256-byte buffer, but Elero packets are max ~30 bytes)
constexpr uint8_t MAX_PACKET_SIZE = 64;  // Match CC1101_FIFO_LENGTH for compatibility

// Fixed RX length: Elero packets are 28-31 bytes (length byte + 27-30 data).
// With 32-bit sync (D3 91 D3 91), the SX1262 strips the full sync word.
// Buffer starts with the whitened payload. Use 32 to cover all packet sizes
// (max: length 0x1E = 30 data + 1 length byte = 31, plus margin).
constexpr uint8_t RX_FIXED_LEN = 32;

// BUSY pin timeout
constexpr uint32_t BUSY_TIMEOUT_MS = 20;

// PA ramp time
constexpr uint8_t PA_RAMP_200US = 0x04;

}  // namespace sx1262

/// SX1262 radio driver implementation.
///
/// Inherits from spi::SPIDevice for ESPHome SPI bus integration.
/// All SPI operations are encapsulated here — the Elero hub never touches hardware.
class Sx1262Driver : public RadioDriver,
                     public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                           spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_8MHZ> {
 public:
  // ── RadioDriver interface ──────────────────────────────────────────────────

  bool init() override;
  void reset() override;

  bool load_and_transmit(const uint8_t *pkt_buf, size_t len) override;
  TxPollResult poll_tx() override;
  void abort_tx() override;

  bool has_data() override;
  size_t read_fifo(uint8_t *buf, size_t max_len) override;

  RadioHealth check_health() override;
  void recover() override;

  void set_frequency_regs(uint8_t f2, uint8_t f1, uint8_t f0) override;
  void dump_config() override;
  const char *radio_name() const override { return "sx1262"; }
  int rx_sensitivity_dbm() const override { return -117; }
  bool irq_rising_edge() const override { return true; }

  // ── SPI setup (called from hub's setup, before init) ───────────────────────

  /// Must be called once from Component::setup() to initialize the SPI bus.
  void setup_spi() { this->spi_setup(); }

  // ── Configuration setters ──────────────────────────────────────────────────

  void set_freq0(uint8_t f) { freq0_ = f; }
  void set_freq1(uint8_t f) { freq1_ = f; }
  void set_freq2(uint8_t f) { freq2_ = f; }

  // ── SX1262-specific pin setters ────────────────────────────────────────────

  void set_busy_pin(InternalGPIOPin *pin) { busy_pin_ = pin; }
  void set_rst_pin(InternalGPIOPin *pin) { rst_pin_ = pin; }
  void set_fem_pa_pin(InternalGPIOPin *pin) { fem_pa_pin_ = pin; }

  // ── SX1262-specific option setters ─────────────────────────────────────────

  void set_rf_switch(bool enable) { rf_switch_ = enable; }
  void set_pa_power(int8_t power) { pa_power_ = power; }
  void set_tcxo_voltage(float voltage) { tcxo_voltage_ = voltage; }

  // ── Diagnostics ────────────────────────────────────────────────────────────

  uint32_t overflow_count() const { return 0; }  // SX1262 has no FIFO overflow
  uint32_t watchdog_count() const { return stat_watchdog_recoveries_.load(std::memory_order_relaxed); }
  uint32_t recover_count() const { return stat_tx_recover_.load(std::memory_order_relaxed); }

 private:
  // ── SPI primitives ─────────────────────────────────────────────────────────

  void wait_busy_();
  void write_opcode_(uint8_t opcode, const uint8_t *data, size_t len);
  void read_opcode_(uint8_t opcode, uint8_t *data, size_t len);
  void write_register_(uint16_t addr, const uint8_t *data, size_t len);
  void read_register_(uint16_t addr, uint8_t *data, size_t len);
  void write_fifo_(uint8_t offset, const uint8_t *data, size_t len);
  void read_fifo_(uint8_t offset, uint8_t *data, size_t len);

  // ── Radio control ──────────────────────────────────────────────────────────

  void set_standby_(uint8_t mode = sx1262::STDBY_RC);
  void set_rx_();
  void set_tx_();
  void configure_fsk_();
  void set_frequency_();
  void set_pa_config_();
  void set_dio_irq_for_rx_();
  void set_dio_irq_for_tx_();
  void clear_irq_status_();
  void restore_rx_packet_params_();
  void apply_errata_pa_clamping_();
  void apply_errata_sensitivity_();
  void apply_pn9_(uint8_t *data, size_t len);
  uint32_t freq_reg_from_cc1101_regs_() const;

  // ── TX state ───────────────────────────────────────────────────────────────

  bool tx_in_progress_{false};
  uint32_t tx_start_ms_{0};
  bool tx_pending_success_{false};

  static constexpr uint32_t TX_TIMEOUT_MS = 50;

  // ── Frequency registers (CC1101 format for compatibility) ──────────────────

  uint8_t freq0_{0x7A};  // defaults::FREQ0
  uint8_t freq1_{0x71};  // defaults::FREQ1
  uint8_t freq2_{0x21};  // defaults::FREQ2

  // ── Pins ───────────────────────────────────────────────────────────────────

  InternalGPIOPin *busy_pin_{nullptr};
  InternalGPIOPin *rst_pin_{nullptr};
  InternalGPIOPin *fem_pa_pin_{nullptr};  ///< FEM PA enable pin (Heltec V4: GPIO46)

  // ── Options ────────────────────────────────────────────────────────────────

  bool rf_switch_{false};   ///< Use DIO2 as RF switch control
  int8_t pa_power_{22};     ///< PA output power in dBm (default max for SX1262)
  float tcxo_voltage_{0.0f}; ///< TCXO voltage (0 = no TCXO, use crystal). Heltec boards: 1.8V

  // ── Health check state ─────────────────────────────────────────────────────

  uint32_t last_radio_check_ms_{0};

  // ── Stats (atomic — incremented on Core 0, read from Core 1) ───────────────

  std::atomic<uint32_t> stat_watchdog_recoveries_{0};
  std::atomic<uint32_t> stat_tx_recover_{0};
};

}  // namespace elero
}  // namespace esphome
