#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"
#include "radio_driver.h"
#include "cc1101.h"
#include "tx_client.h"
#include "elero_packet.h"
#include "elero_strings.h"
#include "device_type.h"
#include <string>
#include <atomic>

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

namespace elero {

/// Decoded RF packet info for WebSocket broadcast
struct RfPacketInfo {
  uint32_t timestamp_ms;
  int64_t decoded_at_us{0};  ///< esp_timer_get_time() when decode completed (us, for latency tracking)
  uint32_t src;           ///< Source address (remote for commands, blind for status)
  uint32_t dst;           ///< Destination address (blind for commands, remote for status)
  uint8_t channel;
  uint8_t type;           ///< Message type byte (0x6a=command, 0xca=status, etc.)
  uint8_t type2;          ///< Secondary type byte
  uint8_t command;        ///< Command byte (for command packets)
  uint8_t state;          ///< State byte (for status packets)
  uint8_t cnt;            ///< Rolling counter value from packet
  float rssi;
  uint8_t lqi;            ///< Link Quality Indicator (0-127)
  bool crc_ok;            ///< CRC status from CC1101 appended byte
  uint8_t hop;
  uint8_t payload[10];
  uint8_t raw_len;
  uint8_t raw[CC1101_FIFO_LENGTH];
};

// String conversion declarations (elero_state_to_string, etc.) are in elero_strings.h

// ─── RF Task IPC Structs ─────────────────────────────────────────────────────

/// Request from main loop -> RF task (via tx_queue).
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

/// Result from RF task -> main loop (via tx_done_queue).
struct TxResult {
  TxClient *client{nullptr};  ///< nullptr for fire-and-forget (raw TX)
  bool success{false};
};

}  // namespace elero
}  // namespace esphome

// Lock payload size invariant: decode_packet memcpy's ParseResult::payload -> RfPacketInfo::payload
static_assert(sizeof(esphome::elero::RfPacketInfo::payload) == sizeof(esphome::elero::packet::ParseResult::payload),
              "RfPacketInfo::payload and ParseResult::payload must have the same size");

namespace esphome {
namespace elero {

class Elero : public Component {
 public:
  void setup() override;
  void loop() override;

  static void IRAM_ATTR interrupt(Elero *arg);
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

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
  void set_stats_tx_success_sensor(sensor::Sensor *s) { stats_tx_success_ = s; }
  void set_stats_tx_fail_sensor(sensor::Sensor *s) { stats_tx_fail_ = s; }
  void set_stats_tx_recover_sensor(sensor::Sensor *s) { stats_tx_recover_ = s; }
  void set_stats_rx_packets_sensor(sensor::Sensor *s) { stats_rx_packets_ = s; }
  void set_stats_rx_drops_sensor(sensor::Sensor *s) { stats_rx_drops_ = s; }
  void set_stats_fifo_overflows_sensor(sensor::Sensor *s) { stats_fifo_overflows_ = s; }
  void set_stats_watchdog_sensor(sensor::Sensor *s) { stats_watchdog_ = s; }
  void set_stats_dispatch_latency_sensor(sensor::Sensor *s) { stats_dispatch_latency_ = s; }
  void set_stats_queue_transit_sensor(sensor::Sensor *s) { stats_queue_transit_ = s; }
  void set_stats_last_rx_age_sensor(sensor::Sensor *s) { stats_last_rx_age_ = s; }
#endif

  // ── Radio driver ──────────────────────────────────────────────────────────
  void set_driver(RadioDriver *d) { driver_ = d; }
  RadioDriver *get_driver() const { return driver_; }

  // ── IRQ pin (for ISR setup — driver doesn't own the pin, hub does) ────────
  void set_irq_pin(InternalGPIOPin *pin) { irq_pin_ = pin; }
  /// @deprecated Use set_irq_pin instead. Kept for backward YAML compatibility.
  void set_gdo0_pin(InternalGPIOPin *pin) { irq_pin_ = pin; }

  void set_freq0(uint8_t freq) { freq0_.store(freq); }
  void set_freq1(uint8_t freq) { freq1_.store(freq); }
  void set_freq2(uint8_t freq) { freq2_.store(freq); }

  void set_version(const char *version) { version_ = version; }
  const char *get_version() const { return version_; }

  // Unified device registry
  void set_registry(DeviceRegistry *reg) { registry_ = reg; }
  DeviceRegistry *get_registry() const { return registry_; }

