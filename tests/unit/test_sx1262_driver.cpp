#include <algorithm>
#include "radio_test_support.h"
#include "elero/semtech_tx_fsm.cpp"
#include "elero/sx1262_driver.cpp"
using namespace esphome::elero;
TEST_F(RadioDriverTest, Sx1262HealthReadsStatusAfterOpcode) {
  Sx1262Driver driver;
  time_.advance(packet::timing::RADIO_WATCHDOG_INTERVAL);
  // The opcode response is unspecified. Only the following byte says RX.
  esphome::spi::test_support::transfer_bytes = {0x00, 0x50};
  EXPECT_EQ(driver.check_health(), RadioHealth::OK);
  const auto &writes = esphome::spi::test_support::writes;
  ASSERT_GE(writes.size(), 2u);
  EXPECT_EQ(writes[0], sx1262::GET_STATUS);
  EXPECT_EQ(writes[1], 0x00);
}

TEST_F(RadioDriverTest, Sx1262DriverRejectsCorruptedFramesAndAcceptsEveryLength) {
  Sx1262Driver driver;
  for (uint8_t length = 27; length <= 57; ++length) {
    for (int corrupt : {-1, 0, 10, 1 + length, 2 + length}) {
      auto frame = make_radio_frame(length, corrupt);
      auto &responses = esphome::spi::test_support::transfer_bytes;
      responses = {0,0,0,2, 0,0,0, 0,0,0,180,180, 0,0, 0,0,0};
      responses.insert(responses.end(), frame.begin(), frame.end());
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

TEST_F(RadioDriverTest, Sx1262PllLockAndOscillatorErrorsRequireRecovery) {
  for (uint8_t error : {0x40, 0x20, 0x04}) {
    Sx1262Driver driver;
    time_.advance(packet::timing::RADIO_WATCHDOG_INTERVAL);
    esphome::spi::test_support::transfer_bytes = {
        0, 0x50, // GET_STATUS
        0, 0, 0, 0, // GET_IRQ_STATUS
        0, 0, 0, error // GET_DEVICE_ERRORS
    };
    EXPECT_EQ(driver.check_health(), error == 0x04 ? RadioHealth::OK : RadioHealth::STUCK);
  }
}

TEST_F(RadioDriverTest, Sx1262BusyTimeoutBeforeFifoReadLeavesOutputUntouched) {
  Sx1262Driver driver;
  esphome::InternalGPIOPin busy;
  busy.read = [&] {
    if (esphome::spi::test_support::writes.size() < 14) return false;
    time_.advance(30);
    return true;
  };
  driver.set_busy_pin(&busy);
  esphome::spi::test_support::transfer_bytes = {0,0,0,2, 0,0,0, 0,0,0,180,180, 0,0};
  uint8_t output[64];
  std::fill(std::begin(output), std::end(output), 0xA5);
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 0u);
  EXPECT_TRUE(std::all_of(std::begin(output), std::end(output), [](uint8_t b) { return b == 0xA5; }));

}

TEST_F(RadioDriverTest, Sx1262InitStopsAtEachConfigurationBusyFailure) {
  for (unsigned fail_at = 1; fail_at <= 39; ++fail_at) {
    Sx1262Driver driver;
    esphome::spi::test_support::reset();
    esphome::spi::test_support::transfer_bytes = {0,0,0,0,0,0,0,0,0xA5};
    esphome::InternalGPIOPin busy;
    unsigned calls = 0;
    busy.read = [&] {
      if (++calls < fail_at) return false;
      time_.advance(30);
      return true;
    };
    driver.set_busy_pin(&busy);
    EXPECT_FALSE(driver.init()) << "failure at BUSY check " << fail_at;
    EXPECT_EQ(calls, fail_at);
  }
}

TEST_F(RadioDriverTest, Sx1262InitRequiresVerifiedRx) {
  for (uint8_t status : {0x20, 0x50, 0xFF}) {
    Sx1262Driver driver;
    esphome::spi::test_support::reset();
    esphome::spi::test_support::transfer_bytes = {0,0,0,0,0,0,0,0,0xA5};
    esphome::InternalGPIOPin busy;
    unsigned calls = 0;
    busy.read = [&] {
      if (++calls == 40) {
        esphome::spi::test_support::transfer_bytes = {0, status};
      }
      return false;
    };
    driver.set_busy_pin(&busy);
    EXPECT_EQ(driver.init(), status == 0x50);
  }
}

TEST_F(RadioDriverTest, Sx1262ShortFrameCompletesWithoutRxDoneOrTransmittedPadding) {
  Sx1262Driver driver;
  auto &responses = esphome::spi::test_support::transfer_bytes;
  responses = {0,0,0,sx1262::IRQ_SYNCWORD_VALID, 0,0,0, 0,0,0,180,180};
  uint8_t output[64]{};
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 0u);
  EXPECT_TRUE(driver.receiving());
  time_.advance(7);
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 0u);
  EXPECT_TRUE(responses.empty()); // No premature buffer read.
  time_.advance(1);
  auto frame = make_radio_frame(27);
  std::fill(frame.begin() + 30, frame.end(), 0); // Unwritten radio RAM, not air padding.
  responses = {0,0, 0,0,0};
  responses.insert(responses.end(), frame.begin(), frame.end());
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 30u);
  EXPECT_FALSE(driver.receiving());
  EXPECT_EQ(output[29], 0x80);
}

