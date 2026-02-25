#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/spi/spi.h"
#include "cc1101.h"
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <cstdarg>
#include <atomic>

// All encryption/decryption structures copied from https://github.com/QuadCorei8085/elero_protocol/ (MIT)
// All remote handling based on code from https://github.com/stanleypa/eleropy (GPLv3)

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

// ─── RF Command Bytes ─────────────────────────────────────────────────────
constexpr uint8_t ELERO_COMMAND_COVER_CONTROL = 0x6a;
constexpr uint8_t ELERO_COMMAND_COVER_CHECK = 0x00;
constexpr uint8_t ELERO_COMMAND_COVER_STOP = 0x10;
constexpr uint8_t ELERO_COMMAND_COVER_UP = 0x20;
constexpr uint8_t ELERO_COMMAND_COVER_TILT = 0x24;
constexpr uint8_t ELERO_COMMAND_COVER_DOWN = 0x40;
constexpr uint8_t ELERO_COMMAND_COVER_INT = 0x44;

// ─── RF State Values ──────────────────────────────────────────────────────
// State byte received in payload[6] of CA/C9 status response packets.
//
// Cover state mapping (EleroCover::set_rx_state):
//   ELERO_STATE_TOP              → position=1.0, operation=IDLE
//   ELERO_STATE_BOTTOM           → position=0.0, operation=IDLE
//   ELERO_STATE_INTERMEDIATE     → position=unchanged, operation=IDLE
//   ELERO_STATE_TILT             → tilt=1.0, operation=IDLE
//   ELERO_STATE_TOP_TILT         → position=1.0, tilt=1.0, operation=IDLE
//   ELERO_STATE_BOTTOM_TILT      → position=0.0, tilt=1.0, operation=IDLE
//   ELERO_STATE_START_MOVING_UP  → operation=OPENING
//   ELERO_STATE_MOVING_UP        → operation=OPENING
//   ELERO_STATE_START_MOVING_DOWN→ operation=CLOSING
//   ELERO_STATE_MOVING_DOWN      → operation=CLOSING
//   ELERO_STATE_STOPPED          → operation=IDLE
//   ELERO_STATE_BLOCKING         → operation=IDLE (logs warning)
//   ELERO_STATE_OVERHEATED       → operation=IDLE (logs warning)
//   ELERO_STATE_TIMEOUT          → operation=IDLE (logs warning)
//
// Light state mapping (EleroLight::set_rx_state):
//   ELERO_STATE_ON               → is_on=true, brightness=1.0
//   ELERO_STATE_OFF              → is_on=false, brightness=0.0
//
constexpr uint8_t ELERO_STATE_UNKNOWN = 0x00;
constexpr uint8_t ELERO_STATE_TOP = 0x01;
constexpr uint8_t ELERO_STATE_BOTTOM = 0x02;
constexpr uint8_t ELERO_STATE_INTERMEDIATE = 0x03;
constexpr uint8_t ELERO_STATE_TILT = 0x04;
constexpr uint8_t ELERO_STATE_BLOCKING = 0x05;
constexpr uint8_t ELERO_STATE_OVERHEATED = 0x06;
constexpr uint8_t ELERO_STATE_TIMEOUT = 0x07;
constexpr uint8_t ELERO_STATE_START_MOVING_UP = 0x08;
constexpr uint8_t ELERO_STATE_START_MOVING_DOWN = 0x09;
constexpr uint8_t ELERO_STATE_MOVING_UP = 0x0a;
constexpr uint8_t ELERO_STATE_MOVING_DOWN = 0x0b;
constexpr uint8_t ELERO_STATE_STOPPED = 0x0d;
constexpr uint8_t ELERO_STATE_TOP_TILT = 0x0e;
constexpr uint8_t ELERO_STATE_BOTTOM_TILT = 0x0f;
constexpr uint8_t ELERO_STATE_OFF = 0x0f;
constexpr uint8_t ELERO_STATE_ON = 0x10;

// ─── Protocol Limits ──────────────────────────────────────────────────────
constexpr uint8_t ELERO_MAX_PACKET_SIZE = 57;  // according to FCC documents

// ─── Timing Constants ─────────────────────────────────────────────────────
constexpr uint32_t ELERO_POLL_INTERVAL_MOVING = 2000;   // poll every two seconds while moving
constexpr uint32_t ELERO_DELAY_SEND_PACKETS = 50;       // 50ms send delay between repeats
constexpr uint32_t ELERO_TIMEOUT_MOVEMENT = 120000;     // poll for up to two minutes while moving

// ─── Queue/Buffer Limits ──────────────────────────────────────────────────
constexpr uint8_t ELERO_SEND_RETRIES = 3;
constexpr uint8_t ELERO_SEND_PACKETS = 2;
constexpr uint8_t ELERO_MAX_COMMAND_QUEUE = 10;   // max commands per blind to prevent OOM
constexpr uint8_t ELERO_MAX_DISCOVERED = 20;      // max discovered blinds to track
constexpr uint8_t ELERO_MAX_RAW_PACKETS = 50;     // max raw packets in dump ring buffer

