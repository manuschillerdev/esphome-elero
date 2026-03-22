// Stub for unit tests
#pragma once
#include <cstdint>

namespace esphome {
namespace spi {

enum BitOrder { BIT_ORDER_MSB_FIRST };
enum ClockPolarity { CLOCK_POLARITY_LOW };
enum ClockPhase { CLOCK_PHASE_LEADING };
enum DataRate { DATA_RATE_2MHZ };

template <BitOrder, ClockPolarity, ClockPhase, DataRate>
class SPIDevice {
 public:
  void spi_setup() {}
  void enable() {}
  void disable() {}
  uint8_t transfer_byte(uint8_t) { return 0; }
  uint8_t read_byte() { return 0; }
  void write_byte(uint8_t) {}
};

}  // namespace spi
}  // namespace esphome
