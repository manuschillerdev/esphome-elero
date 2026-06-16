#include <gtest/gtest.h>

#include "elero/learn_in_manager.h"
#include "elero/time_provider.h"

namespace esphome::elero {
namespace {

class FakeHub {
 public:
  bool queue_accepts{true};
  TxClient *client{nullptr};
  EleroCommand last_cmd{};
  int request_count{0};

  bool request_tx(TxClient *tx_client, const EleroCommand &cmd) {
    ++request_count;
    if (!queue_accepts) {
      return false;
    }
    client = tx_client;
    last_cmd = cmd;
    return true;
  }

  void complete(bool success) {
    ASSERT_NE(client, nullptr);
    TxClient *current = client;
    client = nullptr;
    current->on_tx_complete(success);
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

  MockTimeProvider mock_time_;
  LearnInManager manager_;
  FakeHub hub_;
};

TEST_F(LearnInManagerTest, RejectsInvalidStartRequest) {
  LearnInStartRequest req{};
  EXPECT_FALSE(manager_.start(req));
  EXPECT_EQ(manager_.state(), LearnInState::IDLE);
}

TEST_F(LearnInManagerTest, FullLearnInFlowCompletes) {
  LearnInStartRequest req{};
  req.src_addr = 0x17A753;
  req.channel = 5;

  ASSERT_TRUE(manager_.start(req));
  EXPECT_EQ(manager_.state(), LearnInState::PROGRAMMING);

  for (int i = 0; i < packet::program::PACKETS; ++i) {
    manager_.loop(mock_time_.millis(), &hub_);
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
    manager_.loop(mock_time_.millis(), &hub_);
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
    manager_.loop(mock_time_.millis(), &hub_);
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

  manager_.loop(mock_time_.millis(), &hub_);
  for (int i = 0; i <= packet::limits::SEND_RETRIES; ++i) {
    hub_.complete(false);
    mock_time_.advance(packet::timing::MAX_BACKOFF_MS);
    manager_.loop(mock_time_.millis(), &hub_);
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
  manager_.loop(mock_time_.millis(), &hub_);

  EXPECT_EQ(manager_.state(), LearnInState::TIMED_OUT);
}

}  // namespace
}  // namespace esphome::elero
