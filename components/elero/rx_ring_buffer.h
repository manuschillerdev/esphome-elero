/// @file rx_ring_buffer.h
/// @brief Fixed-size ring buffer for decoded RF packets.
///
/// Decouples fast-path FIFO decoding from slow-path dispatch (logging,
/// registry, sensors). Single-threaded — no mutexes needed since both
/// producer (drain_fifo_) and consumer (dispatch_queued_packets_) run
/// in the same ESPHome loop() context.
///
/// NOTE: This header must be included after RfPacketInfo is defined
/// (included from elero.h, not standalone).

#pragma once

#include <cstddef>

namespace esphome {
namespace elero {

template<typename T, size_t Capacity = 8>
class RxRingBuffer {
 public:
  /// Push an item into the buffer.
  /// @return true if stored, false if buffer is full (caller should warn).
  bool push(const T &item) {
    if (full()) {
      return false;
    }
    buf_[head_] = item;
    head_ = (head_ + 1) % Capacity;
    ++count_;
    return true;
  }

  /// Pop the oldest item from the buffer.
  /// @param[out] item Destination for the popped item.
  /// @return true if an item was available, false if empty.
  bool pop(T &item) {
    if (empty()) {
      return false;
    }
    item = buf_[tail_];
    tail_ = (tail_ + 1) % Capacity;
    --count_;
    return true;
  }

  [[nodiscard]] bool empty() const { return count_ == 0; }
  [[nodiscard]] bool full() const { return count_ == Capacity; }
  [[nodiscard]] size_t size() const { return count_; }
  static constexpr size_t capacity() { return Capacity; }

 private:
  T buf_[Capacity]{};
  size_t head_{0};
  size_t tail_{0};
  size_t count_{0};
};

}  // namespace elero
}  // namespace esphome
