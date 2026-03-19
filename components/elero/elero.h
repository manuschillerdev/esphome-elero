#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/spi/spi.h"
#include "cc1101.h"
#include "tx_client.h"
#include "elero_packet.h"
#include "elero_strings.h"
#include "device_type.h"
#include <string>
#include <map>
#include <atomic>
#include <functional>

#ifdef USE_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#endif

// Forward declaration for new architecture registry
namespace esphome { namespace elero { class DeviceRegistry; } }

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
#ifdef USE_BINARY_SENSOR
namespace binary_sensor {
class BinarySensor;
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

/// Decoded RF packet info for WebSocket broadcast
struct RfPacketInfo {
  uint32_t timestamp_ms;
  int64_t decoded_at_us{0};  ///< esp_timer_get_time() when decode completed (µs, for latency tracking)
  uint32_t src;           ///< Source address (remote for commands, blind for status)
  uint32_t dst;           ///< Destination address (blind for commands, remote for status)
  uint8_t channel;
  uint8_t type;           ///< Message type byte (0x6a=command, 0xca=status, etc.)
  uint8_t type2;          ///< Secondary type byte
  uint8_t command;        ///< Command byte (for command packets)
  uint8_t state;          ///< State byte (for status packets)
  bool echo;              ///< true if this command packet matches a recent TX (mesh echo)
  uint8_t cnt;            ///< Rolling counter value from packet
  float rssi;
  uint8_t lqi;            ///< Link Quality Indicator (0–127)
  bool crc_ok;            ///< CRC status from CC1101 appended byte
  uint8_t hop;
  uint8_t payload[10];
  uint8_t raw_len;
  uint8_t raw[CC1101_FIFO_LENGTH];
};

// String conversion declarations (elero_state_to_string, etc.) are in elero_strings.h

// ─── RF Task IPC Structs ─────────────────────────────────────────────────────

/// Request from main loop → RF task (via tx_queue).
/// Uses a union to minimize queue item size (~50 bytes).
struct RfTaskRequest {
  enum class Type : uint8_t { TX, REINIT_FREQ } type;
  TxClient *client{nullptr};  ///< TX: completion callback target (stable ptr on Device)
  union {
    EleroCommand cmd;                            ///< TX: command to transmit
    struct { uint8_t f2, f1, f0; } freq;         ///< REINIT_FREQ: new frequency registers
  };

  RfTaskRequest() : type(Type::TX), client(nullptr), cmd{} {}
};

/// Result from RF task → main loop (via tx_done_queue).
struct TxResult {
  TxClient *client{nullptr};  ///< nullptr for fire-and-forget (raw TX)
  bool success{false};
};

}  // namespace elero
}  // namespace esphome

// Lock payload size invariant: decode_packet memcpy's ParseResult::payload → RfPacketInfo::payload
static_assert(sizeof(esphome::elero::RfPacketInfo::payload) == sizeof(esphome::elero::packet::ParseResult::payload),
              "RfPacketInfo::payload and ParseResult::payload must have the same size");

namespace esphome {
namespace elero {

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

  static void IRAM_ATTR interrupt(Elero *arg);
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void reset();
  void init();

  /// Dispatch a decoded packet (slow path — logging, registry, sensors).
  /// Called from main loop only (Core 1).
  void dispatch_packet(const RfPacketInfo &pkt);

  // Non-blocking TX API for CommandSender.
  // Posts command to RF task queue. Returns true if queued, false if queue full.
  // Completion is notified asynchronously via TxClient::on_tx_complete() on Core 1.
  [[nodiscard]] bool request_tx(TxClient *client, const EleroCommand &cmd);

  // Raw TX API (for WebSocket debugging/testing) — fire-and-forget via queue.
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
#ifdef USE_BINARY_SENSOR
  void register_problem_sensor(uint32_t address, binary_sensor::BinarySensor *sensor);
#endif

  void set_gdo0_pin(InternalGPIOPin *pin) { gdo0_pin_ = pin; }
  void set_freq0(uint8_t freq) { freq0_.store(freq); }
  void set_freq1(uint8_t freq) { freq1_.store(freq); }
  void set_freq2(uint8_t freq) { freq2_.store(freq); }

  void set_version(const char *version) { version_ = version; }
  const char *get_version() const { return version_; }

  // RF packet notification callback (used by web server)
  using RfPacketCallback = std::function<void(const RfPacketInfo &)>;
  void set_rf_packet_callback(RfPacketCallback cb) { on_rf_packet_ = std::move(cb); }

  // Unified device registry
  void set_registry(DeviceRegistry *reg) { registry_ = reg; }
  DeviceRegistry *get_registry() const { return registry_; }

