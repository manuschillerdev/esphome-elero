#include <gtest/gtest.h>

#include <atomic>

#define ESP_LOGV(tag, format, ...) ((void) 0)
#define ESP_LOGVV(tag, format, ...) ((void) 0)
#define ESP_LOGD(tag, format, ...) ((void) 0)
#define ESP_LOGI(tag, format, ...) ((void) 0)
#define ESP_LOGW(tag, format, ...) ((void) 0)
#define ESP_LOGE(tag, format, ...) ((void) 0)
#define ESP_LOGCONFIG(tag, format, ...) ((void) 0)

#ifndef UNIT_TEST
#define UNIT_TEST
#endif

#include "elero/time_provider.h"

namespace esphome {

uint32_t millis() { return esphome::elero::get_time_provider().millis(); }
void delay(uint32_t) {}
void delay_microseconds_safe(uint32_t) {}

}  // namespace esphome

extern "C" void esp_rom_delay_us(uint32_t) {}

#include "elero/time_provider.cpp"
#include "elero/cc1101_tx_fsm.cpp"
#include "elero/cc1101_driver.cpp"

using namespace esphome::elero;

class Cc1101DriverTxHelperTest : public ::testing::Test {
 protected:
  MockTimeProvider time_;
  CC1101Driver driver_;
  std::atomic<bool> rx_ready_{false};
  std::atomic<bool> tx_done_{false};

  void SetUp() override {
    set_time_provider(&time_);
    time_.reset();
    tx_done_.store(false, std::memory_order_release);
    rx_ready_.store(false, std::memory_order_release);
    driver_.set_irq_flags(&rx_ready_, &tx_done_);
    esphome::spi::test_support::reset();
  }

  void TearDown() override {
    esphome::spi::test_support::reset();
    set_time_provider(nullptr);
  }
};

TEST_F(Cc1101DriverTxHelperTest, WaitTxDoneSucceedsViaMarcstateFallbackWhenIrqMissed) {
  esphome::spi::test_support::read_bytes = {
      CC1101_MARCSTATE_RX,
      0x00,
      0x00,
  };

  driver_.tx_on_state_enter_for_fsm(Cc1101TxState::WaitTxDone, time_.millis());

  EXPECT_EQ(driver_.tx_wait_done_for_fsm(), Cc1101TxPhaseResult::Succeeded);
}

TEST_F(Cc1101DriverTxHelperTest, WaitTxDoneFailsWhenIrqFiresButTxbytesRemainNonZero) {
  tx_done_.store(true, std::memory_order_release);
  esphome::spi::test_support::read_bytes = {
      0x01,
      0x01,
      0x01,
      0x01,
  };

  driver_.tx_on_state_enter_for_fsm(Cc1101TxState::WaitTxDone, time_.millis());

  EXPECT_EQ(driver_.tx_wait_done_for_fsm(), Cc1101TxPhaseResult::Failed);
}

TEST_F(Cc1101DriverTxHelperTest, ReturnToRxStaysPendingWhenMarcstateIsTxEnd) {
  esphome::spi::test_support::read_bytes = {
      CC1101_MARCSTATE_TX_END,
  };

  driver_.tx_set_mode_for_fsm(RadioMode::TX);
  driver_.tx_on_state_enter_for_fsm(Cc1101TxState::ReturnToRx, time_.millis());

  EXPECT_EQ(driver_.tx_return_to_rx_for_fsm(), Cc1101TxPhaseResult::Pending);
  EXPECT_EQ(driver_.mode(), RadioMode::TX);
}

TEST_F(Cc1101DriverTxHelperTest, ReturnToRxFailsWhenTransientStateTimesOut) {
  esphome::spi::test_support::read_bytes = {
      CC1101_MARCSTATE_RXTX_SWITCH,
  };

  driver_.tx_set_mode_for_fsm(RadioMode::TX);
  driver_.tx_on_state_enter_for_fsm(Cc1101TxState::ReturnToRx, time_.millis());
  time_.advance(26);

  EXPECT_EQ(driver_.tx_return_to_rx_for_fsm(), Cc1101TxPhaseResult::Failed);
  EXPECT_EQ(driver_.mode(), RadioMode::TX);
}

TEST_F(Cc1101DriverTxHelperTest, ReturnToRxPreservesRxBytesForLaterDrain) {
  esphome::spi::test_support::read_bytes = {
      CC1101_MARCSTATE_RX,
      0x02,
      0x02,
      CC1101_MARCSTATE_RX,
  };

  driver_.tx_set_mode_for_fsm(RadioMode::TX);

  EXPECT_EQ(driver_.tx_return_to_rx_for_fsm(), Cc1101TxPhaseResult::Succeeded);
  EXPECT_TRUE(rx_ready_.load(std::memory_order_acquire));
  EXPECT_EQ(driver_.mode(), RadioMode::RX);
}

