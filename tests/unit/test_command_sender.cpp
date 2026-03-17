/// @file test_command_sender.cpp
/// @brief Unit tests for CommandSender state machine.
///
/// These tests verify the CommandSender logic using a mock Elero hub
/// and deterministic time control via MockTimeProvider.
///
/// This file includes the REAL production CommandSender from command_sender.h.
/// The templated process_queue<Hub>() accepts MockElero directly, so no
/// TestableCommandSender copy is needed.

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

// Include packet constants and EleroCommand (no ESPHome dependencies)
#include "elero/elero_packet.h"

// Include tx_client.h early so TxClient is available for MockElero.
// (command_sender.h includes this transitively, but we need it before MockElero.)
#include "elero/tx_client.h"

// Now provide minimal definitions for ESPHome base classes that
// command_sender.h's transitive includes expect.

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

/// Minimal mock Elero class for testing CommandSender.
/// Records request_tx calls and allows test to control results.
class MockElero {
 public:
  /// Result to return from next request_tx call
  bool next_request_result{true};

  /// Recorded request_tx calls: (client, dst_addr, command_byte)
  std::vector<std::tuple<TxClient*, uint32_t, uint8_t>> recorded_requests;

  /// Currently pending client (simulates hub ownership)
  TxClient* pending_client{nullptr};

  /// Request to transmit a command.
  bool request_tx(TxClient* client, const EleroCommand& cmd) {
    recorded_requests.push_back({client, cmd.dst_addr, cmd.payload[4]});

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

// Include the REAL production CommandSender (templated process_queue works with MockElero)
#include "elero/command_sender.h"

using namespace esphome::elero;

// ============================================================================
// Test Fixture
// ============================================================================

class CommandSenderTest : public ::testing::Test {
 protected:
  MockTimeProvider mock_time_;
  MockElero mock_hub_;
  CommandSender sender_;

  void SetUp() override {
    set_time_provider(&mock_time_);
    mock_time_.reset();
    mock_hub_.clear_records();
    mock_hub_.next_request_result = true;

    // Configure sender with a test address
    sender_.command().dst_addr = 0x123456;
    sender_.command().src_addr = 0xABCDEF;
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
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_FALSE(sender_.is_busy());
  EXPECT_FALSE(sender_.has_pending_commands());
  EXPECT_EQ(sender_.queue_size(), 0u);
}

TEST_F(CommandSenderTest, EnqueueTransitionsToWaitDelay) {
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));  // UP command
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);
  EXPECT_TRUE(sender_.is_busy());
  EXPECT_TRUE(sender_.has_pending_commands());
  EXPECT_EQ(sender_.queue_size(), 1u);
}

TEST_F(CommandSenderTest, EnqueueRejectsWhenFull) {
  // Alternate between UP and DOWN to avoid consecutive collapsing
  for (int i = 0; i < packet::limits::MAX_COMMAND_QUEUE; i++) {
    uint8_t cmd = (i % 2 == 0) ? packet::command::UP : packet::command::DOWN;
    EXPECT_TRUE(sender_.enqueue(cmd));
  }
  EXPECT_FALSE(sender_.enqueue(packet::command::STOP));  // Should fail - queue full
  EXPECT_EQ(sender_.queue_size(), packet::limits::MAX_COMMAND_QUEUE);
}

// ============================================================================
// TX Timing Tests
// ============================================================================

TEST_F(CommandSenderTest, WaitsForDelayBeforeTx) {
  sender_.enqueue(packet::command::UP);

  // Process immediately - should NOT request TX (delay not elapsed)
  mock_time_.current_time = 0;
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // No TX request yet (delay is 50ms, we're at 0)
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 0u);
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Process after delay elapsed
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Now TX should be requested
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 1u);
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);
}

TEST_F(CommandSenderTest, SendsMultiplePacketsPerCommand) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // First packet
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 1u);
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);

  // Complete first packet successfully
  mock_hub_.complete_tx(true);
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Wait for delay and send second packet
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 2u);

  // Complete second packet - command should be done
  mock_hub_.complete_tx(true);
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_FALSE(sender_.is_busy());
}

// ============================================================================
// Retry Tests
// ============================================================================

TEST_F(CommandSenderTest, RetriesOnFailure) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // First attempt
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(false);  // Fail

  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Should retry after exponential backoff (DELAY << 1 for first retry)
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS << 1);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 2u);
}

