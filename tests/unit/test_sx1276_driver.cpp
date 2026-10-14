#include <algorithm>
#include "radio_test_support.h"
#include "elero/semtech_tx_fsm.cpp"
#include "elero/sx1276_driver.cpp"
using namespace esphome::elero;

TEST_F(RadioDriverTest, Sx1276DriverRejectsCorruptedFramesAndAcceptsEveryLength) {
  Sx1276Driver driver;
  for (uint8_t length = 27; length <= 57; ++length) {
    for (int corrupt : {-1, 0, 10, 1 + length, 2 + length}) {
      auto frame = make_radio_frame(length, corrupt);
      auto &responses = esphome::spi::test_support::transfer_bytes;
      responses = {0, sx1276::IRQ2_PAYLOAD_READY, 0,180,
                   0,1, 0,0, 0,sx1276::IRQ1_MODE_READY, 0,sx1276::MODE_STANDBY};
      frame.resize(length + 3);
      for (uint8_t byte : frame) {
        responses.insert(responses.end(), {0,0, 0,byte});
      }
      responses.insert(responses.end(), {0,sx1276::IRQ2_FIFO_EMPTY});
      uint8_t output[64]{};
      const auto received = driver.read_fifo(output, sizeof(output));
      if (corrupt < 0) {
        EXPECT_EQ(received, length + 3u);
        EXPECT_EQ(output[0], length);
        EXPECT_EQ(output[length + 2], 0x80);
      } else {
        EXPECT_EQ(received, 0u) << "length=" << int(length) << " corrupt=" << corrupt;
      }
    }
  }
}

TEST_F(RadioDriverTest, Sx1276ShortFrameCompletesWithoutPayloadReadyOrPadding) {
  Sx1276Driver driver;
  auto &responses = esphome::spi::test_support::transfer_bytes;
  responses = {0,0, 0,sx1276::IRQ1_SYNC_ADDRESS_MATCH, 0,180};
  uint8_t output[64]{};
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 0u);
  EXPECT_TRUE(driver.receiving());
  time_.advance(7);
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 0u);
  EXPECT_TRUE(responses.empty());
  time_.advance(1);
  auto frame = make_radio_frame(27);
  frame.resize(30); // Exactly the bytes on air, no padding.
  responses = {0,1, 0,0, 0,sx1276::IRQ1_MODE_READY, 0,sx1276::MODE_STANDBY};
  for (uint8_t byte : frame) responses.insert(responses.end(), {0,0, 0,byte});
  responses.insert(responses.end(), {0,sx1276::IRQ2_FIFO_EMPTY});
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 30u);
  EXPECT_FALSE(driver.receiving());
  EXPECT_EQ(output[29], 0x80);
}

TEST_F(RadioDriverTest, Sx1276TruncatedCaptureDoesNotReadPastFifoEmpty) {
  Sx1276Driver driver;
  auto &responses = esphome::spi::test_support::transfer_bytes;
  responses = {0,0, 0,sx1276::IRQ1_SYNC_ADDRESS_MATCH, 0,180};
  uint8_t output[64]{};
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 0u);
  time_.advance(8);
  auto frame = make_radio_frame(30);
  frame.resize(31); // Missing both CRC bytes.
  responses = {0,1, 0,0, 0,sx1276::IRQ1_MODE_READY, 0,sx1276::MODE_STANDBY};
  for (uint8_t byte : frame) responses.insert(responses.end(), {0,0, 0,byte});
  responses.insert(responses.end(), {0,sx1276::IRQ2_FIFO_EMPTY});
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 0u);
  EXPECT_FALSE(driver.receiving());
}

TEST_F(RadioDriverTest, Sx1276FastReplyProvesPostTxRxReadiness) {
  Sx1276Driver driver;
  esphome::spi::test_support::transfer_bytes = {
      0,0, 0,sx1276::IRQ1_SYNC_ADDRESS_MATCH, 0,180};
  uint8_t output[64]{};
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 0u);
  auto &phases = static_cast<SemtechTxFsmOwner &>(driver);
  EXPECT_EQ(phases.tx_wait_rx_ready_for_fsm(), SemtechTxPhaseResult::Succeeded);
  EXPECT_TRUE(driver.receiving());
}

TEST_F(RadioDriverTest, ReviewInitMustRejectReceiverNotReady) {
  Sx1276Driver driver;
  esphome::spi::test_support::transfer_bytes = {0,0, 0,0x12, 0,0, 0,0, 0,0x80, 0,sx1276::MODE_STANDBY};
  // Valid version and initial standby readiness; final RX mode/readiness reads zero.
  EXPECT_FALSE(driver.init());
}

