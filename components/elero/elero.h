#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/spi/spi.h"
#include "cc1101.h"
#include "tx_client.h"
#include "elero_packet.h"
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <atomic>
#include <functional>

// Elero RF protocol implementation based on reverse-engineered specifications.
// Protocol documentation: https://github.com/QuadCorei8085/elero_protocol
// Additional protocol research: https://github.com/stanleypa/eleropy

namespace esphome {

#ifdef USE_SENSOR
namespace sensor {
class Sensor;
}
#endif
#ifdef USE_TEXT_SENSOR
namespace text_sensor {
class TextSensor;
}
#endif

namespace elero {

// ─── TX State Machine ────────────────────────────────────────────────────────
enum class TxState : uint8_t {
  IDLE,          // Not transmitting
  GOTO_IDLE,     // Sent SIDLE, waiting for MARCSTATE_IDLE
  FLUSH_TX,      // Sent SFTX, brief settling
  LOAD_FIFO,     // Loaded TX FIFO, preparing STX
  TRIGGER_TX,    // Sent STX, waiting for MARCSTATE_TX
  WAIT_TX_DONE,  // TX in progress, waiting for GDO0 interrupt
  VERIFY_DONE,   // Checking TXBYTES == 0
  RETURN_RX,     // Returning to RX state
};

struct TxContext {
  TxState state{TxState::IDLE};
  uint32_t state_enter_time{0};
  uint8_t defer_count{0};        ///< Times deferred due to busy channel
  uint32_t backoff_until{0};     ///< Don't attempt TX until this time (millis)

  static constexpr uint32_t STATE_TIMEOUT_MS = 50;
  static constexpr uint8_t DEFER_BACKOFF_THRESHOLD = 3;   ///< Start backoff after N defers
  static constexpr uint8_t DEFER_MAX = 10;                ///< Abort after N defers
  static constexpr uint32_t BACKOFF_MAX_MS = 50;          ///< Max random backoff (ms)

  void reset() {
    defer_count = 0;
    backoff_until = 0;
  }
};

struct EleroCommand {
  uint8_t counter;
  uint32_t dst_addr;   ///< Destination address (blind/light we control)
  uint32_t src_addr;   ///< Source address (our emulated remote)
  uint8_t channel;
  uint8_t type;        ///< Message type (0x6a=command, 0xca=status, etc.)
  uint8_t type2;       ///< Secondary type byte
  uint8_t hop;
  uint8_t payload[10];
};

/// Decoded RF packet info for WebSocket broadcast
struct RfPacketInfo {
  uint32_t timestamp_ms;
  uint32_t src;           ///< Source address (remote for commands, blind for status)
  uint32_t dst;           ///< Destination address (blind for commands, remote for status)
  uint8_t channel;
  uint8_t type;           ///< Message type byte (0x6a=command, 0xca=status, etc.)
  uint8_t type2;          ///< Secondary type byte
  uint8_t command;        ///< Command byte (for command packets)
  uint8_t state;          ///< State byte (for status packets)
  float rssi;
  uint8_t hop;
  uint8_t payload[10];
  uint8_t raw_len;
  uint8_t raw[CC1101_FIFO_LENGTH];
};

const char *elero_state_to_string(uint8_t state);
const char *elero_command_to_string(uint8_t command);

/// Convert action string ("up", "down", "stop", etc.) to command byte.
/// Returns 0xFF if action is not recognized.
uint8_t elero_action_to_command(const char *action);

/// Abstract base class for light actuators registered with the Elero hub.
/// EleroLight inherits from this so the hub never needs the light header.
class EleroLightBase {
 public:
  virtual ~EleroLightBase() = default;
  virtual uint32_t get_blind_address() = 0;
  virtual void set_rx_state(uint8_t state) = 0;
  virtual void notify_rx_meta(uint32_t ms, float rssi) {}
  virtual void enqueue_command(uint8_t cmd_byte) = 0;
  /// Called by the hub when a remote command packet (0x6a/0x69) targets this
  /// device, so it can poll the blind immediately instead of waiting for the
  /// normal poll interval.  Default no-op; concrete classes override.
  virtual void schedule_immediate_poll() {}

