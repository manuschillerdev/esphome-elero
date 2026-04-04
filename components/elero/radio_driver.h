#pragma once

/// @file radio_driver.h
/// @brief Abstract interface for radio hardware drivers (CC1101, SX1262, etc.).
///
/// This interface isolates all SPI/hardware operations from the protocol layer.
/// The Elero hub calls these methods from the RF task (Core 0) only.
/// Implementations own all SPI state and hardware registers.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace elero {

/// Current radio mode — tracks the half-duplex state.
/// Callers can assert mode before RX/TX operations.
enum class RadioMode : uint8_t {
  RX,  ///< Radio is receiving (default idle state)
  TX,  ///< Radio is transmitting (FIFO loaded, TX in progress)
};

/// Result of polling the TX state machine.
enum class TxPollResult : uint8_t {
  PENDING,  ///< TX still in progress
  SUCCESS,  ///< TX completed successfully
  FAILED,   ///< TX failed (timeout, hardware error)
};

/// Radio health status from periodic watchdog check.
enum class RadioHealth : uint8_t {
  OK,             ///< Radio in expected RX state
  FIFO_OVERFLOW,  ///< RX FIFO overflow (deaf until flushed)
  STUCK,          ///< Radio stuck in unexpected state (e.g., IDLE)
  UNRECOVERABLE,  ///< Unrecoverable error
};

/// Abstract radio driver interface.
///
/// All methods are called from the RF task (Core 0) only, except where noted.
/// The ISR flag is shared with the hub's ISR via set_irq_flag().
class RadioDriver {
 public:
  virtual ~RadioDriver() = default;

  // ── Lifecycle ──────────────────────────────────────────────────────────────

  /// Initialize the radio hardware (register writes, enter RX).
  /// Called from setup() on Core 1 (before RF task starts) and from
  /// set_frequency_regs() on Core 0.
  /// @return true if initialization succeeded
  virtual bool init() = 0;

  /// Software reset the radio chip.
  virtual void reset() = 0;

  // ── IRQ flag ───────────────────────────────────────────────────────────────

  /// Pass the ISR atomic flag so the driver can read/clear it in poll_tx/has_data.
  /// Called once during setup, before init().
  void set_irq_flag(std::atomic<bool> *flag) { irq_flag_ = flag; }

  /// Current half-duplex mode. RX when idle, TX during transmission.
  [[nodiscard]] RadioMode mode() const { return mode_; }

  // ── TX (called from RF task only) ──────────────────────────────────────────

  /// Load packet into FIFO and start transmission.
  /// Performs the synchronous PREPARE phase (SIDLE -> flush -> load FIFO -> STX).
  /// @param pkt_buf Packet buffer (first byte is length)
  /// @param len Total buffer length including length byte
  /// @return true if TX started (now in WAIT_TX state), false on immediate failure
  virtual bool load_and_transmit(const uint8_t *pkt_buf, size_t len) = 0;

  /// Poll the TX state machine for completion.
  /// Must be called repeatedly after load_and_transmit() returns true.
  /// @return PENDING while transmitting, SUCCESS or FAILED when done
  virtual TxPollResult poll_tx() = 0;

  /// Abort an in-progress TX and recover the radio to RX.
  virtual void abort_tx() = 0;

  // ── RX (called from RF task only) ──────────────────────────────────────────

  /// Check if the radio has data available (FIFO non-empty).
  /// Reads the IRQ flag but does NOT clear it (caller clears after read_fifo).
  virtual bool has_data() = 0;

  /// Read raw bytes from the RX FIFO.
  /// Handles overflow detection internally (returns 0 on overflow).
  /// @param buf Output buffer
  /// @param max_len Maximum bytes to read
  /// @return Number of bytes read (0 on overflow or empty FIFO)
  virtual size_t read_fifo(uint8_t *buf, size_t max_len) = 0;

  // ── Health ─────────────────────────────────────────────────────────────────

  /// Check radio health (periodic watchdog).
  /// Only call when TX is idle. Internally throttled to every 5 seconds.
  virtual RadioHealth check_health() = 0;

  /// Recover from a bad state (flush FIFOs, re-enter RX, or full reset).
  virtual void recover() = 0;

  // ── Frequency ──────────────────────────────────────────────────────────────

  /// Change frequency registers and reinitialize the radio.
  /// Called from RF task when processing REINIT_FREQ requests.
  virtual void set_frequency_regs(uint8_t f2, uint8_t f1, uint8_t f0) = 0;

  // ── Diagnostics ────────────────────────────────────────────────────────────

  /// Dump driver configuration to ESPHome log.
  virtual void dump_config() = 0;

  /// Radio chip identifier (e.g., "cc1101", "sx1262") for UI/config events.
  virtual const char *radio_name() const = 0;

  /// Receiver sensitivity in dBm (e.g., -104 for CC1101, -117 for SX1262).
  /// Used by the UI to derive signal strength thresholds.
  virtual int rx_sensitivity_dbm() const = 0;

  /// Whether the IRQ pin fires on rising edge (true) or falling edge (false).
  /// CC1101 GDO0 goes LOW at end-of-packet → falling edge.
  /// SX1262 DIO1 goes HIGH on IRQ → rising edge.
  virtual bool irq_rising_edge() const { return false; }  // CC1101 default

 protected:
  RadioMode mode_{RadioMode::RX};
  std::atomic<bool> *irq_flag_{nullptr};
};

}  // namespace elero
}  // namespace esphome