// ─── RF Protocol Encoding/Encryption Constants ────────────────────────────
constexpr uint8_t ELERO_MSG_LENGTH = 0x1d;        // Fixed message length for TX
constexpr uint16_t ELERO_CRYPTO_MULT = 0x708f;    // Encryption multiplier for counter-based code
constexpr uint16_t ELERO_CRYPTO_MASK = 0xffff;    // Mask for 16-bit encryption code
constexpr uint8_t ELERO_SYS_ADDR = 0x01;          // System address in protocol
constexpr uint8_t ELERO_DEST_COUNT = 0x01;        // Destination count in command

// ─── RSSI Calculation Constants ───────────────────────────────────────────
// CC1101 RSSI is in dBm, raw value is two's complement encoded
constexpr uint8_t ELERO_RSSI_SIGN_BIT = 127;      // Sign bit threshold (values > 127 are negative)
constexpr int8_t ELERO_RSSI_OFFSET = -74;         // Constant offset applied in RSSI calculation
constexpr int ELERO_RSSI_DIVISOR = 2;             // Divisor for raw RSSI value

// ─── TX State Machine ────────────────────────────────────────────────────────
enum class TxState : uint8_t {
  IDLE,           // Not transmitting
  GOTO_IDLE,      // Sent SIDLE, waiting for MARCSTATE_IDLE
  FLUSH_TX,       // Sent SFTX, brief settling
  LOAD_FIFO,      // Loaded TX FIFO, preparing STX
  TRIGGER_TX,     // Sent STX, waiting for MARCSTATE_TX
  WAIT_TX_DONE,   // TX in progress, waiting for GDO0 interrupt
  VERIFY_DONE,    // Checking TXBYTES == 0
  RETURN_RX,      // Returning to RX state
};

struct TxContext {
  TxState state{TxState::IDLE};
  uint32_t state_enter_time{0};
  static constexpr uint32_t STATE_TIMEOUT_MS = 50;
};

typedef struct {
  uint8_t counter;
  uint32_t blind_addr;
  uint32_t remote_addr;
  uint8_t channel;
  uint8_t pck_inf[2];
  uint8_t hop;
  uint8_t payload[10];
} t_elero_command;

struct RawPacket {
  uint32_t timestamp_ms;            // millis() when captured
  uint8_t  fifo_len;                // bytes actually read from CC1101 FIFO
  uint8_t  data[CC1101_FIFO_LENGTH];
  bool     valid;                   // true = passed all validation and decoded
  char     reject_reason[32];       // empty when valid
};

struct DiscoveredBlind {
  uint32_t blind_address;
  uint32_t remote_address;
  uint8_t channel;
  uint8_t pck_inf[2];
  uint8_t hop;
  uint8_t payload_1;
  uint8_t payload_2;
  float rssi;
  uint32_t last_seen;
  uint8_t last_state;
  uint16_t times_seen;
  // true when params were derived from a command packet (6a/69) — these are
  // the correct values to use when sending commands to the blind.
  // false when params came only from a CA/C9 status response (less reliable).
  bool params_from_command{false};
};

struct RuntimeBlind {
  uint32_t blind_address;
  uint32_t remote_address;
  uint8_t channel;
  uint8_t pck_inf[2];
  uint8_t hop;
  uint8_t payload_1;
  uint8_t payload_2;
  std::string name;
  uint32_t open_duration_ms{0};
  uint32_t close_duration_ms{0};
  uint32_t poll_intvl_ms{300000};
  uint32_t last_seen_ms{0};
  float last_rssi{0.0f};
  uint8_t last_state{ELERO_STATE_UNKNOWN};
  uint8_t cmd_counter{1};
  std::queue<uint8_t> command_queue;
};

const char *elero_state_to_string(uint8_t state);

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
  // Web API commands
  virtual void enqueue_command(uint8_t cmd_byte) = 0;
  /// Called by the hub when a remote command packet (0x6a/0x69) targets this
  /// blind, so it can poll the blind immediately instead of waiting for the
  /// normal poll interval.  Default no-op; concrete classes override.
  virtual void schedule_immediate_poll() {}
  virtual void apply_runtime_settings(uint32_t open_dur_ms,
                                      uint32_t close_dur_ms,
                                      uint32_t poll_intvl_ms) = 0;
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

class Elero : public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                    spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_2MHZ>,
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
  uint8_t read_reg(uint8_t addr, bool *ok = nullptr);
  [[nodiscard]] uint8_t read_status(uint8_t addr);
  void read_buf(uint8_t addr, uint8_t *buf, uint8_t len);
  void flush_and_rx();
  void interpret_msg();

  // Non-blocking TX state machine
  [[nodiscard]] bool start_transmit();       // Begin async TX, returns true if started
  bool is_tx_busy() const { return tx_ctx_.state != TxState::IDLE; }
  [[nodiscard]] bool poll_tx_result();       // Check if TX completed, get result
  void register_cover(EleroBlindBase *cover);
  void register_light(EleroLightBase *light);
  [[nodiscard]] bool send_command(t_elero_command *cmd);