TEST_F(Cc1101DriverTxHelperTest, ReturnToRxFailsWhenMarcstateIsClearlyBad) {
  esphome::spi::test_support::read_bytes = {
      CC1101_MARCSTATE_TX,
  };

  EXPECT_EQ(driver_.tx_return_to_rx_for_fsm(), Cc1101TxPhaseResult::Failed);
}

TEST_F(Cc1101DriverTxHelperTest, PartialResponseSurvivesAcrossFifoReads) {
  uint8_t output[64]{};
  // Header has arrived but the rest of the frame is still on air.
  esphome::spi::test_support::read_bytes = {2, 2, 0x1D};
  EXPECT_EQ(driver_.read_fifo(output, sizeof(output)), 0u);
  // Only the length byte was consumed; the remaining 31 bytes arrive later.
  esphome::spi::test_support::read_bytes = {31, 31};
  for (uint8_t i = 1; i <= 31; ++i)
    esphome::spi::test_support::read_bytes.push_back(i == 31 ? 0x80 : i);
  EXPECT_EQ(driver_.read_fifo(output, sizeof(output)), 32u);
  EXPECT_EQ(output[0], 0x1D);
  EXPECT_EQ(output[1], 1);
  EXPECT_EQ(output[31], 0x80);
}

TEST_F(Cc1101DriverTxHelperTest, CompleteFrameDoesNotConsumeNextFramePrefix) {
  uint8_t output[64]{};
  esphome::spi::test_support::read_bytes = {34, 34, 0x1D};
  for (uint8_t i = 1; i <= 31; ++i)
    esphome::spi::test_support::read_bytes.push_back(i == 31 ? 0x80 : i);
  esphome::spi::test_support::read_bytes.push_back(0x1B);
  esphome::spi::test_support::read_bytes.push_back(0x42);
  EXPECT_EQ(driver_.read_fifo(output, sizeof(output)), 32u);
  ASSERT_EQ(esphome::spi::test_support::read_bytes.size(), 2u);
  EXPECT_EQ(esphome::spi::test_support::read_bytes.front(), 0x1B);
}

TEST_F(Cc1101DriverTxHelperTest, LoneLengthByteIsNotDrainedDuringReception) {
  esphome::spi::test_support::read_bytes = {1,1,0x1D};
  uint8_t output[64]{};
  EXPECT_EQ(driver_.read_fifo(output, sizeof(output)), 0u);
  ASSERT_EQ(esphome::spi::test_support::read_bytes.size(), 1u);
  EXPECT_EQ(esphome::spi::test_support::read_bytes.front(), 0x1D);
}

TEST_F(Cc1101DriverTxHelperTest, BadCrcIsRejectedWithoutFlushingFollowingFrame) {
  esphome::spi::test_support::read_bytes = {34,34,0x1D};
  for (int i = 0; i < 31; ++i) esphome::spi::test_support::read_bytes.push_back(0);
  esphome::spi::test_support::read_bytes.push_back(0x1B);
  esphome::spi::test_support::read_bytes.push_back(0x42);
  uint8_t output[64]{};
  EXPECT_EQ(driver_.read_fifo(output, sizeof(output)), 0u);
  EXPECT_FALSE(driver_.receiving());
  ASSERT_EQ(esphome::spi::test_support::read_bytes.size(), 2u);
  EXPECT_EQ(esphome::spi::test_support::read_bytes.front(), 0x1B);
}

TEST_F(Cc1101DriverTxHelperTest, TruncatedFrameReleasesRxOwnershipOnTimeout) {
  esphome::spi::test_support::read_bytes = {2,2,0x1D};
  uint8_t output[64]{};
  EXPECT_EQ(driver_.read_fifo(output, sizeof(output)), 0u);
  EXPECT_TRUE(driver_.receiving());
  time_.advance(21);
  esphome::spi::test_support::read_bytes = {1,1,CC1101_MARCSTATE_RX};
  EXPECT_EQ(driver_.read_fifo(output, sizeof(output)), 0u);
  EXPECT_FALSE(driver_.receiving());
}

#include "elero/radio_rx.h"
TEST_F(Cc1101DriverTxHelperTest, ReviewDrainMustReachGoodFrameBehindBadCrc) {
  auto &reads = esphome::spi::test_support::read_bytes;
  reads = {60,60, 60,60, 27}; // has_data, read_fifo count, length
  for (int i=0; i<29; ++i) reads.push_back(0); // bad CRC trailer
  reads.insert(reads.end(), {30,30, 30,30, 27});
  for (int i=0; i<29; ++i) reads.push_back(i == 28 ? 0x80 : 0);
  reads.insert(reads.end(), {0,0});
  unsigned decoded=0;
  uint8_t buf[64]{};
  const bool idle = drain_radio_rx(driver_, rx_ready_, buf, sizeof(buf), [&](size_t) { ++decoded; });
  EXPECT_EQ(decoded, 1u);
  EXPECT_TRUE(idle);
  EXPECT_TRUE(reads.empty());
}
// wait_rx polls at most 200 times. A ready response ends the wait immediately.
static void queue_rx_readiness(bool ready) {
  auto &reads = esphome::spi::test_support::read_bytes;
  if (ready) reads.push_back(CC1101_MARCSTATE_RX);
  else reads.insert(reads.end(), 200, CC1101_MARCSTATE_IDLE);
}

