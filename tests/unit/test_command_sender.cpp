/// @file test_command_sender.cpp
/// @brief Unit tests for CommandSender state machine.
///
/// These tests verify the CommandSender logic using a mock Elero hub
/// and deterministic time control via MockTimeProvider.

#include <gtest/gtest.h>
#include <queue>
#include <vector>
#include <tuple>

// For unit tests, we need to provide stubs for ESPHome logging macros
#define ESP_LOGV(tag, format, ...) ((void)0)
#define ESP_LOGVV(tag, format, ...) ((void)0)
#define ESP_LOGD(tag, format, ...) ((void)0)
#define ESP_LOGI(tag, format, ...) ((void)0)
#define ESP_LOGW(tag, format, ...) ((void)0)
#define ESP_LOGE(tag, format, ...) ((void)0)

// UNIT_TEST is defined by CMake via target_compile_definitions

// Include the time provider (which has UNIT_TEST handling)
#include "elero/time_provider.h"

// Now provide minimal definitions for CommandSender dependencies

namespace esphome {

// Minimal stub for ESPHome Component base class
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
};

// Minimal stub for SPI
namespace spi {
enum BitOrder { BIT_ORDER_MSB_FIRST };
enum ClockPolarity { CLOCK_POLARITY_LOW };
enum ClockPhase { CLOCK_PHASE_LEADING };
enum DataRate { DATA_RATE_2MHZ };

template <BitOrder, ClockPolarity, ClockPhase, DataRate>
class SPIDevice {
 public:
  void enable() {}
  void disable() {}
};
}  // namespace spi

namespace elero {

// Protocol constants needed by command_sender.h
constexpr uint8_t ELERO_SEND_PACKETS = 2;
constexpr uint8_t ELERO_SEND_RETRIES = 3;
constexpr uint32_t ELERO_DELAY_SEND_PACKETS = 50;
constexpr uint8_t ELERO_MAX_COMMAND_QUEUE = 10;

/// TxClient interface (must be defined before MockElero)
class TxClient {
 public:
  virtual ~TxClient() = default;
  virtual void on_tx_complete(bool success) = 0;
};

struct EleroCommand {
  uint8_t counter{1};
  uint32_t blind_addr{0};
  uint32_t remote_addr{0};
  uint8_t channel{0};
  uint8_t pck_inf[2]{0, 0};
  uint8_t hop{0};
  uint8_t payload[10]{0};
};

/// Minimal mock Elero class for testing CommandSender.
/// Records request_tx calls and allows test to control results.
class MockElero {
 public:
  /// Result to return from next request_tx call
  bool next_request_result{true};

  /// Recorded request_tx calls: (client, blind_addr, command_byte)
  std::vector<std::tuple<TxClient*, uint32_t, uint8_t>> recorded_requests;

  /// Currently pending client (simulates hub ownership)
  TxClient* pending_client{nullptr};

  /// Request to transmit a command.
  bool request_tx(TxClient* client, const EleroCommand& cmd) {
    recorded_requests.push_back({client, cmd.blind_addr, cmd.payload[4]});

    if (next_request_result) {
      pending_client = client;
    }
    return next_request_result;
  }

  /// Simulate TX completion (call the pending client's callback)
  void complete_tx(bool success) {
    if (pending_client != nullptr) {
      TxClient* client = pending_client;
      pending_client = nullptr;
      client->on_tx_complete(success);
    }
  }

  /// Clear recorded requests
  void clear_records() {
    recorded_requests.clear();
  }
};

}  // namespace elero
}  // namespace esphome

// Now include a custom version of CommandSender that uses MockElero
// We'll redefine the process_queue signature to accept MockElero*
namespace esphome::elero {

/// CommandSender adapted for testing with MockElero
class TestableCommandSender : public TxClient {
 public:
  enum class State : uint8_t {
    IDLE,
    WAIT_DELAY,
    TX_PENDING,
  };

  /// Timeout for TX_PENDING state (matches production code)
  static constexpr uint32_t TX_PENDING_TIMEOUT_MS = 500;

  TestableCommandSender() = default;