  void reinit_frequency(uint8_t freq2, uint8_t freq1, uint8_t freq0);
  uint8_t get_freq0() const { return freq0_.load(); }
  uint8_t get_freq1() const { return freq1_.load(); }
  uint8_t get_freq2() const { return freq2_.load(); }

 private:
  // ─── Protocol-level methods (stay on Elero — not hardware) ─────────────────
  [[nodiscard]] optional<RfPacketInfo> decode_packet(const uint8_t *buf, size_t buf_len);
  void build_tx_packet_(const EleroCommand &cmd);  // Build packet in msg_tx_
  void decode_fifo_packets_(size_t fifo_count);  // Parse multiple packets from FIFO buffer

  // ─── RF task entry point ───────────────────────────────────────────────────
#ifdef USE_ESP32
  static void rf_task_func_(void *arg);
#endif

  // ─── ISR-shared state ──────────────────────────────────────────────────────
  std::atomic<bool> rx_ready_{false};   ///< ISR→RF task: RX packet available
  std::atomic<bool> tx_done_{false};    ///< ISR→RF task: TX transmission complete

  // ─── RF task-exclusive state (never accessed from main loop after setup) ───
  TxClient *tx_owner_{nullptr};        ///< Current TX owner (for completion callback)
  uint8_t msg_rx_[CC1101_FIFO_LENGTH]; ///< RX FIFO buffer (RF task only)
  uint8_t msg_tx_[CC1101_FIFO_LENGTH]; ///< TX packet buffer (RF task only)

  // ─── Atomic state (written by RF task, read by main loop) ──────────────────
  std::atomic<uint8_t> freq0_{defaults::FREQ0};
  std::atomic<uint8_t> freq1_{defaults::FREQ1};
  std::atomic<uint8_t> freq2_{defaults::FREQ2};

  // ─── Main loop-exclusive state ─────────────────────────────────────────────
  RadioDriver *driver_{nullptr};         ///< Radio hardware driver (CC1101, SX1262, etc.)
  InternalGPIOPin *irq_pin_{nullptr};    ///< Radio IRQ pin (GDO0 for CC1101, DIO1 for SX1262)

  // Unified device registry
  DeviceRegistry *registry_{nullptr};

  const char *version_{"unknown"};

  // ─── RF Stats (mixed core access) ──────────────────────────────────────────
  // Core 0 atomics (incremented on RF task, read on Core 1)
  std::atomic<uint32_t> stat_tx_recover_{0};
  std::atomic<uint32_t> stat_rx_drops_{0};
  std::atomic<uint32_t> stat_fifo_overflows_{0};
  std::atomic<uint32_t> stat_watchdog_recoveries_{0};

  // Core 1 only (incremented and read on main loop)
  uint32_t stat_tx_success_{0};
  uint32_t stat_tx_fail_{0};
  uint32_t stat_rx_packets_{0};
  uint32_t stat_last_rx_ms_{0};
  float stat_dispatch_latency_us_{0};
  float stat_queue_transit_us_{0};
  uint32_t last_stats_publish_ms_{0};

  void publish_stats_();

#ifdef USE_SENSOR
  // Internal diagnostic sensors (optional, created by codegen when auto_stats: true)
  sensor::Sensor *stats_tx_success_{nullptr};
  sensor::Sensor *stats_tx_fail_{nullptr};
  sensor::Sensor *stats_tx_recover_{nullptr};
  sensor::Sensor *stats_rx_packets_{nullptr};
  sensor::Sensor *stats_rx_drops_{nullptr};
  sensor::Sensor *stats_fifo_overflows_{nullptr};
  sensor::Sensor *stats_watchdog_{nullptr};
  sensor::Sensor *stats_dispatch_latency_{nullptr};
  sensor::Sensor *stats_queue_transit_{nullptr};
  sensor::Sensor *stats_last_rx_age_{nullptr};
#endif

  // ─── FreeRTOS IPC (cross-core communication) ──────────────────────────────
#ifdef USE_ESP32
  TaskHandle_t rf_task_handle_{nullptr};
  QueueHandle_t rx_queue_handle_{nullptr};       ///< RF task -> main loop: RfPacketInfo
  QueueHandle_t tx_queue_handle_{nullptr};       ///< Main loop -> RF task: RfTaskRequest
  QueueHandle_t tx_done_queue_handle_{nullptr};  ///< RF task -> main loop: TxResult
#endif
};

}  // namespace elero
}  // namespace esphome
