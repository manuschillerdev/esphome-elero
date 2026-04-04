#pragma once

/// @file cc1101_driver.h
/// @brief CC1101 radio driver — implements RadioDriver for TI CC1101 868 MHz transceiver.
///
/// Extracted from Elero class. Owns all SPI communication with the CC1101.
/// All methods are called from the RF task (Core 0) only, except init() which
/// is called once from setup() (Core 1) before the RF task starts.

#include "radio_driver.h"
#include "cc1101.h"
#include "elero_packet.h"
#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include <atomic>
#include <cstdint>

namespace esphome {
namespace elero {

// Forward declaration
class CC1101Driver;

/// RAII guard for SPI transactions. Calls enable() on construction and
/// disable() on destruction, ensuring CS is always released even on early return.
class SpiTransaction {
 public:
  explicit SpiTransaction(CC1101Driver *driver);
  ~SpiTransaction();
  SpiTransaction(const SpiTransaction &) = delete;
  SpiTransaction &operator=(const SpiTransaction &) = delete;

 private:
  CC1101Driver *driver_;
};

/// TX state machine states (internal to driver).
enum class TxState : uint8_t {
  IDLE,     ///< Radio in RX, waiting for TX request
  PREPARE,  ///< SIDLE -> SFTX -> LOAD_FIFO -> STX -> poll TX (synchronous, ~1ms)
  WAIT_TX,  ///< Wait for GDO0 or MARCSTATE==IDLE fallback (50ms timeout)
  RECOVER,  ///< Flush -> check -> reset+init if stuck -> verify radio alive
};

struct TxContext {
  TxState state{TxState::IDLE};
  uint32_t state_enter_time{0};

  static constexpr uint32_t STATE_TIMEOUT_MS = 50;
};

/// CC1101 radio driver implementation.
///
/// Inherits from spi::SPIDevice for ESPHome SPI bus integration.
/// All SPI operations are encapsulated here — the Elero hub never touches hardware.
class CC1101Driver : public RadioDriver,
                     public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                           spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_2MHZ> {
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
  const char *radio_name() const override { return "cc1101"; }
  int rx_sensitivity_dbm() const override { return -104; }

  // ── SPI setup (called from hub's setup, before init) ───────────────────────

  /// Must be called once from Component::setup() to initialize the SPI bus.
  void setup_spi() { this->spi_setup(); }

  // ── Configuration setters ──────────────────────────────────────────────────

  void set_freq0(uint8_t f) { freq0_ = f; }
  void set_freq1(uint8_t f) { freq1_ = f; }
  void set_freq2(uint8_t f) { freq2_ = f; }

  // ── CC1101-specific diagnostics ────────────────────────────────────────────

  uint32_t overflow_count() const { return stat_fifo_overflows_.load(std::memory_order_relaxed); }
  uint32_t watchdog_count() const { return stat_watchdog_recoveries_.load(std::memory_order_relaxed); }
  uint32_t recover_count() const { return stat_tx_recover_.load(std::memory_order_relaxed); }

 private:
  friend class SpiTransaction;  // Needs access to enable()/disable()

  // ── SPI primitives ─────────────────────────────────────────────────────────

  [[nodiscard]] bool write_reg(uint8_t addr, uint8_t data);
  [[nodiscard]] bool write_burst(uint8_t addr, uint8_t *data, uint8_t len);
  [[nodiscard]] bool write_cmd(uint8_t cmd);
  [[nodiscard]] bool wait_rx();
  [[nodiscard]] uint8_t read_reg(uint8_t addr, bool *ok = nullptr);
  [[nodiscard]] uint8_t read_status(uint8_t addr);
  uint8_t read_status_reliable_(uint8_t addr);
  void read_buf(uint8_t addr, uint8_t *buf, uint8_t len);

  // ── Boot diagnostics ────────────────────────────────────────────────────────

  void diagnose_spi_failure_(uint8_t partnum, uint8_t version);
  bool verify_spi_write_();

  // ── Radio control ──────────────────────────────────────────────────────────

  void flush_and_rx();
  void finalize_tx_success_();
  void init_registers();
  void handle_tx_state_(uint32_t now);
  void recover_radio_();
  void check_radio_health_();

  // ── TX state machine ───────────────────────────────────────────────────────

  TxContext tx_ctx_;
  bool tx_pending_success_{false};
  uint8_t tx_buf_[CC1101_FIFO_LENGTH];  ///< Copy of packet for TX
  size_t tx_len_{0};                     ///< Length of data in tx_buf_

  // ── Frequency registers ────────────────────────────────────────────────────

  uint8_t freq0_{defaults::FREQ0};
  uint8_t freq1_{defaults::FREQ1};
  uint8_t freq2_{defaults::FREQ2};

  // ── Health check state ─────────────────────────────────────────────────────

  uint32_t last_radio_check_ms_{0};

  // ── Stats (atomic — incremented on Core 0, read from Core 1) ───────────────

  std::atomic<uint32_t> stat_fifo_overflows_{0};
  std::atomic<uint32_t> stat_watchdog_recoveries_{0};
  std::atomic<uint32_t> stat_tx_recover_{0};
};

}  // namespace elero
}  // namespace esphome
