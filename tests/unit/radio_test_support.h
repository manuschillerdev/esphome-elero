#pragma once
#include <gtest/gtest.h>
#include <atomic>
#include <functional>
#define ESP_LOGV(tag, format, ...) ((void) 0)
#define ESP_LOGVV(tag, format, ...) ((void) 0)
#define ESP_LOGD(tag, format, ...) ((void) 0)
#define ESP_LOGI(tag, format, ...) ((void) 0)
#define ESP_LOGW(tag, format, ...) ((void) 0)
#define ESP_LOGE(tag, format, ...) ((void) 0)
#define ESP_LOGCONFIG(tag, format, ...) ((void) 0)
#define LOG_PIN(msg, pin) ((void) 0)
#include "elero/time_provider.h"
namespace esphome {
uint32_t millis() { return elero::get_time_provider().millis(); }
void delay(uint32_t) {}
class InternalGPIOPin {
 public:
  std::function<bool()> read = [] { return false; };
  void setup() {}
  bool digital_read() { return read(); }
  void digital_write(bool) {}
};
}
#include "elero/time_provider.cpp"
#include "esphome/components/spi/spi.h"
class RadioDriverTest : public ::testing::Test {
 protected:
  esphome::elero::MockTimeProvider time_;
  void SetUp() override {
    esphome::elero::set_time_provider(&time_);
    time_.reset();
    esphome::spi::test_support::reset();
  }
  void TearDown() override { esphome::elero::set_time_provider(nullptr); }
};

#include "elero/cc1101_compat.h"
inline std::vector<uint8_t> make_radio_frame(uint8_t length, int corrupt_byte = -1) {
  std::vector<uint8_t> frame(64, 0x5A);
  frame[0] = length;
  const auto crc = esphome::elero::cc1101_crc16(frame.data(), 1 + length);
  frame[1 + length] = crc >> 8;
  frame[2 + length] = crc & 0xFF;
  if (corrupt_byte >= 0) frame[corrupt_byte] ^= 0x01;
  esphome::elero::cc1101_pn9_whiten(frame.data(), frame.size());
  return frame;
}
