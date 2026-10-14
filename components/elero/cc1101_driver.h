#pragma once

/// @file cc1101_driver.h
/// @brief CC1101 radio driver — implements RadioDriver for TI CC1101 868 MHz transceiver.
///
/// Extracted from Elero class. Owns all SPI communication with the CC1101.
/// All methods are called from the RF task (Core 0) only, except init() which
/// is called once from setup() (Core 1) before the RF task starts.

#include "radio_driver.h"
#include "cc1101.h"
#include "cc1101_tx_fsm.h"
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


/// CC1101 radio driver implementation.
///
/// Inherits from spi::SPIDevice for ESPHome SPI bus integration.
/// All SPI operations are encapsulated here — the Elero hub never touches hardware.
class CC1101Driver : public RadioDriver,
                     public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                           spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_2MHZ>,
                     public Cc1101TxFsmOwner {
 public:
  CC1101Driver();

  // ── RadioDriver interface ──────────────────────────────────────────────────

  bool init() override;
  void reset() override;

  bool load_and_transmit(const uint8_t *pkt_buf, size_t len) override;
  TxPollResult poll_tx() override;
  void abort_tx() override;

  bool has_data() override;
  bool receiving() const override { return rx_packet_length_ != 0; }
  size_t read_fifo(uint8_t *buf, size_t max_len) override;

  RadioHealth check_health() override;
  void recover() override;

  void set_frequency_regs(uint8_t f2, uint8_t f1, uint8_t f0) override;
  void dump_config() override;
  const char *radio_name() const override { return "cc1101"; }
  int rx_sensitivity_dbm() const override { return -104; }

  // ── Internal TX FSM hooks ──────────────────────────────────────────────────

  bool tx_prepare_for_fsm() override;
  Cc1101TxPhaseResult tx_wait_started_for_fsm() override;
  Cc1101TxPhaseResult tx_wait_done_for_fsm() override;
  Cc1101TxPhaseResult tx_return_to_rx_for_fsm() override;
  void tx_on_state_enter_for_fsm(Cc1101TxState state, uint32_t now) override;
  void tx_set_terminal_result_for_fsm(Cc1101TxTerminalResult result) override;
  void tx_set_mode_for_fsm(RadioMode mode) override;
  void tx_recover_for_fsm() override;

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

  [[nodiscard]] bool flush_and_rx();
  [[nodiscard]] bool init_registers();
  void clear_recovery_failures_();


  // ── TX state machine ───────────────────────────────────────────────────────

  enum class TxDoneCheckResult : uint8_t {
    Pending,
    Succeeded,
    FailedTxBytesNotEmpty,
    FailedTimeout,
  };

  [[nodiscard]] TxDoneCheckResult tx_check_done_result_();

  Cc1101TxFsm tx_fsm_;
  Cc1101TxTerminalResult tx_terminal_result_{Cc1101TxTerminalResult::None};
  uint32_t tx_state_enter_time_{0};
  uint8_t tx_verify_retry_count_{0};
  uint8_t tx_buf_[CC1101_FIFO_LENGTH];  ///< Copy of packet for TX
  uint8_t rx_packet_length_{0};  ///< Consumed length byte; body stays in hardware until complete.
  uint32_t rx_packet_started_ms_{0};
  static constexpr uint32_t RX_PACKET_TIMEOUT_MS = 20;

  size_t tx_len_{0};                    ///< Length of data in tx_buf_

  // ── Frequency registers ────────────────────────────────────────────────────

  uint8_t freq0_{defaults::FREQ0};
  uint8_t freq1_{defaults::FREQ1};
  uint8_t freq2_{defaults::FREQ2};

  // ── Health check state ─────────────────────────────────────────────────────

  uint32_t last_radio_check_ms_{0};

  // ── TX timing / recovery escalation ───────────────────────────────────────

  static constexpr uint8_t TX_START_PROOF_POLLS = 20;
  static constexpr uint32_t TX_DONE_TIMEOUT_MS = 50;
  static constexpr uint32_t RX_READY_TIMEOUT_MS = 25;

  // Consecutive failures escalate: flush → reset → failed; success clears both.
  static constexpr uint8_t RECOVERIES_BEFORE_RESET = 3;
  static constexpr uint8_t RESETS_BEFORE_FAILED = 3;
  uint8_t failed_recoveries_{0};
  uint8_t failed_resets_{0};

  // ── Stats (atomic — incremented on Core 0, read from Core 1) ───────────────

  std::atomic<uint32_t> stat_fifo_overflows_{0};
  std::atomic<uint32_t> stat_watchdog_recoveries_{0};
  std::atomic<uint32_t> stat_tx_recover_{0};
};

}  // namespace elero
}  // namespace esphome
