#include <gtest/gtest.h>
#include <deque>
#include <vector>
#include "elero/rf_transport.h"
#define ESP_LOGV(tag, format, ...) ((void)0)
#define ESP_LOGVV(tag, format, ...) ((void)0)
#define ESP_LOGD(tag, format, ...) ((void)0)
#define ESP_LOGE(tag, format, ...) ((void)0)
#define ESP_LOGW(tag, format, ...) ((void)0)
#include "elero/command_sender.h"

using namespace esphome::elero;
namespace {
class FakeRadio : public RadioDriver {
 public:
  std::deque<std::vector<uint8_t>> frames;
  std::vector<std::vector<uint8_t>> sent;
  TxPollResult result{TxPollResult::SUCCESS};
  RadioHealth health{RadioHealth::OK};
  bool partial{false};
  bool accept{true};
  unsigned reads{0}, polls{0}, health_checks{0}, recoveries{0}, frequencies{0};
  bool init() override { return true; }
  void reset() override {}
  bool load_and_transmit(const uint8_t *data, size_t size) override {
    EXPECT_FALSE(partial);
    EXPECT_TRUE(frames.empty());
    sent.emplace_back(data, data + size);
    return accept;
  }
  TxPollResult poll_tx() override { ++polls; return result; }
  void abort_tx() override {}
  bool has_data() override { EXPECT_FALSE(failed()); return partial || !frames.empty(); }
  bool receiving() const override { return partial; }
  size_t read_fifo(uint8_t *data, size_t capacity) override {
    ++reads;
    if (partial) return 0;
    auto frame = frames.front();
    frames.pop_front();
    EXPECT_LE(frame.size(), capacity);
    std::copy(frame.begin(), frame.end(), data);
    return frame.size();
  }
  RadioHealth check_health() override { ++health_checks; return health; }
  void recover() override { ++recoveries; health = RadioHealth::OK; }
  void set_frequency_regs(uint8_t, uint8_t, uint8_t) override { ++frequencies; }
  void dump_config() override {}
  const char *radio_name() const override { return "fake"; }
  int rx_sensitivity_dbm() const override { return -100; }
  void fail() { failed_ = true; }
};
class Port : public RfTransportPort {
 public:
  std::deque<RfTaskRequest> requests;
  std::deque<TxResult> completions;
  std::vector<std::vector<uint8_t>> received;
  bool take_request(RfTaskRequest &request) override {
    if (requests.empty()) return false;
    request = requests.front(); requests.pop_front(); return true;
  }
  bool publish_completion(const TxResult &result) override {
    if (completions.size() == 8) return false;
    completions.push_back(result); return true;
  }
  void receive_frame(const uint8_t *data, size_t size) override {
    received.emplace_back(data, data + size);
  }
  bool request_tx(TxClient *client, const EleroCommand &cmd) {
    if (requests.size() == 8) return false;
    RfTaskRequest request;
    request.cmd = cmd;
    request.client = client;
    request.attempt = client->begin_tx_attempt();
    requests.push_back(request);
    return true;
  }
  void enqueue(uint32_t attempt, TxClient *client = nullptr) {
    ASSERT_LT(requests.size(), 8u);
    RfTaskRequest request;
    request.cmd.type = packet::msg_type::BUTTON;
    request.cmd.counter = attempt;
    request.client = client;
    request.attempt = attempt;
    requests.push_back(request);
  }
};
class Client : public TxClient {
 public:
  unsigned calls{0};
  void on_tx_complete(bool success) override { EXPECT_TRUE(success); ++calls; }
};
class RfTransportTest : public ::testing::Test {
 protected:
  FakeRadio radio;
  Port port;
  std::atomic<bool> rx{false}, tx{false};
  RfTransport transport{radio, rx, tx};
  MockTimeProvider time;
  void SetUp() override { set_time_provider(&time); time.advance(20); }
  void TearDown() override { set_time_provider(nullptr); }
};
}

TEST_F(RfTransportTest, SaturationRetainsResultsAndContinuesRxAndRecovery) {
  for (uint32_t attempt = 1; attempt <= 8; ++attempt) port.enqueue(attempt);
  transport.step(port);
  port.enqueue(9);
  transport.step(port);
  port.enqueue(10);
  for (int i = 0; i < 20; ++i) transport.step(port);
  ASSERT_EQ(port.completions.size(), 8u);
  ASSERT_EQ(radio.sent.size(), 9u);
  ASSERT_EQ(port.requests.size(), 1u);
  radio.frames = {{1}, {}, {2}}; // CRC rejection between two valid frames
  radio.health = RadioHealth::STUCK;
  transport.step(port);
  EXPECT_EQ(port.received, (std::vector<std::vector<uint8_t>>{{1}, {2}}));
  EXPECT_EQ(radio.recoveries, 1u);
  EXPECT_EQ(radio.sent.size(), 9u);
  for (uint32_t expected = 1; expected <= 10; ++expected) {
    ASSERT_FALSE(port.completions.empty());
    EXPECT_EQ(port.completions.front().attempt, expected);
    EXPECT_TRUE(port.completions.front().success);
    port.completions.pop_front();
    transport.step(port);
  }
  EXPECT_TRUE(port.completions.empty());
  EXPECT_EQ(radio.sent.size(), 10u);
}