  void process_queue(uint32_t now, MockElero* parent, const char* tag) {
    this->log_tag_ = tag;

    switch (this->state_) {
      case State::IDLE:
        if (this->command_queue_.empty()) {
          return;
        }
        this->state_ = State::WAIT_DELAY;
        [[fallthrough]];

      case State::WAIT_DELAY:
        if ((now - this->last_tx_time_) < ELERO_DELAY_SEND_PACKETS) {
          return;
        }

        // Guard: queue might be empty if clear_queue was called during TX
        // and timeout moved us here before callback arrived
        if (this->command_queue_.empty()) {
          this->cancelled_ = false;
          this->state_ = State::IDLE;
          return;
        }

        this->command_.payload[4] = this->command_queue_.front();

        if (parent->request_tx(this, this->command_)) {
          this->state_ = State::TX_PENDING;
          this->tx_start_time_ = now;
        }
        break;

      case State::TX_PENDING:
        // Watchdog: if hub never calls back, recover after timeout
        if ((now - this->tx_start_time_) > TX_PENDING_TIMEOUT_MS) {
          // Treat as TX failure - use retry logic
          ++this->send_retries_;
          if (this->send_retries_ > ELERO_SEND_RETRIES) {
            this->advance_queue_();
          } else {
            this->state_ = State::WAIT_DELAY;
            this->last_tx_time_ = now;
          }
        }
        break;
    }
  }

  void on_tx_complete(bool success) override {
    // Guard: reject stale callbacks after timeout recovery
    if (this->state_ != State::TX_PENDING) {
      return;
    }

    this->last_tx_time_ = get_time_provider().millis();

    if (this->cancelled_) {
      this->cancelled_ = false;
      this->send_packets_ = 0;
      this->send_retries_ = 0;
      this->state_ = State::IDLE;
      return;
    }

    if (success) {
      this->send_retries_ = 0;
      ++this->send_packets_;

      if (this->send_packets_ >= ELERO_SEND_PACKETS) {
        this->advance_queue_();
      } else {
        this->state_ = State::WAIT_DELAY;
      }
    } else {
      ++this->send_retries_;

      if (this->send_retries_ > ELERO_SEND_RETRIES) {
        this->advance_queue_();
      } else {
        this->state_ = State::WAIT_DELAY;
      }
    }
  }

  [[nodiscard]] bool enqueue(uint8_t cmd_byte) {
    if (this->command_queue_.size() >= ELERO_MAX_COMMAND_QUEUE) {
      return false;
    }
    this->command_queue_.push(cmd_byte);

    if (this->state_ == State::IDLE) {
      this->state_ = State::WAIT_DELAY;
    }
    return true;
  }

  void clear_queue() {
    this->command_queue_ = std::queue<uint8_t>{};
    this->send_packets_ = 0;
    this->send_retries_ = 0;

    if (this->state_ == State::TX_PENDING) {
      this->cancelled_ = true;
    } else {
      this->state_ = State::IDLE;
    }
  }

  State state() const { return this->state_; }
  bool is_busy() const { return this->state_ != State::IDLE || !this->command_queue_.empty(); }
  bool has_pending_commands() const { return !this->command_queue_.empty(); }
  size_t queue_size() const { return this->command_queue_.size(); }
  EleroCommand& command() { return this->command_; }
  const EleroCommand& command() const { return this->command_; }

 private:
  void advance_queue_() {
    if (!this->command_queue_.empty()) {
      this->command_queue_.pop();
    }

    this->send_packets_ = 0;
    this->send_retries_ = 0;
    this->increase_counter_();

    if (this->command_queue_.empty()) {
      this->state_ = State::IDLE;
    } else {
      this->state_ = State::WAIT_DELAY;
    }
  }

  void increase_counter_() {
    if (this->command_.counter == 0xff) {
      this->command_.counter = 1;
    } else {
      ++this->command_.counter;
    }
  }

  EleroCommand command_{1, 0, 0, 0, {0, 0}, 0, {0}};
  std::queue<uint8_t> command_queue_;

  State state_{State::IDLE};
  uint32_t last_tx_time_{0};
  uint32_t tx_start_time_{0};
  uint8_t send_packets_{0};
  uint8_t send_retries_{0};
  bool cancelled_{false};
  const char* log_tag_{"sender"};
};

}  // namespace esphome::elero

