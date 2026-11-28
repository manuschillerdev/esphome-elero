#include <gtest/gtest.h>
#include <array>
#include <cstdio>

#include "elero_web/learn_in_request.h"

#include "elero/learn_in_manager.h"
#include "elero/time_provider.h"

namespace esphome::elero {
namespace {

class FakeHub {
 public:
  bool queue_accepts{true};
  TxClient *client{nullptr};
  uint32_t attempt{0};
  EleroCommand last_cmd{};
  int request_count{0};

  bool request_tx(TxClient *tx_client, const EleroCommand &cmd) {
    ++request_count;
    if (!queue_accepts) {
      return false;
    }
    client = tx_client;
    attempt = client->begin_tx_attempt();
    last_cmd = cmd;
    return true;
  }

  void complete(bool success) {
    ASSERT_NE(client, nullptr);
    TxClient *current = client;
    client = nullptr;
    current->complete_tx_attempt(attempt, success);
  }
};

class LearnInManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    set_time_provider(&mock_time_);
  }

  void TearDown() override {
    set_time_provider(nullptr);
  }

  // Fixture source: stanleypa/eleropy at 05058c5, eleroProtocol.generate_msg.
  // Shared remote 010203, receiver 040506, channel 1.
  bool start_web(const char *mode, bool confirm_all = false, uint32_t source = 0x010203) {
    JsonDocument json;
    auto root = json.to<JsonObject>();
    root["type"] = "learn_in_start";
    root["mode"] = mode;
    root["channel"] = 1;
    root["receiver_address"] = "0x040506";
    root["confirm_all_receivers"] = confirm_all;
    LearnInStartRequest request{source, 1};
    const auto saved = [](uint32_t receiver, uint32_t remote, uint8_t channel) {
      return receiver == 0x040506 && remote == 0x010203 && channel == 1;
    };
    return !parse_training_request(root, 0x010203, saved, request) && manager_.start(request);
  }

  void transmit(const char *expected) {
    for (int i = 0; i < packet::program::PACKETS; ++i) {
      manager_.loop(&hub_);
      ASSERT_NE(hub_.client, nullptr);
      uint8_t bytes[64]{};
      const auto size = packet::build_command_packet(hub_.last_cmd, bytes);
      std::string actual;
      for (size_t pos = 0; pos < size; ++pos) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", bytes[pos]);
        actual += hex;
      }
      EXPECT_EQ(actual, expected);
      hub_.complete(true);
      if (i + 1 < packet::program::PACKETS) mock_time_.advance(packet::button::INTER_PACKET_MS);
    }
  }

  void start_training(bool after_standby = false) {
    ASSERT_TRUE(start_web("train"));
    transmit(after_standby ? "1d02701000010101020301020301020301010000000383abdf5079afd3cd"
                          : "1d01701000010101020301020301020301010000000354f4aebc6cd5a492");
    mock_time_.advance(2099);
    manager_.loop(&hub_);
    ASSERT_EQ(hub_.client, nullptr);
    mock_time_.advance(1);
    transmit(after_standby ? "1d03f8100001010102030102030102030104050600000000000000000000"
                          : "1d02f8100001010102030102030102030104050600000000000000000000");
    mock_time_.advance(499);
    manager_.loop(&hub_);
    ASSERT_EQ(hub_.client, nullptr);
    mock_time_.advance(1);
    transmit(after_standby ? "1d047810000101010203010203010203010405060401dac895e3facd866f"
                          : "1d037810000101010203010203010203010405060401c3d62f3a498ec3b1");
    EXPECT_EQ(manager_.state(), LearnInState::WATCH_MOVEMENT);
  }

  MockTimeProvider mock_time_;
  LearnInManager manager_;
  FakeHub hub_;
};

