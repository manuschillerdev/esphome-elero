#pragma once

/// @file sx1276_driver.h
/// @brief SX1276 radio driver — implements RadioDriver for Semtech SX1276/77/78 868 MHz transceiver.
///
/// Register-based FSK driver matching the Elero protocol parameters (868.35 MHz, 2-FSK, ~76.8 kbaud).
/// Structurally closer to CC1101 than SX1262 (register read/write, not command-based).
/// Uses software CRC-16 and PN9 whitening (hardware implementations are incompatible with CC1101).
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
// SX1276 REGISTER MAP (FSK mode only)
// ═══════════════════════════════════════════════════════════════════════════════

namespace sx1276 {

// ── FIFO ────────────────────────────────────────────────────────────────────
constexpr uint8_t REG_FIFO = 0x00;

// ── Common registers ────────────────────────────────────────────────────────
constexpr uint8_t REG_OP_MODE = 0x01;
constexpr uint8_t REG_BITRATE_MSB = 0x02;
constexpr uint8_t REG_BITRATE_LSB = 0x03;
constexpr uint8_t REG_FDEV_MSB = 0x04;
constexpr uint8_t REG_FDEV_LSB = 0x05;
constexpr uint8_t REG_FRF_MSB = 0x06;
constexpr uint8_t REG_FRF_MID = 0x07;
constexpr uint8_t REG_FRF_LSB = 0x08;

// ── TX registers ────────────────────────────────────────────────────────────
constexpr uint8_t REG_PA_CONFIG = 0x09;
constexpr uint8_t REG_PA_RAMP = 0x0A;
constexpr uint8_t REG_OCP = 0x0B;

// ── RX registers ────────────────────────────────────────────────────────────
constexpr uint8_t REG_LNA = 0x0C;
constexpr uint8_t REG_RX_CONFIG = 0x0D;
constexpr uint8_t REG_RSSI_CONFIG = 0x0E;
constexpr uint8_t REG_RSSI_COLLISION = 0x0F;
constexpr uint8_t REG_RSSI_THRESH = 0x10;
constexpr uint8_t REG_RSSI_VALUE = 0x11;
constexpr uint8_t REG_RX_BW = 0x12;
constexpr uint8_t REG_AFC_BW = 0x13;
constexpr uint8_t REG_OOK_PEAK = 0x14;
constexpr uint8_t REG_AFC_FEI = 0x1A;
constexpr uint8_t REG_AFC_MSB = 0x1B;
constexpr uint8_t REG_AFC_LSB = 0x1C;
constexpr uint8_t REG_FEI_MSB = 0x1D;
constexpr uint8_t REG_FEI_LSB = 0x1E;
constexpr uint8_t REG_PREAMBLE_DETECT = 0x1F;

// ── Timeouts ────────────────────────────────────────────────────────────────
constexpr uint8_t REG_RX_TIMEOUT1 = 0x20;
constexpr uint8_t REG_RX_TIMEOUT2 = 0x21;
constexpr uint8_t REG_RX_TIMEOUT3 = 0x22;

// ── Oscillator ──────────────────────────────────────────────────────────────
constexpr uint8_t REG_OSC = 0x24;

// ── Packet handler ──────────────────────────────────────────────────────────
constexpr uint8_t REG_PREAMBLE_MSB = 0x25;
constexpr uint8_t REG_PREAMBLE_LSB = 0x26;
constexpr uint8_t REG_SYNC_CONFIG = 0x27;
constexpr uint8_t REG_SYNC_VALUE1 = 0x28;
constexpr uint8_t REG_SYNC_VALUE2 = 0x29;
constexpr uint8_t REG_SYNC_VALUE3 = 0x2A;
constexpr uint8_t REG_SYNC_VALUE4 = 0x2B;
constexpr uint8_t REG_SYNC_VALUE5 = 0x2C;
constexpr uint8_t REG_SYNC_VALUE6 = 0x2D;
constexpr uint8_t REG_SYNC_VALUE7 = 0x2E;
constexpr uint8_t REG_SYNC_VALUE8 = 0x2F;
constexpr uint8_t REG_PACKET_CONFIG1 = 0x30;
constexpr uint8_t REG_PACKET_CONFIG2 = 0x31;
constexpr uint8_t REG_PAYLOAD_LENGTH = 0x32;
constexpr uint8_t REG_NODE_ADRS = 0x33;
constexpr uint8_t REG_BROADCAST_ADRS = 0x34;
constexpr uint8_t REG_FIFO_THRESH = 0x35;

// ── Sequencer ───────────────────────────────────────────────────────────────
constexpr uint8_t REG_SEQ_CONFIG1 = 0x36;
constexpr uint8_t REG_SEQ_CONFIG2 = 0x37;
constexpr uint8_t REG_TIMER_RESOL = 0x38;
constexpr uint8_t REG_TIMER1_COEF = 0x39;
constexpr uint8_t REG_TIMER2_COEF = 0x3A;

// ── IRQ flags ───────────────────────────────────────────────────────────────
constexpr uint8_t REG_IRQ_FLAGS1 = 0x3E;
constexpr uint8_t REG_IRQ_FLAGS2 = 0x3F;

// ── DIO mapping ─────────────────────────────────────────────────────────────
constexpr uint8_t REG_DIO_MAPPING1 = 0x40;
constexpr uint8_t REG_DIO_MAPPING2 = 0x41;

// ── Version ─────────────────────────────────────────────────────────────────
constexpr uint8_t REG_VERSION = 0x42;

// ── Additional ──────────────────────────────────────────────────────────────
constexpr uint8_t REG_TCXO = 0x4B;
constexpr uint8_t REG_PA_DAC = 0x4D;
constexpr uint8_t REG_BITRATE_FRAC = 0x5D;

// ── SPI access bits ─────────────────────────────────────────────────────────
constexpr uint8_t SPI_WRITE = 0x80;  ///< OR with register address for write

// ── RegOpMode (0x01) bit fields ─────────────────────────────────────────────
constexpr uint8_t OPMODE_LONG_RANGE = 0x80;   ///< 1=LoRa, 0=FSK/OOK
constexpr uint8_t OPMODE_MOD_FSK = 0x00;      ///< Modulation type: FSK
constexpr uint8_t OPMODE_MOD_OOK = 0x20;      ///< Modulation type: OOK

// Mode bits [2:0]
constexpr uint8_t MODE_SLEEP = 0x00;
constexpr uint8_t MODE_STANDBY = 0x01;
constexpr uint8_t MODE_FS_TX = 0x02;
constexpr uint8_t MODE_TX = 0x03;
constexpr uint8_t MODE_FS_RX = 0x04;
constexpr uint8_t MODE_RX = 0x05;
constexpr uint8_t MODE_MASK = 0x07;

// ── RegIrqFlags1 (0x3E) bits ───────────────────────────────────────────────
constexpr uint8_t IRQ1_MODE_READY = 0x80;
constexpr uint8_t IRQ1_RX_READY = 0x40;
constexpr uint8_t IRQ1_TX_READY = 0x20;
constexpr uint8_t IRQ1_PLL_LOCK = 0x10;
constexpr uint8_t IRQ1_RSSI = 0x08;
constexpr uint8_t IRQ1_TIMEOUT = 0x04;
constexpr uint8_t IRQ1_PREAMBLE_DETECT = 0x02;
constexpr uint8_t IRQ1_SYNC_ADDRESS_MATCH = 0x01;

// ── RegIrqFlags2 (0x3F) bits ───────────────────────────────────────────────
constexpr uint8_t IRQ2_FIFO_FULL = 0x80;
constexpr uint8_t IRQ2_FIFO_EMPTY = 0x40;
constexpr uint8_t IRQ2_FIFO_LEVEL = 0x20;
constexpr uint8_t IRQ2_FIFO_OVERRUN = 0x10;
constexpr uint8_t IRQ2_PACKET_SENT = 0x08;
constexpr uint8_t IRQ2_PAYLOAD_READY = 0x04;
constexpr uint8_t IRQ2_CRC_OK = 0x02;
constexpr uint8_t IRQ2_LOW_BAT = 0x01;

// ── RegPaConfig (0x09) bits ─────────────────────────────────────────────────
constexpr uint8_t PA_SELECT_BOOST = 0x80;  ///< PA_BOOST pin (up to +17/+20 dBm)
constexpr uint8_t PA_SELECT_RFO = 0x00;    ///< RFO pin (up to +14 dBm)

// ── RegPaDac (0x4D) values ──────────────────────────────────────────────────
constexpr uint8_t PA_DAC_DEFAULT = 0x84;   ///< Default PA (max +17 dBm on PA_BOOST)
constexpr uint8_t PA_DAC_BOOST = 0x87;     ///< +20 dBm mode on PA_BOOST

// ── RegDioMapping1 (0x40) — FSK packet mode ────────────────────────────────
// DIO0 [7:6]: 00=PayloadReady(RX)/PacketSent(TX)
// DIO1 [5:4]: 00=FifoLevel, 01=FifoEmpty, 10=FifoFull
// DIO2 [3:2]: 00=FifoFull, 01=RxReady, 10=Timeout, 11=SyncAddress
// DIO3 [1:0]: 00=FifoEmpty

// ── Elero FSK parameters ───────────────────────────────────────────────────
// SX1276 FXOSC = 32 MHz, Fstep = 32e6 / 2^19 = 61.035 Hz

// Bitrate: FXOSC / R_DATA = 32e6 / 76800 = 416.667
// Integer part = 416 = 0x01A0, fractional = 0.667 * 16 ≈ 11 = 0x0B
// Effective: 32e6 / (416 + 11/16) = 32e6 / 416.6875 = 76800.0 bps (exact!)
constexpr uint8_t ELERO_BITRATE_MSB = 0x01;
constexpr uint8_t ELERO_BITRATE_LSB = 0xA0;
constexpr uint8_t ELERO_BITRATE_FRAC = 0x0B;

// Frequency deviation: 34912 Hz / 61.035 Hz = 571.9 ≈ 572 = 0x023C
constexpr uint8_t ELERO_FDEV_MSB = 0x02;
constexpr uint8_t ELERO_FDEV_LSB = 0x3C;

// RX bandwidth: must be >= 2*fdev + bitrate = 2*34912 + 76800 = 146.6 kHz
// Use Mant=20 (01), Exp=2 → 32e6 / (20 * 2^(2+2)) = 32e6 / 320 = 100 kHz SSB = 200 kHz DSB
// This covers the 146.6 kHz requirement with margin.
constexpr uint8_t ELERO_RX_BW = 0x0B;   // Mant=16(00), Exp=3 → 32M/(16*32) = 62.5kHz SSB = 125kHz DSB

// AFC bandwidth — wider than RX for initial acquisition
constexpr uint8_t ELERO_AFC_BW = 0x0A;  // Mant=16(00), Exp=2 → 32M/(16*16) = 125kHz SSB = 250kHz DSB

// FIFO size
constexpr uint8_t FIFO_SIZE = 64;

// Fixed RX length: same as SX1262, covers all Elero packet sizes.
// Elero packets are 28-31 bytes (length byte + 27-30 data + 2 CRC).
constexpr uint8_t RX_FIXED_LEN = 32;

// Chip version
constexpr uint8_t EXPECTED_VERSION = 0x12;

// Timeouts
constexpr uint32_t MODE_SWITCH_TIMEOUT_MS = 10;

}  // namespace sx1276

