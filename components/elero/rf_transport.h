#pragma once

#include "elero_packet.h"
#include "radio_driver.h"
#include "tx_completion.h"

namespace esphome::elero {

/// Copied from Core 1 to the RF task; client pointers have stable device lifetime.
struct RfTaskRequest {
  enum class Type : uint8_t { TX, REINIT_FREQ } type;
  TxClient *client{nullptr};
  uint32_t attempt{0};
  union {
    EleroCommand cmd;
    struct { uint8_t f2, f1, f0; } freq;
  };
  RfTaskRequest() : type(Type::TX), cmd{} {}
};

/// Nonblocking queue operations and synchronous frame delivery, owned by the hub.
class RfTransportPort {
 public:
  virtual ~RfTransportPort() = default;
  virtual bool take_request(RfTaskRequest &request) = 0;
  virtual bool publish_completion(const TxResult &result) = 0;
  virtual void receive_frame(const uint8_t *frame, size_t size) = 0;
};

/// Single RF-task owner of admission, RX draining and completion retention.
/// No client callbacks run here: Core 1 validates attempt IDs when dispatching.
class RfTransport {
 public:
  RfTransport(RadioDriver &driver, std::atomic<bool> &rx_ready, std::atomic<bool> &tx_done)
      : driver_(driver), rx_ready_(rx_ready), tx_done_(tx_done) {}

  /// One bounded pass. Returns true when an active TX failed (hub diagnostics).
  bool step(RfTransportPort &port);

 private:
  bool drain_rx_(RfTransportPort &port);
  void start_request_(const RfTaskRequest &request);
  bool poll_tx_();

  RadioDriver &driver_;
  std::atomic<bool> &rx_ready_;
  std::atomic<bool> &tx_done_;
  std::optional<TxResult> active_;
  PendingTxCompletion completion_;
  uint8_t rx_buffer_[packet::FIFO_LENGTH]{};
  uint8_t tx_buffer_[packet::FIFO_LENGTH]{};
};

}  // namespace esphome::elero