TEST_F(LearnInManagerTest, StandbyPairAndRemoveGatewayChannel) {
  ASSERT_TRUE(start_web("open_standby"));
  transmit("1d016a1005010101020301020301020301040506000454f42fbc6cdea4b2");
  EXPECT_EQ(manager_.state(), LearnInState::STANDBY_SENT);
  start_training(true);

  // Synthetic D4 envelope with the same identity fields used by eleropy main.py.
  std::array<uint8_t, 30> reply{};
  reply[0] = 27;
  reply[packet::pkt_offset::TYPE] = 0xd4;
  reply[packet::pkt_offset::CHANNEL] = 1;
  packet::write_addr(reply.data() + packet::pkt_offset::SRC_ADDR, 0x040506);
  packet::write_addr(reply.data() + packet::pkt_offset::FWD_ADDR, 0x010203);
  reply[packet::pkt_offset::NUM_DESTS] = 1;
  reply.back() = packet::cc1101_status::CRC_OK_BIT;
  const auto parsed = packet::parse_packet(reply.data(), reply.size());
  ASSERT_TRUE(parsed.complete_frame);
  ASSERT_EQ(parsed.type, 0xd4);
  manager_.on_receiver_frame(reply);
  ASSERT_EQ(manager_.state(), LearnInState::WATCH_MOVEMENT);
  ASSERT_TRUE(manager_.confirm_down());
  transmit("1d057810000101010203010203010203010405060001aa821504aa7266be");
  ASSERT_TRUE(manager_.confirm_up());
  transmit("1d067810000101010203010203010203010405060001f05d971bd06d7b67");

  for (size_t offset : std::array<size_t, 8>{0, packet::pkt_offset::TYPE, packet::pkt_offset::CHANNEL, packet::pkt_offset::SRC_ADDR,
                      packet::pkt_offset::FWD_ADDR, packet::pkt_offset::NUM_DESTS, packet::pkt_offset::FIRST_DEST, reply.size() - 1}) {
    auto other = reply;
    other[offset] ^= 0x80;
    manager_.on_receiver_frame(other);
    EXPECT_EQ(manager_.state(), LearnInState::WATCH_MOVEMENT);
  }
  manager_.on_receiver_frame(reply);
  manager_.on_receiver_frame(reply);
  transmit("1d07781000010101020301020301020301040506080140b1e796c0f14b95");
  EXPECT_EQ(manager_.state(), LearnInState::ACK_SENT);
  EXPECT_FALSE(manager_.is_active());
  EXPECT_FALSE(start_web("remove_link"));
  EXPECT_FALSE(start_web("remove_link", true, 0x040506));
  ASSERT_TRUE(start_web("remove_link", true));
  transmit("1b084510000101010203010203010203010100017cef15298443fcc9");
  EXPECT_EQ(manager_.state(), LearnInState::REMOVAL_SENT);
}

TEST_F(LearnInManagerTest, MissingReplyTimesOutAndCancelStopsScheduledTraining) {
  start_training();
  mock_time_.advance(300000);
  manager_.loop(&hub_);
  EXPECT_EQ(manager_.state(), LearnInState::TIMED_OUT);
  ASSERT_TRUE(start_web("train"));
  manager_.loop(&hub_);
  ASSERT_NE(hub_.client, nullptr);
  manager_.cancel();
  hub_.complete(true);
  mock_time_.advance(3000);
  manager_.loop(&hub_);
  EXPECT_EQ(hub_.client, nullptr);
  EXPECT_EQ(manager_.state(), LearnInState::CANCELLED);
}

TEST_F(LearnInManagerTest, RejectsInvalidStartRequest) {
  LearnInStartRequest req{};
  EXPECT_FALSE(manager_.start(req));
  EXPECT_EQ(manager_.state(), LearnInState::IDLE);
}

TEST_F(LearnInManagerTest, StartDuringRegistryLoopUsesCurrentClock) {
  manager_.loop(&hub_);  // Idle at the start of the main loop.
  mock_time_.advance(1);
  ASSERT_TRUE(manager_.start({0x17A753, 5, 1}));  // Web request during registry processing.
  manager_.loop(&hub_);
  EXPECT_EQ(hub_.request_count, 1);
  EXPECT_EQ(manager_.state(), LearnInState::PROGRAMMING);
}

