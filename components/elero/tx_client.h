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
/// - The hub calls on_tx_complete() exactly once when TX finishes
/// - After the callback, ownership is released and another client can transmit
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

  /// Called by Elero hub when TX completes.
  ///
  /// This callback is guaranteed to be called exactly once for each successful
  /// request_tx() call. It will be called even on timeout or abort scenarios.
  ///
  /// @param success true if transmission succeeded (packet sent, FIFO empty),
  ///                false on timeout, hardware error, or abort
  virtual void on_tx_complete(bool success) = 0;

 protected:
  // Only derived classes can construct
  TxClient() = default;
};

}  // namespace esphome::elero