TEST_F(Cc1101DriverTxHelperTest, SuccessfulResetClearsConsecutiveRecoveryFailures) {
  for (unsigned cycle = 0; cycle < 4; ++cycle) {
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
      queue_rx_readiness(false);
      if (attempt == 2) queue_rx_readiness(true); // Full reset restores RX.
      driver_.recover();
      ASSERT_FALSE(driver_.failed());
      ASSERT_TRUE(esphome::spi::test_support::read_bytes.empty());
    }
  }
  // A single failed flush after these successes must not escalate to reset.
  queue_rx_readiness(false);
  const auto before = esphome::spi::test_support::writes.size();
  driver_.recover();
  EXPECT_FALSE(driver_.failed());
  const auto &writes = esphome::spi::test_support::writes;
  EXPECT_EQ(std::count(writes.begin() + before, writes.end(), CC1101_SRES), 0);
}

TEST_F(Cc1101DriverTxHelperTest, ConsecutiveFailedResetsDisableRadio) {
  for (unsigned attempt = 0; attempt < 5; ++attempt) {
    queue_rx_readiness(false);
    if (attempt >= 2) queue_rx_readiness(false);
    driver_.recover();
    EXPECT_EQ(driver_.failed(), attempt == 4);
    ASSERT_TRUE(esphome::spi::test_support::read_bytes.empty());
  }
}

TEST_F(Cc1101DriverTxHelperTest, InitRequiresReceiverReadiness) {
  for (bool ready : {false, true}) {
    esphome::spi::test_support::read_bytes = {0, 0x14, 0x08};
    queue_rx_readiness(ready);
    EXPECT_EQ(driver_.init(), ready);
    ASSERT_TRUE(esphome::spi::test_support::read_bytes.empty());
  }
}

TEST_F(Cc1101DriverTxHelperTest, WatchdogRecoveryWaitsThroughCalibration) {
  esphome::spi::test_support::read_bytes = {CC1101_MARCSTATE_IDLE, CC1101_MARCSTATE_RX};
  driver_.recover();
  EXPECT_FALSE(driver_.failed());
  EXPECT_TRUE(esphome::spi::test_support::read_bytes.empty());
}

TEST_F(Cc1101DriverTxHelperTest, DrainBudgetDefersTxWhileFramesRemain) {
  auto &reads = esphome::spi::test_support::read_bytes;
  for (uint8_t count : {20, 16, 12, 8}) {
    reads.insert(reads.end(), {count, count, count, count, 1, 0x42, 0, 0x80});
  }
  reads.insert(reads.end(), {4, 4}); // Data remains after the four-frame budget.
  uint8_t buffer[64]{};
  unsigned delivered = 0;
  EXPECT_FALSE(drain_radio_rx(driver_, rx_ready_, buffer, sizeof(buffer),
                             [&](size_t) { ++delivered; }));
  EXPECT_EQ(delivered, 4u);
  EXPECT_TRUE(reads.empty());
  reads = {4, 4, 4, 4, 1, 0x42, 0, 0x80, 0, 0, 0, 0};
  EXPECT_TRUE(drain_radio_rx(driver_, rx_ready_, buffer, sizeof(buffer),
                            [&](size_t) { ++delivered; }));
  EXPECT_EQ(delivered, 5u);
}

TEST_F(Cc1101DriverTxHelperTest, DrainDefersTxWhileFrameIsIncomplete) {
  esphome::spi::test_support::read_bytes = {2, 2, 2, 2, 27};
  uint8_t buffer[64]{};
  EXPECT_FALSE(drain_radio_rx(driver_, rx_ready_, buffer, sizeof(buffer),
                             [&](size_t) { FAIL() << "Incomplete frame delivered"; }));
  EXPECT_TRUE(driver_.receiving());
}

TEST_F(Cc1101DriverTxHelperTest, DrainStopsWhenReadPermanentlyFailsRadio) {
  struct FailingRadio : CC1101Driver {
    unsigned polls{0};
    bool has_data() override { ++polls; return true; }
    size_t read_fifo(uint8_t *, size_t) override { this->failed_ = true; return 0; }
  } radio;
  uint8_t buffer[64]{};
  EXPECT_TRUE(drain_radio_rx(radio, rx_ready_, buffer, sizeof(buffer),
                            [&](size_t) { FAIL() << "Failed read delivered data"; }));
  EXPECT_EQ(radio.polls, 1u);
}