TEST_F(CommandSenderTest, DropsCommandAfterMaxRetries) {
  sender_.enqueue(packet::command::UP);

  // Fail more than packet::limits::SEND_RETRIES times
  for (int i = 0; i <= packet::limits::SEND_RETRIES + 1; i++) {
    mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

    if (mock_hub_.pending_client != nullptr) {
      mock_hub_.complete_tx(false);
    }
  }

  // Should be idle now (command dropped)
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());
}

// ============================================================================
// Cancellation Tests
// ============================================================================

TEST_F(CommandSenderTest, ClearQueueWhileIdle) {
  sender_.enqueue(packet::command::UP);
  sender_.enqueue(packet::command::DOWN);

  sender_.clear_queue();

  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());
  EXPECT_EQ(sender_.queue_size(), 0u);
}

TEST_F(CommandSenderTest, ClearQueueDuringTx) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);

  // Clear queue while TX is in progress
  sender_.clear_queue();

  // State remains TX_PENDING (can't abort mid-TX)
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);

  // When TX completes, it should be ignored
  mock_hub_.complete_tx(true);

  // Should go directly to IDLE (cancelled)
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
}

TEST_F(CommandSenderTest, ClearQueueDuringTx_FailureIgnored) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  sender_.clear_queue();
  mock_hub_.complete_tx(false);  // Failure should also be ignored

  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());
}

TEST_F(CommandSenderTest, ClearQueueDuringTx_TimeoutRecovery) {
  // This tests the edge case where:
  // 1. TX starts
  // 2. clear_queue() is called (sets cancelled_, empties queue)
  // 3. Timeout fires before callback (moves to WAIT_DELAY)
  // 4. process_queue called - must not crash on empty queue
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // Start TX
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);

  // Clear queue during TX
  sender_.clear_queue();
  EXPECT_FALSE(sender_.has_pending_commands());

  // Timeout fires (hub never called back)
  mock_time_.advance(CommandSender::TX_PENDING_TIMEOUT_MS + 10);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Should be in WAIT_DELAY after timeout (with backoff applied)
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Next process_queue should NOT crash on empty queue
  // Need to wait for backoff (DELAY << 1 for first retry) before queue check happens
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS << 1);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Should detect empty queue and go to IDLE
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());
}

// ============================================================================
// Radio Busy Tests
// ============================================================================

TEST_F(CommandSenderTest, RetriesWhenRadioBusy) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // Radio is busy
  mock_hub_.next_request_result = false;
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Should stay in WAIT_DELAY (will retry)
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Radio becomes available
  mock_hub_.next_request_result = true;
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Now should be TX_PENDING
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);
}

// ============================================================================
// TX_PENDING Timeout Tests
// ============================================================================

TEST_F(CommandSenderTest, TimeoutInTxPending_TriggersRetry) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // Start TX
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);

  // Simulate hub never calling back - advance time past timeout
  mock_time_.advance(CommandSender::TX_PENDING_TIMEOUT_MS + 1);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Should have transitioned to WAIT_DELAY for retry
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Can retry successfully (need to wait for backoff: DELAY << 1 for first retry)
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS << 1);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 2u);
}

TEST_F(CommandSenderTest, TimeoutInTxPending_DropsAfterMaxRetries) {
  sender_.enqueue(packet::command::UP);

  // Repeatedly timeout (never call on_tx_complete)
  for (int i = 0; i <= packet::limits::SEND_RETRIES + 1; i++) {
    mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

    if (sender_.state() == CommandSender::State::TX_PENDING) {
      // Simulate timeout
      mock_time_.advance(CommandSender::TX_PENDING_TIMEOUT_MS + 1);
      sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
    }
  }

  // Should be idle (command dropped after max timeout retries)
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());
}

TEST_F(CommandSenderTest, NoTimeoutIfCallbackArrives) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // Start TX
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Advance time but not past timeout
  mock_time_.advance(CommandSender::TX_PENDING_TIMEOUT_MS - 100);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Still in TX_PENDING (no timeout yet)
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);

  // Callback arrives
  mock_hub_.complete_tx(true);

  // Normal transition to WAIT_DELAY for second packet
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);
}

TEST_F(CommandSenderTest, StaleCallbackAfterTimeoutIsIgnored) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // Start TX
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);

  // Timeout fires - transitions to WAIT_DELAY for retry
  mock_time_.advance(CommandSender::TX_PENDING_TIMEOUT_MS + 10);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Now hub finally calls back (stale callback) - should be ignored
  size_t queue_before = sender_.queue_size();
  mock_hub_.complete_tx(true);

  // State should remain WAIT_DELAY, queue unchanged
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);
  EXPECT_EQ(sender_.queue_size(), queue_before);

  // State machine should still work - complete normally (wait for backoff: DELAY << 1)
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS << 1);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);
  mock_hub_.complete_tx(true);
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);
}