  // Web API helpers — identity & configuration
  virtual std::string get_light_name() const = 0;
  virtual uint8_t get_channel() const = 0;
  virtual uint32_t get_remote_address() const = 0;
  virtual uint32_t get_dim_duration_ms() const = 0;
};

/// Abstract base class for blinds registered with the Elero hub.
/// EleroCover inherits from this so the hub never needs the cover header.
class EleroBlindBase {
 public:
  virtual ~EleroBlindBase() = default;
  virtual void set_rx_state(uint8_t state) = 0;
  virtual uint32_t get_blind_address() = 0;
  virtual void set_poll_offset(uint32_t offset) = 0;
  // Called by the hub whenever a packet arrives from this blind
  virtual void notify_rx_meta(uint32_t ms, float rssi) {}  // default no-op
  // Web API helpers — identity & state
  virtual std::string get_blind_name() const = 0;
  virtual float get_cover_position() const = 0;
  virtual const char *get_operation_str() const = 0;
  virtual uint32_t get_last_seen_ms() const = 0;
  virtual float get_last_rssi() const = 0;
  virtual uint8_t get_last_state_raw() const = 0;
  // Web API helpers — configuration
  virtual uint8_t get_channel() const = 0;
  virtual uint32_t get_remote_address() const = 0;
  virtual uint32_t get_poll_interval_ms() const = 0;
  virtual uint32_t get_open_duration_ms() const = 0;
  virtual uint32_t get_close_duration_ms() const = 0;
  virtual bool get_supports_tilt() const = 0;
  // Web API commands — use perform_action() for standard commands (same path as HA)
  virtual bool perform_action(const char *action) = 0;
  // Low-level command queue (bypasses entity logic, use only for protocol-specific commands like "check")
  virtual void enqueue_command(uint8_t cmd_byte) = 0;
  /// Called by the hub when a remote command packet (0x6a/0x69) targets this
  /// blind, so it can poll the blind immediately instead of waiting for the
  /// normal poll interval.  Default no-op; concrete classes override.
  virtual void schedule_immediate_poll() {}
  virtual void apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) = 0;
};

class Elero;  // Forward declaration for SpiTransaction

/// RAII guard for SPI transactions. Calls enable() on construction and
/// disable() on destruction, ensuring CS is always released even on early return.
class SpiTransaction {
 public:
  explicit SpiTransaction(Elero *device);
  ~SpiTransaction();
  SpiTransaction(const SpiTransaction &) = delete;
  SpiTransaction &operator=(const SpiTransaction &) = delete;