TEST_F(LearnInManagerTest, SessionTimeoutAtClockWrap) {
  mock_time_.current_time = UINT32_MAX - 99;
  LearnInStartRequest req{};
  req.src_addr = 1;
  req.channel = 1;
  req.session_timeout_ms = 100;
  ASSERT_TRUE(manager_.start(req));
  manager_.loop(&hub_);
  ASSERT_EQ(hub_.request_count, 1);
  mock_time_.advance(100);
  manager_.loop(&hub_);
  EXPECT_EQ(manager_.state(), LearnInState::TIMED_OUT);
}

TEST_F(LearnInManagerTest, FullLearnInFlowCompletes) {
  mock_time_.advance(25u * 86400000u);
  LearnInStartRequest req{};
  req.src_addr = 0x17A753;
  req.channel = 5;

  ASSERT_TRUE(manager_.start(req));
  EXPECT_EQ(manager_.state(), LearnInState::PROGRAMMING);

  for (int i = 0; i < packet::program::PACKETS; ++i) {
    manager_.loop(&hub_);
    ASSERT_EQ(hub_.last_cmd.type, packet::msg_type::PROGRAM);
    ASSERT_EQ(hub_.last_cmd.type2, packet::program::TYPE2);
    ASSERT_EQ(hub_.last_cmd.hop, packet::program::HOP);
    ASSERT_EQ(hub_.last_cmd.payload[4], packet::command::PROGRAM);
    hub_.complete(true);
    mock_time_.advance(packet::button::INTER_PACKET_MS);
  }
  EXPECT_EQ(manager_.state(), LearnInState::WAIT_UP);

  ASSERT_TRUE(manager_.confirm_up());
  for (int i = 0; i < packet::button::PACKETS; ++i) {
    manager_.loop(&hub_);
    ASSERT_EQ(hub_.last_cmd.type, packet::msg_type::BUTTON);
    ASSERT_EQ(hub_.last_cmd.type2, packet::button::TYPE2);
    ASSERT_EQ(hub_.last_cmd.hop, packet::button::HOP);
    ASSERT_EQ(hub_.last_cmd.payload[4], packet::command::UP);
    hub_.complete(true);
    mock_time_.advance(packet::button::INTER_PACKET_MS);
  }
  EXPECT_EQ(manager_.state(), LearnInState::WAIT_DOWN);

  ASSERT_TRUE(manager_.confirm_down());
  for (int i = 0; i < packet::button::PACKETS; ++i) {
    manager_.loop(&hub_);
    ASSERT_EQ(hub_.last_cmd.type, packet::msg_type::BUTTON);
    ASSERT_EQ(hub_.last_cmd.type2, packet::button::TYPE2);
    ASSERT_EQ(hub_.last_cmd.hop, packet::button::HOP);
    ASSERT_EQ(hub_.last_cmd.payload[4], packet::command::DOWN);
    hub_.complete(true);
    mock_time_.advance(packet::button::INTER_PACKET_MS);
  }
  EXPECT_EQ(manager_.state(), LearnInState::COMPLETE);
}

TEST_F(LearnInManagerTest, ConfirmOrderIsEnforced) {
  LearnInStartRequest req{};
  req.src_addr = 0x17A753;
  req.channel = 5;

  ASSERT_TRUE(manager_.start(req));
  EXPECT_FALSE(manager_.confirm_down());
  EXPECT_FALSE(manager_.confirm_up());
}

TEST_F(LearnInManagerTest, RetriesAndFailsAfterMaxRetries) {
  LearnInStartRequest req{};
  req.src_addr = 0x17A753;
  req.channel = 5;

  ASSERT_TRUE(manager_.start(req));

  manager_.loop(&hub_);
  for (int i = 0; i <= packet::limits::SEND_RETRIES; ++i) {
    hub_.complete(false);
    mock_time_.advance(packet::timing::MAX_BACKOFF_MS);
    manager_.loop(&hub_);
  }

  EXPECT_EQ(manager_.state(), LearnInState::FAILED);
}