/// SX1276 radio driver implementation.
///
/// Inherits from spi::SPIDevice for ESPHome SPI bus integration.
/// SPI clock: 8 MHz (SX1276 max is 10 MHz).
/// All SPI operations are encapsulated here — the Elero hub never touches hardware.
class Sx1276Driver : public RadioDriver,
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
  const char *radio_name() const override { return "sx1276"; }
  int rx_sensitivity_dbm() const override { return -117; }
  bool irq_rising_edge() const override { return true; }  // DIO0 goes HIGH on PayloadReady/PacketSent

  // ── SPI setup (called from hub's setup, before init) ───────────────────────

  /// Must be called once from Component::setup() to initialize the SPI bus.
  void setup_spi() { this->spi_setup(); }

  // ── Configuration setters ──────────────────────────────────────────────────

  void set_freq0(uint8_t f) { freq0_ = f; }
  void set_freq1(uint8_t f) { freq1_ = f; }
  void set_freq2(uint8_t f) { freq2_ = f; }

  // ── SX1276-specific pin setters ────────────────────────────────────────────

  void set_rst_pin(InternalGPIOPin *pin) { rst_pin_ = pin; }

  // ── SX1276-specific option setters ─────────────────────────────────────────

  void set_pa_power(int8_t power) { pa_power_ = power; }

  // ── Diagnostics ────────────────────────────────────────────────────────────

  uint32_t overflow_count() const { return stat_fifo_overflows_.load(std::memory_order_relaxed); }
  uint32_t watchdog_count() const { return stat_watchdog_recoveries_.load(std::memory_order_relaxed); }
  uint32_t recover_count() const { return stat_tx_recover_.load(std::memory_order_relaxed); }

 private:
  // ── SPI primitives ─────────────────────────────────────────────────────────

  void write_reg_(uint8_t addr, uint8_t val);
  uint8_t read_reg_(uint8_t addr);
  void write_burst_(uint8_t addr, const uint8_t *data, size_t len);
  void read_burst_(uint8_t addr, uint8_t *data, size_t len);

  // ── Mode control ───────────────────────────────────────────────────────────

  void set_mode_(uint8_t mode);
  void set_mode_and_wait_(uint8_t mode);
  void set_standby_();
  void set_rx_();

  // ── Radio configuration ────────────────────────────────────────────────────

  void configure_fsk_();
  void set_frequency_();
  void set_pa_config_();
  void set_dio_for_rx_();
  void set_dio_for_tx_();
  void flush_fifo_();

  // ── Frequency conversion ───────────────────────────────────────────────────

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

  InternalGPIOPin *rst_pin_{nullptr};

  // ── Options ────────────────────────────────────────────────────────────────

  int8_t pa_power_{17};  ///< PA output power in dBm (default max for PA_BOOST without DAC)

  // ── Health check state ─────────────────────────────────────────────────────

  uint32_t last_radio_check_ms_{0};

  // ── Stats (atomic — incremented on Core 0, read from Core 1) ───────────────

  std::atomic<uint32_t> stat_fifo_overflows_{0};
  std::atomic<uint32_t> stat_watchdog_recoveries_{0};
  std::atomic<uint32_t> stat_tx_recover_{0};
};

}  // namespace elero
}  // namespace esphome