// Register model for mode/readiness transitions. No precomputed SPI ordering:
// RX PLL lock can settle late or fail while configuration reads remain valid.
class Sx1276RegisterModel {
 public:
  uint8_t mode{sx1276::MODE_STANDBY};
  unsigned rx_polls{0};
  unsigned version_reads{0};
  unsigned lock_after{2};
  bool standby_ready{true};

  uint8_t transfer(uint8_t value) {
    if (!data_byte_) {
      address_ = value;
      data_byte_ = true;
      return 0;
    }
    data_byte_ = false;
    const uint8_t address = address_ & 0x7F;
    if (address_ & sx1276::SPI_WRITE) {
      if (address == sx1276::REG_OP_MODE) {
        mode = value & sx1276::MODE_MASK;
        if (mode == sx1276::MODE_RX) rx_polls = 0;
      }
      return 0;
    }
    if (address == sx1276::REG_VERSION) { ++version_reads; return sx1276::EXPECTED_VERSION; }
    if (address == sx1276::REG_OP_MODE) return mode;
    if (address == sx1276::REG_IRQ_FLAGS1) {
      if (mode == sx1276::MODE_RX) {
        return sx1276::IRQ1_MODE_READY |
               (++rx_polls >= lock_after ? sx1276::IRQ1_PLL_LOCK : 0);
      }
      return standby_ready ? sx1276::IRQ1_MODE_READY : 0;
    }
    return 0;
  }
 private:
  bool data_byte_{false};
  uint8_t address_{0};
};

TEST_F(RadioDriverTest, Sx1276InitWaitsForPllLock) {
  Sx1276RegisterModel radio;
  esphome::spi::test_support::transfer_handler = [&](uint8_t value) { return radio.transfer(value); };
  Sx1276Driver driver;
  EXPECT_TRUE(driver.init());
  EXPECT_EQ(radio.rx_polls, 2u);
}

TEST_F(RadioDriverTest, Sx1276RecoveryAllowsPllToSettleWithoutReset) {
  Sx1276RegisterModel radio;
  esphome::spi::test_support::transfer_handler = [&](uint8_t value) { return radio.transfer(value); };
  Sx1276Driver driver;
  driver.recover();
  EXPECT_FALSE(driver.failed());
  EXPECT_EQ(radio.rx_polls, 2u);
  EXPECT_EQ(radio.version_reads, 0u); // Soft recovery sufficed.
}

TEST_F(RadioDriverTest, Sx1276RecoveryFailsWhenResetCannotRestorePllLock) {
  Sx1276RegisterModel radio;
  radio.lock_after = 100000;
  esphome::spi::test_support::transfer_handler = [&](uint8_t value) { return radio.transfer(value); };
  Sx1276Driver driver;
  driver.recover();
  EXPECT_TRUE(driver.failed());
  EXPECT_EQ(radio.version_reads, 1u);
}

TEST_F(RadioDriverTest, Sx1276InitPropagatesStandbyTimeout) {
  Sx1276RegisterModel radio;
  radio.standby_ready = false;
  esphome::spi::test_support::transfer_handler = [&](uint8_t value) { return radio.transfer(value); };
  Sx1276Driver driver;
  EXPECT_FALSE(driver.init());
  EXPECT_EQ(radio.rx_polls, 0u);
}

TEST_F(RadioDriverTest, Sx1276LargestSupportedGroupReachesHardwareTx) {
  Sx1276Driver driver;
  esphome::spi::test_support::transfer_bytes = {0,0, 0,0, 0,sx1276::IRQ1_MODE_READY, 0,sx1276::MODE_STANDBY};
  EleroCommand cmd{};
  cmd.type = packet::msg_type::BUTTON;
  cmd.num_dests = packet::GROUP_MAX_DESTS;
  for (uint8_t i = 0; i < cmd.num_dests; ++i) cmd.dest_channels[i] = i + 1;
  uint8_t buffer[packet::FIFO_LENGTH]{};
  const size_t size = packet::build_command_packet(cmd, buffer);
  ASSERT_EQ(size, packet::MAX_PACKET_SIZE + 1u);
  EXPECT_TRUE(driver.load_and_transmit(buffer, size));
  EXPECT_EQ(driver.mode(), RadioMode::TX);
  const auto crc = cc1101_crc16(buffer, size);
  buffer[size] = crc >> 8;
  buffer[size + 1] = crc & 0xFF;
  cc1101_pn9_whiten(buffer, size + 2);
  const auto &writes = esphome::spi::test_support::writes;
  EXPECT_NE(std::search(writes.begin(), writes.end(), buffer, buffer + size + 2), writes.end());
}