TEST_F(LearnInManagerTest, TimesOutSession) {
  LearnInStartRequest req{};
  req.src_addr = 0x17A753;
  req.channel = 5;
  req.session_timeout_ms = 100;

  ASSERT_TRUE(manager_.start(req));
  mock_time_.advance(101);
  manager_.loop(&hub_);

  EXPECT_EQ(manager_.state(), LearnInState::TIMED_OUT);
}


TEST_F(LearnInManagerTest, LateCompletionCannotCompleteRetriedStep) {
  ASSERT_TRUE(manager_.start({0x17A753, 5, 1}));
  manager_.loop(&hub_);
  const auto old_attempt = hub_.attempt;
  mock_time_.advance(5000);
  hub_.complete(false);
  manager_.loop(&hub_);
  mock_time_.advance(20);
  manager_.loop(&hub_);
  ASSERT_EQ(hub_.request_count, 2);
  manager_.complete_tx_attempt(old_attempt, true);
  EXPECT_EQ(manager_.state(), LearnInState::PROGRAMMING);
  hub_.complete(true);
  EXPECT_EQ(manager_.state(), LearnInState::WAIT_UP);
}

TEST_F(LearnInManagerTest, CancelledSessionCompletionCannotCompleteNewSession) {
  ASSERT_TRUE(manager_.start({0x17A753, 5, 1}));
  manager_.loop(&hub_);
  const auto old_attempt = hub_.attempt;
  manager_.cancel();
  ASSERT_TRUE(manager_.start({0x17A753, 5, 1}));
  manager_.loop(&hub_);
  manager_.complete_tx_attempt(old_attempt, true);
  EXPECT_EQ(manager_.state(), LearnInState::PROGRAMMING);
  hub_.complete(true);
  EXPECT_EQ(manager_.state(), LearnInState::WAIT_UP);
}

TEST_F(LearnInManagerTest, BusyUntilAcceptedPacketCompletes) {
  ASSERT_TRUE(manager_.start({0x17A753, 5, 1}));
  hub_.queue_accepts = false;
  manager_.loop(&hub_);
  EXPECT_TRUE(manager_.is_busy());
  EXPECT_EQ(hub_.client, nullptr);
  EXPECT_FALSE(manager_.confirm_up());

  hub_.queue_accepts = true;
  manager_.loop(&hub_);
  EXPECT_TRUE(manager_.is_busy());
  hub_.complete(true);
  EXPECT_FALSE(manager_.is_busy());
  EXPECT_TRUE(manager_.is_active());
  EXPECT_EQ(manager_.state(), LearnInState::WAIT_UP);
  EXPECT_FALSE(manager_.start({0x17A753, 5, 1}));
}

TEST_F(LearnInManagerTest, TimeoutDiscardsInFlightStep) {
  ASSERT_TRUE(manager_.start({0x17A753, 5, 1, 100}));
  manager_.loop(&hub_);
  mock_time_.advance(100);
  manager_.loop(&hub_);
  hub_.complete(true);
  EXPECT_FALSE(manager_.is_busy());
  EXPECT_FALSE(manager_.is_active());
  EXPECT_EQ(manager_.state(), LearnInState::TIMED_OUT);
}

TEST_F(LearnInManagerTest, LongQueueWaitDoesNotRetryProgrammingStep) {
  ASSERT_TRUE(manager_.start({0x17A753, 5, 1}));
  manager_.loop(&hub_);
  mock_time_.advance(10000);
  manager_.loop(&hub_);
  EXPECT_EQ(hub_.request_count, 1);
  hub_.complete(true);
  EXPECT_EQ(manager_.state(), LearnInState::WAIT_UP);
}

}  // namespace
}  // namespace esphome::elero