#ifdef USE_SENSOR
  void register_rssi_sensor(uint32_t address, sensor::Sensor *sensor);
#endif
#ifdef USE_TEXT_SENSOR
  void register_text_sensor(uint32_t address, text_sensor::TextSensor *sensor);
#endif

  // Discovery / scan mode
  void start_scan() { scan_mode_ = true; }
  void stop_scan() { scan_mode_ = false; }
  bool is_scanning() const { return scan_mode_; }
  const std::vector<DiscoveredBlind> &get_discovered_blinds() const { return discovered_blinds_; }
  size_t get_discovered_count() const { return discovered_blinds_.size(); }
  void clear_discovered() { discovered_blinds_.clear(); }

  // Cover access for web server
  bool is_cover_configured(uint32_t address) const {
    return address_to_cover_mapping_.find(address) != address_to_cover_mapping_.end();
  }
  const std::map<uint32_t, EleroBlindBase *> &get_configured_covers() const { return address_to_cover_mapping_; }

  // Packet dump mode: capture every received FIFO read into a ring buffer
  void start_packet_dump();
  void stop_packet_dump();
  bool is_packet_dump_active() const { return packet_dump_mode_; }
  const std::vector<RawPacket> &get_raw_packets() const { return raw_packets_; }
  void clear_raw_packets();

  // Runtime adopted blinds (controllable from web UI without reflashing)
  bool adopt_blind(const DiscoveredBlind &discovered, const std::string &name);
  bool remove_runtime_blind(uint32_t addr);
  bool send_runtime_command(uint32_t addr, uint8_t cmd_byte);
  bool update_runtime_blind_settings(uint32_t addr, uint32_t open_dur_ms,
                                     uint32_t close_dur_ms, uint32_t poll_intvl_ms);
  const std::map<uint32_t, RuntimeBlind> &get_runtime_blinds() const { return runtime_blinds_; }
  bool is_blind_adopted(uint32_t addr) const;

  // Log buffer
  static const uint8_t ELERO_LOG_BUFFER_SIZE = 200;
  struct LogEntry {
    uint32_t timestamp_ms;
    uint8_t level;
    char tag[24];
    char message[160];
  };
  void append_log(uint8_t level, const char *tag, const char *fmt, ...);
  void clear_log_entries() { log_entries_.clear(); log_write_idx_ = 0; }
  const std::vector<LogEntry> &get_log_entries() const { return log_entries_; }
  void set_log_capture(bool en) { log_capture_ = en; }
  bool is_log_capture_active() const { return log_capture_; }

  void set_gdo0_pin(InternalGPIOPin *pin) { gdo0_pin_ = pin; }
  void set_freq0(uint8_t freq) { freq0_ = freq; }
  void set_freq1(uint8_t freq) { freq1_ = freq; }
  void set_freq2(uint8_t freq) { freq2_ = freq; }
  void reinit_frequency(uint8_t freq2, uint8_t freq1, uint8_t freq0);
  uint8_t get_freq0() const { return freq0_; }
  uint8_t get_freq1() const { return freq1_; }
  uint8_t get_freq2() const { return freq2_; }

 private:
  void track_discovered_blind(uint32_t src, uint32_t remote, uint8_t channel,
                              uint8_t pck_inf0, uint8_t pck_inf1, uint8_t hop,
                              uint8_t payload_1, uint8_t payload_2,
                              float rssi, uint8_t state, bool from_command);
  void capture_raw_packet_(uint8_t fifo_len);
  void mark_last_raw_packet_(bool valid, const char *reason);
  void handle_tx_state_(uint32_t now);       // Progress TX state machine
  void abort_tx_();                          // Abort TX and return to RX

  std::atomic<bool> received_{false};
  TxContext tx_ctx_;
  bool tx_pending_success_{false};
  uint8_t msg_rx_[CC1101_FIFO_LENGTH];
  uint8_t msg_tx_[CC1101_FIFO_LENGTH];
  uint8_t freq0_{0x7a};
  uint8_t freq1_{0x71};
  uint8_t freq2_{0x21};
  InternalGPIOPin *gdo0_pin_{nullptr};
  std::map<uint32_t, EleroBlindBase*> address_to_cover_mapping_;
  std::map<uint32_t, EleroLightBase*> address_to_light_mapping_;
#ifdef USE_SENSOR
  std::map<uint32_t, sensor::Sensor*> address_to_rssi_sensor_;
#endif
#ifdef USE_TEXT_SENSOR
  std::map<uint32_t, text_sensor::TextSensor*> address_to_text_sensor_;
#endif
  std::vector<DiscoveredBlind> discovered_blinds_;
  bool scan_mode_{false};
  bool packet_dump_mode_{false};
  bool packet_dump_pending_update_{false};
  std::vector<RawPacket> raw_packets_;
  uint8_t raw_packet_write_idx_{0};
  std::map<uint32_t, RuntimeBlind> runtime_blinds_;
  // Log buffer
  bool log_capture_{false};
  std::vector<LogEntry> log_entries_;
  uint8_t log_write_idx_{0};
};

}  // namespace elero
}  // namespace esphome