TEST_F(RadioDriverTest, Sx1262TruncatedCaptureDoesNotReportSuccess) {
  Sx1262Driver driver;
  auto frame = make_radio_frame(30);
  std::fill(frame.begin() + 31, frame.end(), 0); // Both wire CRC bytes missing.
  auto &responses = esphome::spi::test_support::transfer_bytes;
  responses = {0,0,0,sx1262::IRQ_SYNCWORD_VALID, 0,0,0, 0,0,0,180,180};
  uint8_t output[64]{};
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 0u);
  time_.advance(8);
  responses = {0,0, 0,0,0};
  responses.insert(responses.end(), frame.begin(), frame.end());
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 0u);
  EXPECT_FALSE(driver.receiving());
}

TEST_F(RadioDriverTest, Sx1262FastReplyProvesPostTxRxReadiness) {
  Sx1262Driver driver;
  esphome::spi::test_support::transfer_bytes = {
      0,0,0,sx1262::IRQ_SYNCWORD_VALID, 0,0,0, 0,0,0,180,180};
  uint8_t output[64]{};
  EXPECT_EQ(driver.read_fifo(output, sizeof(output)), 0u);
  auto &phases = static_cast<SemtechTxFsmOwner &>(driver);
  EXPECT_EQ(phases.tx_wait_rx_ready_for_fsm(), SemtechTxPhaseResult::Succeeded);
  EXPECT_TRUE(driver.receiving()); // Hub still defers the next TX.
}

TEST_F(RadioDriverTest, ReviewCompletedSingleRxMustNotTriggerRecovery) {
  Sx1262Driver driver;
  time_.advance(packet::timing::RADIO_WATCHDOG_INTERVAL);
  esphome::spi::test_support::transfer_bytes = {
    0,0x20, 0,0,0,sx1262::IRQ_RX_DONE, 0,0,0,0
  };
  EXPECT_EQ(driver.check_health(), RadioHealth::OK);
}

TEST_F(RadioDriverTest, Sx1262StandbyWithoutRxEventStillRequiresRecovery) {
  Sx1262Driver driver;
  time_.advance(packet::timing::RADIO_WATCHDOG_INTERVAL);
  esphome::spi::test_support::transfer_bytes = {0,0x20, 0,0,0,0, 0,0,0,0};
  EXPECT_EQ(driver.check_health(), RadioHealth::STUCK);
}

TEST_F(RadioDriverTest, Sx1262RecoveryChecksEveryRestoreOperationBeforeAcceptingRx) {
  // Eight restore operations plus the final GET_STATUS BUSY check.
  for (unsigned fail_at = 1; fail_at <= 9; ++fail_at) {
    Sx1262Driver driver;
    esphome::spi::test_support::reset();
    esphome::InternalGPIOPin busy;
    unsigned calls = 0;
    busy.read = [&] {
      ++calls;
      if (calls == fail_at) {
        time_.advance(30);
        return true;
      }
      // A failed soft restore must run init, including its SPI write proof.
      if (calls == fail_at + 1)
        esphome::spi::test_support::transfer_bytes = {0,0,0,0,0,0,0,0,0xA5};
      if (calls == fail_at + 40)
        esphome::spi::test_support::transfer_bytes = {0, 0x50};
      return false;
    };
    driver.set_busy_pin(&busy);
    driver.recover();
    EXPECT_EQ(calls, fail_at + 41) << fail_at;
    EXPECT_FALSE(driver.failed());
  }
}

TEST_F(RadioDriverTest, Sx1262SuccessfulRecoveryClearsConsecutiveFailures) {
  Sx1262Driver driver;
  esphome::InternalGPIOPin busy;
  bool fail = true;
  unsigned calls = 0;
  busy.read = [&] {
    if (fail) {
      time_.advance(30);
      return true;
    }
    if (++calls == 9) esphome::spi::test_support::transfer_bytes = {0, 0x50};
    return false;
  };
  driver.set_busy_pin(&busy);
  for (int cycle = 0; cycle < 4; ++cycle) {
    fail = true;
    driver.recover();
    driver.recover();
    EXPECT_FALSE(driver.failed());
    fail = false;
    calls = 0;
    driver.recover();
    EXPECT_FALSE(driver.failed());
  }
  fail = true;
  driver.recover();
  driver.recover();
  EXPECT_FALSE(driver.failed());
  driver.recover();
  EXPECT_TRUE(driver.failed());
}

TEST_F(RadioDriverTest, Sx1262LargestSupportedGroupReachesHardwareTx) {
  Sx1262Driver driver;
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