// ============================================================================
// Multiple Commands Tests
// ============================================================================

TEST_F(CommandSenderTest, ProcessesMultipleCommandsInOrder) {
  sender_.enqueue(packet::command::UP);  // UP
  sender_.enqueue(packet::command::DOWN);  // DOWN

  // Complete first command (2 packets)
  for (int pkt = 0; pkt < packet::limits::SEND_PACKETS; pkt++) {
    mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
    mock_hub_.complete_tx(true);
  }

  // Should still have second command
  EXPECT_TRUE(sender_.has_pending_commands());
  EXPECT_EQ(sender_.queue_size(), 1u);

  // Complete second command
  for (int pkt = 0; pkt < packet::limits::SEND_PACKETS; pkt++) {
    mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
    mock_hub_.complete_tx(true);
  }

  // Should be done
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_FALSE(sender_.has_pending_commands());

  // Verify commands were sent in order
  ASSERT_EQ(mock_hub_.recorded_requests.size(), 4u);
  EXPECT_EQ(std::get<2>(mock_hub_.recorded_requests[0]), packet::command::UP);    // First UP
  EXPECT_EQ(std::get<2>(mock_hub_.recorded_requests[1]), packet::command::UP);    // Second UP
  EXPECT_EQ(std::get<2>(mock_hub_.recorded_requests[2]), packet::command::DOWN);  // First DOWN
  EXPECT_EQ(std::get<2>(mock_hub_.recorded_requests[3]), packet::command::DOWN);  // Second DOWN
}

// ============================================================================
// Counter Increment Tests
// ============================================================================

TEST_F(CommandSenderTest, CounterIncrementsAfterCommand) {
  uint8_t initial_counter = sender_.command().counter;

  sender_.enqueue(packet::command::UP);

  // Complete command
  for (int pkt = 0; pkt < packet::limits::SEND_PACKETS; pkt++) {
    mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
    mock_hub_.complete_tx(true);
  }

  EXPECT_EQ(sender_.command().counter, initial_counter + 1);
}

TEST_F(CommandSenderTest, CounterWrapsFrom255To1) {
  sender_.command().counter = 255;
  sender_.enqueue(packet::command::UP);

  for (int pkt = 0; pkt < packet::limits::SEND_PACKETS; pkt++) {
    mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
    sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
    mock_hub_.complete_tx(true);
  }

  EXPECT_EQ(sender_.command().counter, 1);
}

// ============================================================================
// Partial Completion Tests
// ============================================================================

TEST_F(CommandSenderTest, PartialCompletion_Packet1Success_Packet2Failure) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // First packet succeeds
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(true);
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Second packet fails
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(false);

  // Should retry second packet
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Retry succeeds (need to wait for backoff: DELAY << 1 for first retry)
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS << 1);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(true);

  // Command should be complete now
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
}

// ============================================================================
// Queue Stress Tests
// ============================================================================

TEST_F(CommandSenderTest, QueueAllTenCommands) {
  // Fill queue to max (alternate to avoid consecutive collapsing)
  for (int i = 0; i < packet::limits::MAX_COMMAND_QUEUE; i++) {
    uint8_t cmd = (i % 2 == 0) ? packet::command::UP : packet::command::DOWN;
    EXPECT_TRUE(sender_.enqueue(cmd));
  }
  EXPECT_EQ(sender_.queue_size(), packet::limits::MAX_COMMAND_QUEUE);

  // Process all commands
  for (int cmd = 0; cmd < packet::limits::MAX_COMMAND_QUEUE; cmd++) {
    for (int pkt = 0; pkt < packet::limits::SEND_PACKETS; pkt++) {
      mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
      sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
      mock_hub_.complete_tx(true);
    }
  }

  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_EQ(sender_.queue_size(), 0u);
  EXPECT_EQ(mock_hub_.recorded_requests.size(), packet::limits::MAX_COMMAND_QUEUE * packet::limits::SEND_PACKETS);
}

// ============================================================================
// Command Collapsing Tests
// ============================================================================

TEST_F(CommandSenderTest, CollapsesDuplicateConsecutiveCommands) {
  // Enqueue same command multiple times (simulates button mashing)
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));  // Should be collapsed
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));  // Should be collapsed

  // Only one command should be in the queue
  EXPECT_EQ(sender_.queue_size(), 1u);
}