using namespace esphome::elero;

// ============================================================================
// Test Fixture
// ============================================================================

class CommandSenderTest : public ::testing::Test {
 protected:
  MockTimeProvider mock_time_;
  MockElero mock_hub_;
  TestableCommandSender sender_;

  void SetUp() override {
    set_time_provider(&mock_time_);
    mock_time_.reset();
    mock_hub_.clear_records();
    mock_hub_.next_request_result = true;

    // Configure sender with a test address
    sender_.command().blind_addr = 0x123456;
    sender_.command().remote_addr = 0xABCDEF;
    sender_.command().channel = 5;
  }

  void TearDown() override {
    set_time_provider(nullptr);  // Restore default
  }
};

// ============================================================================
// Basic State Machine Tests
// ============================================================================

TEST_F(CommandSenderTest, InitialState) {
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::IDLE);
  EXPECT_FALSE(sender_.is_busy());
  EXPECT_FALSE(sender_.has_pending_commands());
  EXPECT_EQ(sender_.queue_size(), 0u);
}

TEST_F(CommandSenderTest, EnqueueTransitionsToWaitDelay) {
  EXPECT_TRUE(sender_.enqueue(0x20));  // UP command
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);
  EXPECT_TRUE(sender_.is_busy());
  EXPECT_TRUE(sender_.has_pending_commands());
  EXPECT_EQ(sender_.queue_size(), 1u);
}

TEST_F(CommandSenderTest, EnqueueRejectsWhenFull) {
  for (int i = 0; i < ELERO_MAX_COMMAND_QUEUE; i++) {
    EXPECT_TRUE(sender_.enqueue(0x20));
  }
  EXPECT_FALSE(sender_.enqueue(0x20));  // Should fail - queue full
  EXPECT_EQ(sender_.queue_size(), ELERO_MAX_COMMAND_QUEUE);
}

// ============================================================================
// TX Timing Tests
// ============================================================================

TEST_F(CommandSenderTest, WaitsForDelayBeforeTx) {
  sender_.enqueue(0x20);

  // Process immediately - should NOT request TX (delay not elapsed)
  mock_time_.current_time = 0;
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // No TX request yet (delay is 50ms, we're at 0)
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 0u);
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);

  // Process after delay elapsed
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Now TX should be requested
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 1u);
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::TX_PENDING);
}

TEST_F(CommandSenderTest, SendsMultiplePacketsPerCommand) {
  sender_.enqueue(0x20);
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);

  // First packet
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 1u);
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::TX_PENDING);

  // Complete first packet successfully
  mock_hub_.complete_tx(true);
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);

  // Wait for delay and send second packet
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 2u);

  // Complete second packet - command should be done
  mock_hub_.complete_tx(true);
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::IDLE);
  EXPECT_FALSE(sender_.is_busy());
}

// ============================================================================
// Retry Tests
// ============================================================================

TEST_F(CommandSenderTest, RetriesOnFailure) {
  sender_.enqueue(0x20);
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);

  // First attempt
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(false);  // Fail

  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);

  // Should retry
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 2u);
}

TEST_F(CommandSenderTest, DropsCommandAfterMaxRetries) {
  sender_.enqueue(0x20);

  // Fail more than ELERO_SEND_RETRIES times
  for (int i = 0; i <= ELERO_SEND_RETRIES + 1; i++) {
    mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

    if (mock_hub_.pending_client != nullptr) {
      mock_hub_.complete_tx(false);
    }
  }

  // Should be idle now (command dropped)
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());
}

// ============================================================================
// Cancellation Tests
// ============================================================================

TEST_F(CommandSenderTest, ClearQueueWhileIdle) {
  sender_.enqueue(0x20);
  sender_.enqueue(0x40);

  sender_.clear_queue();

  EXPECT_EQ(sender_.state(), TestableCommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());
  EXPECT_EQ(sender_.queue_size(), 0u);
}

