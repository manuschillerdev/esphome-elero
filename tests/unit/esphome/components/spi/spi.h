// Stub for unit tests
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace esphome {
namespace spi {

namespace test_support {
inline std::deque<uint8_t> transfer_bytes;
inline std::deque<uint8_t> read_bytes;
inline std::vector<uint8_t> writes;
inline std::function<uint8_t(uint8_t)> transfer_handler;

inline void reset() {
  transfer_bytes.clear();
  read_bytes.clear();
  writes.clear();
  transfer_handler = {};
}
}  // namespace test_support

enum BitOrder { BIT_ORDER_MSB_FIRST };
enum ClockPolarity { CLOCK_POLARITY_LOW };
enum ClockPhase { CLOCK_PHASE_LEADING };
enum DataRate { DATA_RATE_2MHZ, DATA_RATE_8MHZ };

template <BitOrder, ClockPolarity, ClockPhase, DataRate>
class SPIDevice {
 public:
  void spi_setup() {}
  void enable() {}
  void disable() {}
  uint8_t transfer_byte(uint8_t value) {
    test_support::writes.push_back(value);
    if (test_support::transfer_handler) return test_support::transfer_handler(value);
    if (test_support::transfer_bytes.empty()) {
      return 0;
    }
    uint8_t out = test_support::transfer_bytes.front();
    test_support::transfer_bytes.pop_front();
    return out;
  }
  uint8_t read_byte() {
    if (test_support::read_bytes.empty()) {
      return 0;
    }
    uint8_t out = test_support::read_bytes.front();
    test_support::read_bytes.pop_front();
    return out;
  }
  void write_byte(uint8_t value) { test_support::writes.push_back(value); }
};

}  // namespace spi
}  // namespace esphome