TEST_F(CommandSenderTest, DoesNotCollapseDifferentCommands) {
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));
  EXPECT_TRUE(sender_.enqueue(packet::command::DOWN));
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));  // Different from previous (DOWN)

  // All three should be in the queue
  EXPECT_EQ(sender_.queue_size(), 3u);
}

TEST_F(CommandSenderTest, CollapsesOnlyConsecutiveDuplicates) {
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));    // Collapsed
  EXPECT_TRUE(sender_.enqueue(packet::command::DOWN));
  EXPECT_TRUE(sender_.enqueue(packet::command::DOWN));  // Collapsed
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));

  // Should have: UP, DOWN, UP = 3 commands
  EXPECT_EQ(sender_.queue_size(), 3u);
}

TEST_F(CommandSenderTest, CollapsingStillReturnsTrue) {
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));

  // Collapsing should still return true (success, just not added)
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));
  EXPECT_TRUE(sender_.enqueue(packet::command::UP));

  EXPECT_EQ(sender_.queue_size(), 1u);
}

// ============================================================================
// Enqueue During Non-IDLE States
// ============================================================================

TEST_F(CommandSenderTest, EnqueueDuringWaitDelay_GrowsQueueKeepsState) {
  sender_.enqueue(packet::command::UP);
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);
  EXPECT_EQ(sender_.queue_size(), 1u);

  // Enqueue another command while in WAIT_DELAY
  sender_.enqueue(packet::command::DOWN);
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);  // Unchanged
  EXPECT_EQ(sender_.queue_size(), 2u);
}

TEST_F(CommandSenderTest, EnqueueDuringTxPending_GrowsQueueKeepsState) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);

  // Enqueue another command while TX is in flight
  sender_.enqueue(packet::command::DOWN);
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);  // Unchanged
  EXPECT_EQ(sender_.queue_size(), 2u);

  // Complete TX and process second command normally
  mock_hub_.complete_tx(true);
  // After 2 packets for UP, should move to DOWN
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(true);
  // UP complete, now DOWN
  EXPECT_TRUE(sender_.has_pending_commands());
}

TEST_F(CommandSenderTest, ProcessQueueIdleEmptyIsNoOp) {
  // Calling process_queue when IDLE with empty queue does nothing
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_EQ(sender_.queue_size(), 0u);

  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 0u);
}

TEST_F(CommandSenderTest, IsBusyWhenIdleWithQueuedCommands) {
  // Edge case: after timeout recovery, state could be IDLE but queue has items
  // (This shouldn't happen in practice, but is_busy checks both)
  // We test the logic by checking that is_busy reflects queue state
  EXPECT_FALSE(sender_.is_busy());

  sender_.enqueue(packet::command::UP);
  EXPECT_TRUE(sender_.is_busy());

  // Even after clearing, not busy
  sender_.clear_queue();
  EXPECT_FALSE(sender_.is_busy());
}

// ============================================================================
// Exponential Backoff Tests
// ============================================================================

// Backoff constants for readability (mirrors calculate_backoff_ms_)
// Backoff = DELAY_SEND_PACKETS << retry_count, capped at 400ms
static constexpr uint32_t BACKOFF_RETRY_1 = packet::timing::DELAY_SEND_PACKETS << 1;  // 100ms
static constexpr uint32_t BACKOFF_RETRY_2 = packet::timing::DELAY_SEND_PACKETS << 2;  // 200ms
static constexpr uint32_t BACKOFF_RETRY_3 = packet::timing::DELAY_SEND_PACKETS << 3;  // 400ms (capped)

TEST_F(CommandSenderTest, ExponentialBackoffOnFirstRetry) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // Start TX
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);

  // Fail - should trigger exponential backoff
  mock_hub_.complete_tx(false);
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // First retry backoff = DELAY << 1
  // Wait only half - should NOT be ready yet
  mock_time_.advance(BACKOFF_RETRY_1 / 2);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Wait the other half - NOW should be ready
  mock_time_.advance(BACKOFF_RETRY_1 / 2);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);
}

TEST_F(CommandSenderTest, ExponentialBackoffIncreasesWithRetries) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // Retry 1: BACKOFF_RETRY_1
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(false);

  // Wait for first retry
  mock_time_.advance(BACKOFF_RETRY_1);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);

  // Retry 2: BACKOFF_RETRY_2
  mock_hub_.complete_tx(false);

  // Wait only 3/4 of backoff - should NOT be ready
  mock_time_.advance((BACKOFF_RETRY_2 * 3) / 4);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Wait remaining 1/4 - NOW should be ready
  mock_time_.advance(BACKOFF_RETRY_2 / 4);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);
}