TEST_F(CommandSenderTest, ClearQueueDuringTx) {
  sender_.enqueue(0x20);
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  EXPECT_EQ(sender_.state(), TestableCommandSender::State::TX_PENDING);

  // Clear queue while TX is in progress
  sender_.clear_queue();

  // State remains TX_PENDING (can't abort mid-TX)
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::TX_PENDING);

  // When TX completes, it should be ignored
  mock_hub_.complete_tx(true);

  // Should go directly to IDLE (cancelled)
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::IDLE);
}

TEST_F(CommandSenderTest, ClearQueueDuringTx_FailureIgnored) {
  sender_.enqueue(0x20);
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  sender_.clear_queue();
  mock_hub_.complete_tx(false);  // Failure should also be ignored

  EXPECT_EQ(sender_.state(), TestableCommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());
}

TEST_F(CommandSenderTest, ClearQueueDuringTx_TimeoutRecovery) {
  // This tests the edge case where:
  // 1. TX starts
  // 2. clear_queue() is called (sets cancelled_, empties queue)
  // 3. Timeout fires before callback (moves to WAIT_DELAY)
  // 4. process_queue called - must not crash on empty queue
  sender_.enqueue(0x20);
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);

  // Start TX
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::TX_PENDING);

  // Clear queue during TX
  sender_.clear_queue();
  EXPECT_FALSE(sender_.has_pending_commands());

  // Timeout fires (hub never called back)
  mock_time_.advance(TestableCommandSender::TX_PENDING_TIMEOUT_MS + 10);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Should be in WAIT_DELAY after timeout
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);

  // Next process_queue should NOT crash on empty queue
  // Should detect empty queue and go to IDLE
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  EXPECT_EQ(sender_.state(), TestableCommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());
}

// ============================================================================
// Radio Busy Tests
// ============================================================================

TEST_F(CommandSenderTest, RetriesWhenRadioBusy) {
  sender_.enqueue(0x20);
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);

  // Radio is busy
  mock_hub_.next_request_result = false;
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Should stay in WAIT_DELAY (will retry)
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);

  // Radio becomes available
  mock_hub_.next_request_result = true;
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Now should be TX_PENDING
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::TX_PENDING);
}

// ============================================================================
// TX_PENDING Timeout Tests (NEW - tests the timeout watchdog)
// ============================================================================

TEST_F(CommandSenderTest, TimeoutInTxPending_TriggersRetry) {
  sender_.enqueue(0x20);
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);

  // Start TX
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::TX_PENDING);

  // Simulate hub never calling back - advance time past timeout
  mock_time_.advance(TestableCommandSender::TX_PENDING_TIMEOUT_MS + 1);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Should have transitioned to WAIT_DELAY for retry
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);

  // Can retry successfully
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::TX_PENDING);
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 2u);
}

TEST_F(CommandSenderTest, TimeoutInTxPending_DropsAfterMaxRetries) {
  sender_.enqueue(0x20);

  // Repeatedly timeout (never call on_tx_complete)
  for (int i = 0; i <= ELERO_SEND_RETRIES + 1; i++) {
    mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

    if (sender_.state() == TestableCommandSender::State::TX_PENDING) {
      // Simulate timeout
      mock_time_.advance(TestableCommandSender::TX_PENDING_TIMEOUT_MS + 1);
      sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
    }
  }

  // Should be idle (command dropped after max timeout retries)
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());
}

TEST_F(CommandSenderTest, NoTimeoutIfCallbackArrives) {
  sender_.enqueue(0x20);
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);

  // Start TX
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Advance time but not past timeout
  mock_time_.advance(TestableCommandSender::TX_PENDING_TIMEOUT_MS - 100);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Still in TX_PENDING (no timeout yet)
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::TX_PENDING);

  // Callback arrives
  mock_hub_.complete_tx(true);

  // Normal transition to WAIT_DELAY for second packet
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);
}

TEST_F(CommandSenderTest, StaleCallbackAfterTimeoutIsIgnored) {
  sender_.enqueue(0x20);
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);

  // Start TX
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::TX_PENDING);

  // Timeout fires - transitions to WAIT_DELAY for retry
  mock_time_.advance(TestableCommandSender::TX_PENDING_TIMEOUT_MS + 10);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);

  // Now hub finally calls back (stale callback) - should be ignored
  size_t queue_before = sender_.queue_size();
  mock_hub_.complete_tx(true);

  // State should remain WAIT_DELAY, queue unchanged
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);
  EXPECT_EQ(sender_.queue_size(), queue_before);

  // State machine should still work - complete normally
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::TX_PENDING);
  mock_hub_.complete_tx(true);
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);
}

