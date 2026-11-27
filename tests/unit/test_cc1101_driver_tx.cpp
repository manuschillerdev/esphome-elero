#include <algorithm>
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

TEST_F(Cc1101DriverTxHelperTest, LargestSupportedGroupReachesHardwareTx) {
  esphome::spi::test_support::read_bytes = {0, 0, CC1101_MARCSTATE_RX, CC1101_MARCSTATE_RX,
                                           CC1101_PKTSTATUS_CCA, CC1101_MARCSTATE_TX};
  EleroCommand cmd{};
  cmd.type = packet::msg_type::BUTTON;
  cmd.num_dests = packet::GROUP_MAX_DESTS;
  for (uint8_t i = 0; i < cmd.num_dests; ++i) cmd.frame_data[i] = i + 1;
  uint8_t buffer[packet::FIFO_LENGTH]{};
  const size_t size = packet::build_command_packet(cmd, buffer);
  ASSERT_EQ(size, packet::MAX_PACKET_SIZE + 1u);
  EXPECT_TRUE(driver_.load_and_transmit(buffer, size));
  EXPECT_EQ(driver_.mode(), RadioMode::TX);
  const auto &writes = esphome::spi::test_support::writes;
  EXPECT_NE(std::search(writes.begin(), writes.end(), buffer, buffer + size), writes.end());
}

#include "elero/rf_transport.h"

namespace {
class OneRequest : public RfTransportPort {
 public:
  bool take_request(RfTaskRequest &request) override {
    if (taken) return false;
    taken = true;
    request.attempt = 19;
    return true;
  }
  bool publish_completion(const TxResult &result) override {
    results.push_back(result);
    return true;
  }
  void receive_frame(const uint8_t *, size_t) override {}
  bool taken{false};
  std::vector<TxResult> results;
};
}

TEST_F(Cc1101DriverTxHelperTest, ClearChannelAssessmentFromRequestToCompletion) {
  struct Scenario {
    const char *name;
    unsigned clear_at;
    bool reject_first_strobe;
    bool finish_before_poll;
  };
  for (const Scenario scenario : {Scenario{"clear", 0, false, false},
                                  Scenario{"busy then clear", 8, false, false},
                                  Scenario{"always busy", 1000, false, false},
                                  Scenario{"channel changed before STX", 0, true, false},
                                  Scenario{"short frame", 0, false, true}}) {
    SCOPED_TRACE(scenario.name);
    const auto clear_at = scenario.clear_at;
    unsigned strobes = 0;
    time_.reset();
    uint8_t state = CC1101_MARCSTATE_RX;
    unsigned transmissions = 0;
    unsigned busy_transmissions = 0;
    bool loaded = false;
    bool observed_idle_during_busy_rx = false;
    esphome::spi::test_support::transfer_handler = [&](uint8_t command) -> uint8_t {
      if (command == CC1101_SIDLE) {
        if (!loaded && time_.millis() < clear_at) observed_idle_during_busy_rx = true;
        state = CC1101_MARCSTATE_IDLE;
      }
      if (command == CC1101_SRX) state = CC1101_MARCSTATE_RX;
      if (command == CC1101_SFTX) loaded = false;
      if (command == (CC1101_TXFIFO | CC1101_WRITE_BURST)) loaded = true;
      if (command == CC1101_STX) {
        ++strobes;
        EXPECT_TRUE(loaded);
        const bool busy = time_.millis() < clear_at || (scenario.reject_first_strobe && strobes == 1);
        // TI CC1101 section 17.5 applies CCA only to an STX issued in RX.
        if (state == CC1101_MARCSTATE_IDLE || (state == CC1101_MARCSTATE_RX && !busy)) {
          busy_transmissions += busy;
          ++transmissions;
          state = scenario.finish_before_poll ? CC1101_MARCSTATE_RX : CC1101_MARCSTATE_TX;
          loaded = false;
        }
      }
      return 0;
    };
    esphome::spi::test_support::read_register = [&](uint8_t address) -> uint8_t {
      const auto reg = address & 0x3f;
      if (reg == CC1101_PKTSTATUS) return time_.millis() >= clear_at ? 0x10 : 0;
      if (reg == CC1101_TXBYTES) return loaded ? 28 : 0;
      if (reg != CC1101_MARCSTATE) return 0;
      const auto reported = state;
      if (state == CC1101_MARCSTATE_TX) state = CC1101_MARCSTATE_RX;
      return reported;
    };
    RfTransport transport(driver_, rx_ready_, tx_done_);
    OneRequest port;
    for (unsigned tick = 0; tick < 100; ++tick) {
      transport.step(port);
      time_.advance(1);
    }
    ASSERT_EQ(port.results.size(), 1u);
    EXPECT_EQ(port.results.front().attempt, 19u);
    EXPECT_EQ(port.results.front().success, clear_at < 100);
    EXPECT_EQ(busy_transmissions, 0u);
    EXPECT_FALSE(observed_idle_during_busy_rx);
    EXPECT_EQ(strobes, transmissions + (scenario.reject_first_strobe ? 1u : 0u));
    EXPECT_EQ(transmissions, clear_at < 100 ? 1u : 0u);
    EXPECT_EQ(state, CC1101_MARCSTATE_RX);
    EXPECT_EQ(driver_.mode(), RadioMode::RX);
    // A complete frame must still pass through the RX path after each case.
    auto &bytes = esphome::spi::test_support::read_bytes;
    bytes = {30, 30, 27};
    for (uint8_t n = 1; n <= 27; ++n) bytes.push_back(n);
    bytes.push_back(0x64);
    bytes.push_back(0x80);
    uint8_t received[64]{};
    ASSERT_EQ(driver_.read_fifo(received, sizeof(received)), 30u);
    EXPECT_EQ(received[0], 27);
    EXPECT_EQ(received[29], 0x80);
  }
}