TEST_F(RfTransportTest, DriverCountsOverflowAndWatchdogRecoveryOnce) {
  radio.recover();  // Startup also restores receive mode.
  EXPECT_EQ(radio.watchdog_count(), 0u);
  radio.health = RadioHealth::FIFO_OVERFLOW;
  transport.step(port);
  transport.step(port);
  EXPECT_EQ(radio.overflow_count(), 1u);
  EXPECT_EQ(radio.watchdog_count(), 1u);
}

TEST_F(RfTransportTest, PartialReceiveDefersTxAndWatchdogUntilFrameCompletes) {
  port.enqueue(1);
  radio.partial = true;
  radio.health = RadioHealth::STUCK;
  for (int i = 0; i < 10; ++i) transport.step(port);
  EXPECT_TRUE(radio.sent.empty());
  EXPECT_EQ(radio.health_checks, 0u);
  radio.partial = false;
  radio.frames = {{1, 2, 3}, {}, {4, 5, 6}};
  transport.step(port);
  EXPECT_EQ(port.received.size(), 2u);
  EXPECT_EQ(radio.sent.size(), 1u);
}

TEST_F(RfTransportTest, BurstDrainingIsBoundedAndQueuedTxEventuallyProgresses) {
  port.enqueue(1);
  for (int i = 0; i < 23; ++i) radio.frames.push_back({static_cast<uint8_t>(i)});
  transport.step(port);
  EXPECT_EQ(radio.reads, 8u); // Two bounded drain passes.
  EXPECT_TRUE(radio.sent.empty());
  transport.step(port);
  transport.step(port);
  EXPECT_EQ(port.received.size(), 23u);
  transport.step(port);
  EXPECT_EQ(radio.sent.size(), 1u);
}

TEST_F(RfTransportTest, FailedDriverCompletesActiveAndQueuedRequestsWithoutHardwarePolling) {
  radio.result = TxPollResult::PENDING;
  port.enqueue(1);
  port.enqueue(2);
  transport.step(port);
  radio.fail();
  EXPECT_TRUE(transport.step(port));
  transport.step(port);
  transport.step(port);
  ASSERT_EQ(port.completions.size(), 2u);
  EXPECT_FALSE(port.completions[0].success);
  EXPECT_FALSE(port.completions[1].success);
  EXPECT_EQ(port.completions[0].attempt, 1u);
  EXPECT_EQ(port.completions[1].attempt, 2u);
  EXPECT_EQ(radio.polls, 1u);
}

TEST_F(RfTransportTest, CancelledCompletionCannotCompleteNewAttempt) {
  Client client;
  const auto old_attempt = client.begin_tx_attempt();
  port.enqueue(old_attempt, &client);
  transport.step(port);
  client.invalidate_tx_attempt();
  const auto new_attempt = client.begin_tx_attempt();
  port.enqueue(new_attempt, &client);
  transport.step(port);
  auto result = port.completions.front(); port.completions.pop_front();
  result.client->complete_tx_attempt(result.attempt, result.success);
  EXPECT_EQ(client.calls, 0u);
  transport.step(port);
  result = port.completions.front();
  result.client->complete_tx_attempt(result.attempt, result.success);
  EXPECT_EQ(client.calls, 1u);
}

TEST_F(RfTransportTest, RawSelectorListAndTailReachTheRadio) {
  port.enqueue(1);
  auto &cmd = port.requests.front().cmd;
  cmd.type = packet::msg_type::BUTTON_GROUP;
  cmd.channel = 0xff;
  cmd.payload[1] = 0x03;
  cmd.payload[4] = packet::command::UP;
  const uint8_t selectors[] = {0x11, 0x22, 0x03};
  const uint8_t tail[] = {0xd1, 0xd2};
  ASSERT_TRUE(cmd.set_frame_data(selectors, tail));

  transport.step(port);
  ASSERT_EQ(radio.sent.size(), 1u);
  const auto &frame = radio.sent.front();
  ASSERT_EQ(frame.size(), 32u);
  EXPECT_EQ(frame[0], 31);
  EXPECT_EQ(frame[2], 0x45);
  EXPECT_EQ(frame[6], 0xff);
  EXPECT_EQ(frame[16], 3);
  EXPECT_EQ(frame[17], 0x11);
  EXPECT_EQ(frame[18], 0x22);
  EXPECT_EQ(frame[19], 0x03);
  EXPECT_EQ(frame[21], 0x03);
  EXPECT_EQ(frame[30], 0xd1);
  EXPECT_EQ(frame[31], 0xd2);
}