  void reinit_frequency(uint8_t freq2, uint8_t freq1, uint8_t freq0);
  uint8_t get_freq0() const { return freq0_.load(); }
  uint8_t get_freq1() const { return freq1_.load(); }
  uint8_t get_freq2() const { return freq2_.load(); }

 private:
  // ─── SPI communication (called exclusively from RF task after setup) ────────
  [[nodiscard]] bool write_reg(uint8_t addr, uint8_t data);
  [[nodiscard]] bool write_burst(uint8_t addr, uint8_t *data, uint8_t len);
  [[nodiscard]] bool write_cmd(uint8_t cmd);
  [[nodiscard]] bool wait_rx();
  [[nodiscard]] uint8_t read_reg(uint8_t addr, bool *ok = nullptr);
  [[nodiscard]] uint8_t read_status(uint8_t addr);
  void read_buf(uint8_t addr, uint8_t *buf, uint8_t len);
  void flush_and_rx();
  [[nodiscard]] bool start_transmit();
  [[nodiscard]] optional<RfPacketInfo> decode_packet(const uint8_t *buf, size_t buf_len);

  // ─── TX state machine (RF task only) ──────────────────────────────────────
  void handle_tx_state_(uint32_t now);  // Progress TX state machine
  void defer_tx_(uint32_t now);         // Defer TX due to busy channel
  void abort_tx_();                     // Abort TX and return to RX
  void build_tx_packet_(const EleroCommand &cmd);  // Build packet in msg_tx_
  uint8_t read_status_reliable_(uint8_t addr);     // CC1101 errata workaround
  void check_radio_health_();           // Periodic watchdog for stuck radio states
  void record_tx_(uint8_t counter);     // Record TX counter for echo detection
  bool is_own_echo_(uint8_t counter) const;  // Check if RX counter matches a recent TX
  void drain_fifo_();                   // Read FIFO, decode packets, push to queue

  // ─── RF task entry point ───────────────────────────────────────────────────
#ifdef USE_ESP32
  static void rf_task_func_(void *arg);
#endif

  // ─── ISR-shared state ──────────────────────────────────────────────────────
  std::atomic<bool> received_{false};   ///< Set by ISR, consumed by RF task

  // ─── RF task-exclusive state (never accessed from main loop after setup) ───
  TxContext tx_ctx_;
  bool tx_pending_success_{false};
  TxClient *tx_owner_{nullptr};        ///< Current TX owner (for completion callback)
  uint32_t last_chip_reset_ms_{0};     ///< Rate-limit chip resets (zombie recovery)
  uint32_t last_radio_check_ms_{0};    ///< Last radio health check timestamp
  uint8_t msg_rx_[CC1101_FIFO_LENGTH]; ///< RX FIFO buffer (RF task only)
  uint8_t msg_tx_[CC1101_FIFO_LENGTH]; ///< TX packet buffer (RF task only)

  // TX echo detection: ring buffer of recently sent counter values (RF task only)
  static constexpr size_t TX_HISTORY_SIZE = 16;
  uint8_t tx_history_[TX_HISTORY_SIZE]{};
  uint8_t tx_history_idx_{0};

  // ─── Atomic state (written by RF task, read by main loop) ──────────────────
  std::atomic<uint8_t> freq0_{defaults::FREQ0};
  std::atomic<uint8_t> freq1_{defaults::FREQ1};
  std::atomic<uint8_t> freq2_{defaults::FREQ2};

  // ─── Main loop-exclusive state ─────────────────────────────────────────────
  InternalGPIOPin *gdo0_pin_{nullptr};
#ifdef USE_SENSOR
  std::map<uint32_t, sensor::Sensor *> address_to_rssi_sensor_;
#endif
#ifdef USE_TEXT_SENSOR
  std::map<uint32_t, text_sensor::TextSensor *> address_to_text_sensor_;
#endif
#ifdef USE_BINARY_SENSOR
  std::map<uint32_t, binary_sensor::BinarySensor *> address_to_problem_sensor_;
#endif

  // RF packet notification callback (optional, set by web server)
  RfPacketCallback on_rf_packet_{};

  // Unified device registry
  DeviceRegistry *registry_{nullptr};

  const char *version_{"unknown"};

  // ─── FreeRTOS IPC (cross-core communication) ──────────────────────────────
#ifdef USE_ESP32
  TaskHandle_t rf_task_handle_{nullptr};
  QueueHandle_t rx_queue_handle_{nullptr};       ///< RF task → main loop: RfPacketInfo
  QueueHandle_t tx_queue_handle_{nullptr};       ///< Main loop → RF task: RfTaskRequest
  QueueHandle_t tx_done_queue_handle_{nullptr};  ///< RF task → main loop: TxResult
#endif
};

}  // namespace elero
}  // namespace esphome
