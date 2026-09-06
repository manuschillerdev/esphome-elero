#pragma once

#include <cstdint>

namespace esphome::elero {

/// Interface for components that want to transmit via the Elero hub.
///
/// Components that need to send RF commands implement this interface to receive
/// asynchronous TX completion notifications. This enables non-blocking transmission
/// where the hub arbitrates access to the shared CC1101 radio.
///
/// Ownership model:
/// - A TxClient calls Elero::request_tx() to request transmission
/// - If granted (returns true), the client becomes the "owner" of the TX
/// - The hub delivers completion with the attempt ID captured at submission
/// - Invalidated or already-delivered attempts cannot notify the client
/// - Driver FSMs own hardware timeouts; waiting for queue capacity is not a retry
///
/// Thread safety:
/// - All calls happen in the main ESPHome loop (single-threaded)
/// - Callbacks are never called from ISR context
/// - Re-entrancy: on_tx_complete() may call request_tx() for a new transmission
class TxClient {
 public:
  virtual ~TxClient() = default;

  // Non-copyable, non-movable (prevent slicing, pointers are used for callbacks)
  TxClient(const TxClient &) = delete;
  TxClient &operator=(const TxClient &) = delete;
  TxClient(TxClient &&) = delete;
  TxClient &operator=(TxClient &&) = delete;

  /// Begin/invalidate/deliver are Core 1 only. The RF task transports the ID
  /// by value and never reads client state. Clients must outlive queued work.
  uint32_t begin_tx_attempt() { return ++this->tx_attempt_; }
  void invalidate_tx_attempt() { ++this->tx_attempt_; }
  void complete_tx_attempt(uint32_t attempt, bool success) {
    if (attempt != this->tx_attempt_) return;
    this->invalidate_tx_attempt();  // Consume before invoking a reentrant callback.
    this->on_tx_complete(success);
  }

  /// Called for the current, non-invalidated TX attempt.
  ///
  /// Cancellation invalidates outstanding attempts. Hardware timeouts arrive
  /// as failed completions; queue waiting has no separate client-side timeout.
  ///
  /// @param success true if transmission succeeded (packet sent, FIFO empty),
  ///                false on timeout, hardware error, or abort
  virtual void on_tx_complete(bool success) = 0;

 protected:
  // Only derived classes can construct
  TxClient() = default;

 private:
  uint32_t tx_attempt_{0};
};

}  // namespace esphome::elero