TEST_F(RfTransportTest, RawButtonAndProgramPreserveExplicitChannelAndPrefix) {
  const uint8_t selectors[] = {1, 2};
  const uint8_t tail[] = {0xab};
  for (auto type : {packet::msg_type::BUTTON, packet::msg_type::PROGRAM}) {
    port.enqueue(1);
    auto &cmd = port.requests.front().cmd;
    cmd.type = type;
    cmd.channel = 0xff;
    cmd.payload[0] = 0x12;
    cmd.payload[1] = 0x34;
    ASSERT_TRUE(cmd.set_frame_data(selectors, tail));
    transport.step(port);
    ASSERT_FALSE(radio.sent.empty());
    const auto &frame = radio.sent.back();
    ASSERT_EQ(frame.size(), 30u);
    EXPECT_EQ(frame[2], type);
    EXPECT_EQ(frame[6], 0xff);
    EXPECT_EQ(frame[17], 1);
    EXPECT_EQ(frame[18], 2);
    EXPECT_EQ(frame[19], 0x12);
    EXPECT_EQ(frame[20], 0x34);
    EXPECT_EQ(frame.back(), 0xab);
  }
}

TEST_F(RfTransportTest, CancelSelectorFromTemplateKeepsOtherSelectorAndTail) {
  CommandSender sender;
  auto &cmd = sender.command();
  cmd.type = packet::msg_type::BUTTON_GROUP;
  cmd.channel = 0xff;
  cmd.src_addr = 0x123456;
  const uint8_t selectors[] = {1, 2};
  const uint8_t tail[] = {0xab};
  ASSERT_TRUE(cmd.set_frame_data(selectors, tail));
  ASSERT_TRUE(sender.enqueue(packet::command::UP, 1, cmd.type));
  sender.cancel_target(cmd.src_addr, 2, 0);
  sender.process_queue(100, &port, "test");
  transport.step(port);
  ASSERT_EQ(radio.sent.size(), 1u);
  const auto &frame = radio.sent.front();
  ASSERT_EQ(frame.size(), 29u);
  EXPECT_EQ(frame[6], 0xff);
  EXPECT_EQ(frame[16], 1);
  EXPECT_EQ(frame[17], 1);
  EXPECT_EQ(frame.back(), 0xab);
}

TEST_F(RfTransportTest, InvalidGroupNeverReusesPreviousPacket) {
  port.enqueue(1);
  transport.step(port);
  port.enqueue(2);
  port.requests.front().cmd.num_dests = packet::GROUP_MAX_DESTS + 1;
  transport.step(port);
  transport.step(port);
  EXPECT_EQ(radio.sent.size(), 1u);
  ASSERT_EQ(port.completions.size(), 2u);
  EXPECT_FALSE(port.completions[1].success);
}

TEST_F(RfTransportTest, FrequencyChangeWaitsForActiveTxAndUnreadReply) {
  radio.result = TxPollResult::PENDING;
  port.enqueue(1);
  transport.step(port);
  RfTaskRequest frequency;
  frequency.type = RfTaskRequest::Type::REINIT_FREQ;
  frequency.freq = {1, 2, 3};
  port.requests.push_back(frequency);
  transport.step(port);
  EXPECT_EQ(radio.frequencies, 0u);
  radio.result = TxPollResult::SUCCESS;
  transport.step(port);
  radio.partial = true;
  transport.step(port);
  EXPECT_EQ(radio.frequencies, 0u);
  radio.partial = false;
  radio.frames = {{42}};
  transport.step(port);
  ASSERT_EQ(port.received.size(), 1u);
  EXPECT_EQ(radio.frequencies, 1u);
}

TEST_F(RfTransportTest, FullRequestQueueLeavesCommandPendingUntilCapacityReturns) {
  CommandSender sender;
  ASSERT_TRUE(sender.enqueue(packet::command::UP, 1));
  for (uint32_t attempt = 1; attempt <= 8; ++attempt) port.enqueue(attempt);
  for (int i = 0; i < 100; ++i) {
    sender.process_queue(time.millis(), &port, "test");
    time.advance(100);
  }
  EXPECT_EQ(sender.queue_size(), 1u);
  EXPECT_TRUE(radio.sent.empty());
  // Make room, then accept the same command and dispatch its actual completion.
  transport.step(port);
  sender.process_queue(time.millis(), &port, "test");
  for (int i = 0; i < 20; ++i) {
    transport.step(port);
    while (!port.completions.empty()) {
      const auto result = port.completions.front();
      port.completions.pop_front();
      if (result.client) result.client->complete_tx_attempt(result.attempt, result.success);
    }
  }
  EXPECT_EQ(radio.sent.size(), 9u);
  EXPECT_EQ(sender.queue_size(), 0u);
}
