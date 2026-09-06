#pragma once

#include "radio_driver.h"

namespace esphome::elero {

/// Drain a bounded batch, including rejected frames. Return true only when
/// there is no remaining RX work that a TX/frequency change would discard.
/// Called on the RF task; deliver consumes the buffer synchronously.
template<typename Deliver>
bool drain_radio_rx(RadioDriver &driver, std::atomic<bool> &rx_ready,
                    uint8_t *buffer, size_t capacity, Deliver deliver) {
  for (unsigned i = 0; i < 4 && !driver.failed() && driver.has_data(); ++i) {
    rx_ready.exchange(false, std::memory_order_acquire);
    const size_t count = driver.read_fifo(buffer, capacity);
    if (count != 0) deliver(count);
    // Zero can mean a rejected frame; recheck hardware before stopping.
    if (driver.receiving()) return false;
  }
  return driver.failed() || (!driver.receiving() && !driver.has_data());
}

}  // namespace esphome::elero