TEST_F(CommandSenderTest, ExponentialBackoffCapsAt400ms) {
  // This test verifies backoff caps at 400ms (DELAY << 3)
  // We need to reach retry 3 without exceeding SEND_RETRIES (3)
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // First TX attempt (not a retry yet)
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(false);  // Fail -> retry 1

  mock_time_.advance(BACKOFF_RETRY_1);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(false);  // Fail -> retry 2

  mock_time_.advance(BACKOFF_RETRY_2);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(false);  // Fail -> retry 3 (last before drop)

  // Now at retry 3 - backoff should be capped at BACKOFF_RETRY_3 (400ms)
  // Wait less than full backoff - should NOT be ready
  mock_time_.advance(BACKOFF_RETRY_3 - packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Wait remaining time - NOW should be ready
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);
}

TEST_F(CommandSenderTest, SuccessResetsBackoff) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // Fail once (triggers BACKOFF_RETRY_1)
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(false);

  // Wait for retry
  mock_time_.advance(BACKOFF_RETRY_1);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");

  // Succeed - should reset retry count
  mock_hub_.complete_tx(true);
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Next packet should only need standard delay (not backoff)
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);
}

// ============================================================================
// STOP Priority Tests
// ============================================================================

TEST_F(CommandSenderTest, ClearQueue_ResetsDelayForImmediateStop) {
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  mock_hub_.complete_tx(true);
  // UP packet 1 sent, last_tx_time_ is now recent

  // Clear and enqueue STOP — should transmit without 50ms wait
  sender_.clear_queue();
  sender_.enqueue(packet::command::STOP);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);
}

// ============================================================================
// Per-Command Packet Count Tests
// ============================================================================

TEST_F(CommandSenderTest, SinglePacketCommand) {
  // Enqueue a command with packets=1; only 1 TX before advance
  sender_.enqueue(packet::command::CHECK, 1);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // First (and only) packet
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(sender_.state(), CommandSender::State::TX_PENDING);
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 1u);

  // Complete the single packet — command should be done
  mock_hub_.complete_tx(true);
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_FALSE(sender_.is_busy());
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 1u);
}

TEST_F(CommandSenderTest, DefaultPacketCount) {
  // Enqueue without explicit packets param — should default to SEND_PACKETS (2)
  sender_.enqueue(packet::command::UP);
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);

  // First packet
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 1u);
  mock_hub_.complete_tx(true);

  // Should need a second packet (not done yet)
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);
  EXPECT_TRUE(sender_.is_busy());

  // Second packet
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 2u);
  mock_hub_.complete_tx(true);

  // Now done
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_FALSE(sender_.is_busy());
}

TEST_F(CommandSenderTest, MixedPacketCounts) {
  // Enqueue cmd with 1 packet, then cmd with 2 packets
  sender_.enqueue(packet::command::CHECK, 1);
  sender_.enqueue(packet::command::UP, 2);

  // --- Process first command (1 packet) ---
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 1u);
  EXPECT_EQ(std::get<2>(mock_hub_.recorded_requests[0]), packet::command::CHECK);

  mock_hub_.complete_tx(true);

  // First command done after 1 packet; second command still pending
  EXPECT_TRUE(sender_.has_pending_commands());
  EXPECT_EQ(sender_.queue_size(), 1u);

  // --- Process second command (2 packets) ---
  // Packet 1
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 2u);
  EXPECT_EQ(std::get<2>(mock_hub_.recorded_requests[1]), packet::command::UP);
  mock_hub_.complete_tx(true);

  // Should still need packet 2
  EXPECT_EQ(sender_.state(), CommandSender::State::WAIT_DELAY);

  // Packet 2
  mock_time_.advance(packet::timing::DELAY_SEND_PACKETS);
  sender_.process_queue(mock_time_.millis(), &mock_hub_, "test");
  EXPECT_EQ(mock_hub_.recorded_requests.size(), 3u);
  EXPECT_EQ(std::get<2>(mock_hub_.recorded_requests[2]), packet::command::UP);
  mock_hub_.complete_tx(true);

  // All done
  EXPECT_EQ(sender_.state(), CommandSender::State::IDLE);
  EXPECT_FALSE(sender_.is_busy());
  EXPECT_EQ(sender_.queue_size(), 0u);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