// ============================================================================
// Multiple Commands Tests
// ============================================================================

TEST_F(CommandSenderTest, ProcessesMultipleCommandsInOrder) {
  sender_.enqueue(0x20);  // UP
  sender_.enqueue(0x40);  // DOWN

  // Complete first command (2 packets)
  for (int packet = 0; packet < ELERO_SEND_PACKETS; packet++) {
    mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
    mock_hub_.complete_tx(true);
  }

  // Should still have second command
  EXPECT_TRUE(sender_.has_pending_commands());
  EXPECT_EQ(sender_.queue_size(), 1u);

  // Complete second command
  for (int packet = 0; packet < ELERO_SEND_PACKETS; packet++) {
    mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
    mock_hub_.complete_tx(true);
  }

  // Should be done
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());

  // Verify commands were sent in order
  ASSERT_EQ(mock_hub_.recorded_requests.size(), 4u);
  EXPECT_EQ(std::get<2>(mock_hub_.recorded_requests[0]), 0x20);  // First UP
  EXPECT_EQ(std::get<2>(mock_hub_.recorded_requests[1]), 0x20);  // Second UP
  EXPECT_EQ(std::get<2>(mock_hub_.recorded_requests[2]), 0x40);  // First DOWN
  EXPECT_EQ(std::get<2>(mock_hub_.recorded_requests[3]), 0x40);  // Second DOWN
}

// ============================================================================
// Counter Increment Tests
// ============================================================================

TEST_F(CommandSenderTest, CounterIncrementsAfterCommand) {
  uint8_t initial_counter = sender_.command().counter;

  sender_.enqueue(0x20);

  // Complete command
  for (int packet = 0; packet < ELERO_SEND_PACKETS; packet++) {
    mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
    mock_hub_.complete_tx(true);
  }

  EXPECT_EQ(sender_.command().counter, initial_counter + 1);
}

TEST_F(CommandSenderTest, CounterWrapsFrom255To1) {
  sender_.command().counter = 255;
  sender_.enqueue(0x20);

  for (int packet = 0; packet < ELERO_SEND_PACKETS; packet++) {
    mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
    mock_hub_.complete_tx(true);
  }

  EXPECT_EQ(sender_.command().counter, 1);
}

// ============================================================================
// Partial Completion Tests
// ============================================================================

TEST_F(CommandSenderTest, PartialCompletion_Packet1Success_Packet2Failure) {
  sender_.enqueue(0x20);
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);

  // First packet succeeds
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(true);
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);

  // Second packet fails
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(false);

  // Should retry second packet
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::WAIT_DELAY);

  // Retry succeeds
  mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(true);

  // Command should be complete now
  EXPECT_EQ(sender_.state(), TestableCommandSender::State::IDLE);
}

// ============================================================================
// Queue Stress Tests
// ============================================================================

TEST_F(CommandSenderTest, QueueAllTenCommands) {
  // Fill queue to max
  for (int i = 0; i < ELERO_MAX_COMMAND_QUEUE; i++) {
    EXPECT_TRUE(sender_.enqueue(0x20 + i));
  }
  EXPECT_EQ(sender_.queue_size(), ELERO_MAX_COMMAND_QUEUE);

  // Process all commands
  for (int cmd = 0; cmd < ELERO_MAX_COMMAND_QUEUE; cmd++) {
    for (int packet = 0; packet < ELERO_SEND_PACKETS; packet++) {
      mock_time_.advance(ELERO_DELAY_SEND_PACKETS);
      sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
      mock_hub_.complete_tx(true);
    }
  }

  EXPECT_EQ(sender_.state(), TestableCommandSender::State::IDLE);
  EXPECT_EQ(sender_.queue_size(), 0u);
  EXPECT_EQ(mock_hub_.recorded_requests.size(), ELERO_MAX_COMMAND_QUEUE * ELERO_SEND_PACKETS);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