 private:
  Elero *device_;
};

class Elero : public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                    spi::DATA_RATE_2MHZ>,
              public Component {
 public:
  void setup() override;
  void loop() override;

  static void interrupt(Elero *arg);
  void set_received();
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void reset();
  void init();

  // SPI communication methods — return false on CC1101 RXFIFO overflow
  [[nodiscard]] bool write_reg(uint8_t addr, uint8_t data);
  [[nodiscard]] bool write_burst(uint8_t addr, uint8_t *data, uint8_t len);
  [[nodiscard]] bool write_cmd(uint8_t cmd);
  [[nodiscard]] bool wait_rx();
  [[nodiscard]] bool wait_tx();
  [[nodiscard]] bool wait_tx_done();
  [[nodiscard]] bool wait_idle();
  [[nodiscard]] bool transmit();
  [[nodiscard]] uint8_t read_reg(uint8_t addr, bool *ok = nullptr);
  [[nodiscard]] uint8_t read_status(uint8_t addr);
  void read_buf(uint8_t addr, uint8_t *buf, uint8_t len);
  void flush_and_rx();
  void interpret_msg();

  // Non-blocking TX state machine (internal)
  [[nodiscard]] bool start_transmit();  // Begin async TX, returns true if started
  bool is_tx_busy() const { return tx_ctx_.state != TxState::IDLE || tx_owner_ != nullptr; }
  [[nodiscard]] bool poll_tx_result();  // Check if TX completed, get result

  // Non-blocking TX API for CommandSender
  /// Request to transmit a command. Non-blocking.
  /// @param client The sender requesting TX (receives completion callback)
  /// @param cmd The command to transmit
  /// @return true if TX started, false if hub is busy
  [[nodiscard]] bool request_tx(TxClient *client, const EleroCommand &cmd);

  /// Get the current TX owner (for debugging/testing).
  TxClient *get_tx_owner() const { return tx_owner_; }

  void register_cover(EleroBlindBase *cover);
  void register_light(EleroLightBase *light);

  // Legacy blocking TX API (for backwards compatibility and simple use cases)
  [[nodiscard]] bool send_command(EleroCommand *cmd);

  // Raw TX API (for WebSocket debugging/testing)
  [[nodiscard]] bool send_raw_command(uint32_t dst_addr, uint32_t src_addr, uint8_t channel,
                                      uint8_t command,
                                      uint8_t payload_1 = packet::defaults::PAYLOAD_1,
                                      uint8_t payload_2 = packet::defaults::PAYLOAD_2,
                                      uint8_t type = packet::msg_type::COMMAND,
                                      uint8_t type2 = packet::defaults::TYPE2,
                                      uint8_t hop = packet::defaults::HOP);

#ifdef USE_SENSOR
  void register_rssi_sensor(uint32_t address, sensor::Sensor *sensor);
#endif
#ifdef USE_TEXT_SENSOR
  void register_text_sensor(uint32_t address, text_sensor::TextSensor *sensor);
#endif

  // Cover access for web server
  bool is_cover_configured(uint32_t address) const {
    return address_to_cover_mapping_.find(address) != address_to_cover_mapping_.end();
  }
  const std::map<uint32_t, EleroBlindBase *> &get_configured_covers() const { return address_to_cover_mapping_; }
  const std::map<uint32_t, EleroLightBase *> &get_configured_lights() const { return address_to_light_mapping_; }

  void set_gdo0_pin(InternalGPIOPin *pin) { gdo0_pin_ = pin; }
  void set_freq0(uint8_t freq) { freq0_ = freq; }
  void set_freq1(uint8_t freq) { freq1_ = freq; }
  void set_freq2(uint8_t freq) { freq2_ = freq; }

  // RF packet notification callback (used by web server)
  using RfPacketCallback = std::function<void(const RfPacketInfo &)>;
  void set_rf_packet_callback(RfPacketCallback cb) { on_rf_packet_ = std::move(cb); }

  void reinit_frequency(uint8_t freq2, uint8_t freq1, uint8_t freq0);
  uint8_t get_freq0() const { return freq0_; }
  uint8_t get_freq1() const { return freq1_; }
  uint8_t get_freq2() const { return freq2_; }

 private:
  void handle_tx_state_(uint32_t now);  // Progress TX state machine
  void defer_tx_(uint32_t now);         // Defer TX due to busy channel
  void abort_tx_();                     // Abort TX and return to RX
  void build_tx_packet_(const EleroCommand &cmd);  // Build packet in msg_tx_
  void notify_tx_owner_(bool success);  // Notify owner and clear

  std::atomic<bool> received_{false};
  TxContext tx_ctx_;
  bool tx_pending_success_{false};
  TxClient *tx_owner_{nullptr};  // Current TX owner (for non-blocking API)
  uint32_t last_chip_reset_ms_{0};  ///< Rate-limit chip resets (zombie recovery)
  uint8_t msg_rx_[CC1101_FIFO_LENGTH];
  uint8_t msg_tx_[CC1101_FIFO_LENGTH];
  uint8_t freq0_{defaults::FREQ0};
  uint8_t freq1_{defaults::FREQ1};
  uint8_t freq2_{defaults::FREQ2};
  InternalGPIOPin *gdo0_pin_{nullptr};
  std::map<uint32_t, EleroBlindBase *> address_to_cover_mapping_;
  std::map<uint32_t, EleroLightBase *> address_to_light_mapping_;
#ifdef USE_SENSOR
  std::map<uint32_t, sensor::Sensor *> address_to_rssi_sensor_;
#endif
#ifdef USE_TEXT_SENSOR
  std::map<uint32_t, text_sensor::TextSensor *> address_to_text_sensor_;
#endif

  // RF packet notification callback (optional, set by web server)
  RfPacketCallback on_rf_packet_{};
};

}  // namespace elero
}  // namespace esphome
