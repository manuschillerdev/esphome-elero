/// @file test_cover_core.cpp
/// @brief Unit tests for CoverCore — position tracking, state mapping, polling.

#include <gtest/gtest.h>
#include "elero/cover_core.h"
#include "elero/elero_packet.h"

using namespace esphome::elero;
using Op = CoverCore::Operation;

class CoverCoreTest : public ::testing::Test {
 protected:
  CoverCore core;

  void SetUp() override {
    core.config.open_duration_ms = 10000;
    core.config.close_duration_ms = 10000;
    core.config.poll_interval_ms = 300000;
    core.position = 0.5f;
    core.immediate_poll = false;  // Tests set this explicitly when needed
  }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Position Tracking
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, RecomputePosition_Opening) {
  core.position = 0.0f;
  core.start_movement(Op::OPENING, 0);
  core.recompute_position(5000);
  EXPECT_NEAR(core.position, 0.5f, 0.01f);
}

TEST_F(CoverCoreTest, RecomputePosition_Closing) {
  core.position = 1.0f;
  core.start_movement(Op::CLOSING, 0);
  core.recompute_position(5000);
  EXPECT_NEAR(core.position, 0.5f, 0.01f);
}

TEST_F(CoverCoreTest, RecomputePosition_ClampsTo1) {
  core.position = 0.5f;
  core.start_movement(Op::OPENING, 0);
  core.recompute_position(20000);  // 200% elapsed
  EXPECT_FLOAT_EQ(core.position, 1.0f);
}

TEST_F(CoverCoreTest, RecomputePosition_ClampsTo0) {
  core.position = 0.5f;
  core.start_movement(Op::CLOSING, 0);
  core.recompute_position(20000);
  EXPECT_FLOAT_EQ(core.position, 0.0f);
}

TEST_F(CoverCoreTest, RecomputePosition_IdleNoChange) {
  core.position = 0.5f;
  core.operation = Op::IDLE;
  core.recompute_position(5000);
  EXPECT_FLOAT_EQ(core.position, 0.5f);
}

TEST_F(CoverCoreTest, RecomputePosition_ZeroDurationNoChange) {
  core.config.open_duration_ms = 0;
  core.config.close_duration_ms = 0;
  core.position = 0.5f;
  core.operation = Op::OPENING;
  core.recompute_position(5000);
  EXPECT_FLOAT_EQ(core.position, 0.5f);
}

TEST_F(CoverCoreTest, RecomputePosition_AsymmetricDurations) {
  core.config.open_duration_ms = 10000;
  core.config.close_duration_ms = 20000;  // Closing is slower
  core.position = 1.0f;
  core.start_movement(Op::CLOSING, 0);
  core.recompute_position(10000);  // 50% of close duration
  EXPECT_NEAR(core.position, 0.5f, 0.01f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Movement Timeout
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, MovementTimeout_ResetsToIdle) {
  core.start_movement(Op::OPENING, 0);
  EXPECT_EQ(core.operation, Op::OPENING);
  bool timed_out = core.check_movement_timeout(packet::timing::TIMEOUT_MOVEMENT + 1);
  EXPECT_TRUE(timed_out);
  EXPECT_EQ(core.operation, Op::IDLE);
}

TEST_F(CoverCoreTest, MovementTimeout_NoTimeoutBeforeLimit) {
  core.start_movement(Op::OPENING, 0);
  bool timed_out = core.check_movement_timeout(packet::timing::TIMEOUT_MOVEMENT - 1);
  EXPECT_FALSE(timed_out);
  EXPECT_EQ(core.operation, Op::OPENING);
}

TEST_F(CoverCoreTest, MovementTimeout_IdleNeverTimesOut) {
  core.operation = Op::IDLE;
  bool timed_out = core.check_movement_timeout(999999);
  EXPECT_FALSE(timed_out);
}

// ═══════════════════════════════════════════════════════════════════════════════
// State Mapping (on_rx_state)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, OnRxState_TopSetsOpen) {
  core.position = 0.0f;
  auto result = core.on_rx_state(packet::state::TOP, 1000);
  EXPECT_TRUE(result.changed);
  EXPECT_FLOAT_EQ(core.position, 1.0f);
  EXPECT_EQ(core.operation, Op::IDLE);
}

TEST_F(CoverCoreTest, OnRxState_BottomSetsClosed) {
  core.position = 1.0f;
  auto result = core.on_rx_state(packet::state::BOTTOM, 1000);
  EXPECT_TRUE(result.changed);
  EXPECT_FLOAT_EQ(core.position, 0.0f);
  EXPECT_EQ(core.operation, Op::IDLE);
}

TEST_F(CoverCoreTest, OnRxState_MovingUpContinuesOpening) {
  // RF MOVING_UP while already OPENING → stays OPENING
  core.start_movement(Op::OPENING, 500);
  auto result = core.on_rx_state(packet::state::MOVING_UP, 1000);
  EXPECT_FALSE(result.changed);  // already OPENING
  EXPECT_EQ(core.operation, Op::OPENING);
}

TEST_F(CoverCoreTest, OnRxState_MovingDownContinuesClosing) {
  // RF MOVING_DOWN while already CLOSING → stays CLOSING
  core.start_movement(Op::CLOSING, 500);
  auto result = core.on_rx_state(packet::state::MOVING_DOWN, 1000);
  EXPECT_FALSE(result.changed);  // already CLOSING
  EXPECT_EQ(core.operation, Op::CLOSING);
}

TEST_F(CoverCoreTest, OnRxState_StoppedSetsIdle) {
  core.operation = Op::OPENING;
  auto result = core.on_rx_state(packet::state::STOPPED, 1000);
  EXPECT_TRUE(result.changed);
  EXPECT_EQ(core.operation, Op::IDLE);
}

TEST_F(CoverCoreTest, OnRxState_SameStateNoChange) {
  core.position = 1.0f;
  core.operation = Op::IDLE;
  core.tilt = 0.0f;
  core.on_rx_state(packet::state::TOP, 1000);  // Already at top/idle
  // Second call with same state
  auto result = core.on_rx_state(packet::state::TOP, 2000);
  EXPECT_FALSE(result.changed);
}

TEST_F(CoverCoreTest, OnRxState_MovementStartTracking) {
  core.position = 0.5f;
  // Must start movement explicitly first (RF alone can't start from IDLE)
  core.start_movement(Op::OPENING, 5000);
  EXPECT_EQ(core.movement_start_ms, 5000u);
  EXPECT_FLOAT_EQ(core.movement_start_pos, 0.5f);
  EXPECT_EQ(core.last_direction, Op::OPENING);
  // RF confirms — no reset
  core.on_rx_state(packet::state::MOVING_UP, 6000);
  EXPECT_EQ(core.movement_start_ms, 5000u);
}

TEST_F(CoverCoreTest, OnRxState_MovementStartNotResetOnSameOp) {
  core.start_movement(Op::OPENING, 5000);
  // Same operation via RF — movement start should NOT be reset
  core.on_rx_state(packet::state::MOVING_UP, 7000);
  EXPECT_EQ(core.movement_start_ms, 5000u);
}

TEST_F(CoverCoreTest, OnRxState_WarningReturned) {
  auto result = core.on_rx_state(packet::state::BLOCKING, 1000);
  EXPECT_TRUE(result.is_warning);
  EXPECT_NE(result.warning_msg, nullptr);
}

TEST_F(CoverCoreTest, OnRxState_NoWarningForNormalState) {
  auto result = core.on_rx_state(packet::state::TOP, 1000);
  EXPECT_FALSE(result.is_warning);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Target Position (Intermediate Stops)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, IsAtTarget_IntermediateOpeningReached) {
  core.target_position = 0.7f;
  core.position = 0.7f;
  core.operation = Op::OPENING;
  EXPECT_TRUE(core.is_at_target());
}

TEST_F(CoverCoreTest, IsAtTarget_IntermediateClosingReached) {
  core.target_position = 0.3f;
  core.position = 0.3f;
  core.operation = Op::CLOSING;
  EXPECT_TRUE(core.is_at_target());
}

TEST_F(CoverCoreTest, IsAtTarget_FullOpenNotIntermediate) {
  core.target_position = 1.0f;
  core.position = 1.0f;
  core.operation = Op::OPENING;
  EXPECT_FALSE(core.is_at_target());  // Blind handles endpoint
}

TEST_F(CoverCoreTest, IsAtTarget_FullClosedNotIntermediate) {
  core.target_position = 0.0f;
  core.position = 0.0f;
  core.operation = Op::CLOSING;
  EXPECT_FALSE(core.is_at_target());
}

TEST_F(CoverCoreTest, IsAtTarget_NotYetReached) {
  core.target_position = 0.7f;
  core.position = 0.5f;
  core.operation = Op::OPENING;
  EXPECT_FALSE(core.is_at_target());
}

TEST_F(CoverCoreTest, IsAtTarget_IdleReturnsFalse) {
  core.target_position = 0.5f;
  core.position = 0.5f;
  core.operation = Op::IDLE;
  EXPECT_FALSE(core.is_at_target());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Polling
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, ShouldPoll_AfterInterval) {
  core.last_poll_ms = 0;
  core.config.poll_interval_ms = 1000;
  EXPECT_TRUE(core.should_poll(1001));
}

TEST_F(CoverCoreTest, ShouldPoll_BeforeInterval) {
  core.last_poll_ms = 0;
  core.config.poll_interval_ms = 1000;
  EXPECT_FALSE(core.should_poll(500));
}

TEST_F(CoverCoreTest, ShouldPoll_ImmediatePoll) {
  core.immediate_poll = true;
  core.last_poll_ms = 0;
  core.config.poll_interval_ms = 999999;
  EXPECT_TRUE(core.should_poll(1));
}

TEST_F(CoverCoreTest, ShouldPoll_ZeroIntervalDisablesPolling) {
  core.config.poll_interval_ms = 0;
  EXPECT_FALSE(core.should_poll(999999));
}

TEST_F(CoverCoreTest, ShouldPoll_MaxIntervalDisablesPolling) {
  core.config.poll_interval_ms = UINT32_MAX;
  EXPECT_FALSE(core.should_poll(999999));
}

TEST_F(CoverCoreTest, EffectivePollInterval_MovingUsesFastPoll) {
  core.operation = Op::OPENING;
  EXPECT_EQ(core.effective_poll_interval(), packet::timing::POLL_INTERVAL_MOVING);
}

TEST_F(CoverCoreTest, EffectivePollInterval_IdleUsesConfigured) {
  core.operation = Op::IDLE;
  EXPECT_EQ(core.effective_poll_interval(), core.config.poll_interval_ms);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Start Movement
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, StartMovement_RecordsStartPosition) {
  core.position = 0.3f;
  core.start_movement(Op::OPENING, 1000);
  EXPECT_FLOAT_EQ(core.movement_start_pos, 0.3f);
  EXPECT_EQ(core.movement_start_ms, 1000u);
  EXPECT_EQ(core.operation, Op::OPENING);
  EXPECT_EQ(core.last_direction, Op::OPENING);
}

TEST_F(CoverCoreTest, StartMovement_ResetsTilt) {
  core.tilt = 1.0f;
  core.start_movement(Op::CLOSING, 0);
  EXPECT_FLOAT_EQ(core.tilt, 0.0f);
}

TEST_F(CoverCoreTest, StartMovement_SameDirectionNoOp) {
  core.position = 0.3f;
  core.start_movement(Op::OPENING, 1000);
  core.position = 0.5f;
  core.start_movement(Op::OPENING, 2000);  // Same direction
  // Start position should NOT be updated (no-op)
  EXPECT_FLOAT_EQ(core.movement_start_pos, 0.3f);
  EXPECT_EQ(core.movement_start_ms, 1000u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Position Tracking Helper
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Operation String (position-aware)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, OperationStr_Opening) {
  core.operation = Op::OPENING;
  EXPECT_STREQ(core.operation_str(), "opening");
}

TEST_F(CoverCoreTest, OperationStr_Closing) {
  core.operation = Op::CLOSING;
  EXPECT_STREQ(core.operation_str(), "closing");
}

TEST_F(CoverCoreTest, OperationStr_IdleOpen) {
  core.operation = Op::IDLE;
  core.position = 1.0f;
  EXPECT_STREQ(core.operation_str(), "open");
}

TEST_F(CoverCoreTest, OperationStr_IdleClosed) {
  core.operation = Op::IDLE;
  core.position = 0.0f;
  EXPECT_STREQ(core.operation_str(), "closed");
}

TEST_F(CoverCoreTest, OperationStr_IdleStopped) {
  core.operation = Op::IDLE;
  core.position = 0.5f;
  EXPECT_STREQ(core.operation_str(), "stopped");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Position Tracking Helper
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, HasPositionTracking_BothDurationsRequired) {
  core.config.open_duration_ms = 10000;
  core.config.close_duration_ms = 10000;
  EXPECT_TRUE(core.has_position_tracking());

  core.config.open_duration_ms = 0;
  EXPECT_FALSE(core.has_position_tracking());

  core.config.open_duration_ms = 10000;
  core.config.close_duration_ms = 0;
  EXPECT_FALSE(core.has_position_tracking());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Reset
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// RF → Operation Transitions (cooldown-based blocking)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, OnRxState_IdleToMovingAllowed_NoCooldown) {
  // Without a recent STOP, RF CAN start movement (e.g. physical remote)
  core.operation = Op::IDLE;
  core.idle_from_stop = false;
  auto result = core.on_rx_state(packet::state::MOVING_UP, 1000);
  EXPECT_TRUE(result.changed);
  EXPECT_EQ(core.operation, Op::OPENING);
}

TEST_F(CoverCoreTest, OnRxState_IdleToMovingBlocked_DuringCooldown) {
  // After STOP, transient "still moving" RF responses are blocked
  core.stop_movement(1000);
  EXPECT_EQ(core.operation, Op::IDLE);
  EXPECT_TRUE(core.idle_from_stop);

  auto result = core.on_rx_state(packet::state::MOVING_UP, 1500);  // Within 3s cooldown
  EXPECT_FALSE(result.changed);
  EXPECT_EQ(core.operation, Op::IDLE);
}

TEST_F(CoverCoreTest, OnRxState_IdleToMovingAllowed_AfterCooldown) {
  // After cooldown expires, RF can start movement again
  core.stop_movement(1000);
  uint32_t after_cooldown = 1000 + packet::timing::POST_STOP_COOLDOWN_MS + 1;
  auto result = core.on_rx_state(packet::state::MOVING_UP, after_cooldown);
  EXPECT_TRUE(result.changed);
  EXPECT_EQ(core.operation, Op::OPENING);
}

TEST_F(CoverCoreTest, OnRxState_IdleClearsStopFlag) {
  // When blind reports IDLE state (TOP/BOTTOM/STOPPED), clear the stop flag
  core.stop_movement(1000);
  EXPECT_TRUE(core.idle_from_stop);

  core.on_rx_state(packet::state::STOPPED, 1500);
  EXPECT_FALSE(core.idle_from_stop);
}

TEST_F(CoverCoreTest, OnRxState_MovingToIdleAllowed) {
  core.start_movement(Op::OPENING, 500);
  auto result = core.on_rx_state(packet::state::TOP, 1000);
  EXPECT_TRUE(result.changed);
  EXPECT_EQ(core.operation, Op::IDLE);
}

TEST_F(CoverCoreTest, OnRxState_MovingToMovingAllowed) {
  // OPENING → RF says MOVING_DOWN → becomes CLOSING
  core.start_movement(Op::OPENING, 500);
  auto result = core.on_rx_state(packet::state::MOVING_DOWN, 1000);
  EXPECT_TRUE(result.changed);
  EXPECT_EQ(core.operation, Op::CLOSING);
}

// ═══════════════════════════════════════════════════════════════════════════════
// command_to_operation static helper
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, CommandToOperation_Up) {
  EXPECT_EQ(CoverCore::command_to_operation(packet::command::UP), Op::OPENING);
}

TEST_F(CoverCoreTest, CommandToOperation_Down) {
  EXPECT_EQ(CoverCore::command_to_operation(packet::command::DOWN), Op::CLOSING);
}

TEST_F(CoverCoreTest, CommandToOperation_Stop) {
  EXPECT_EQ(CoverCore::command_to_operation(packet::command::STOP), Op::IDLE);
}

TEST_F(CoverCoreTest, CommandToOperation_Check) {
  EXPECT_EQ(CoverCore::command_to_operation(packet::command::CHECK), Op::IDLE);
}

// ═══════════════════════════════════════════════════════════════════════════════
// External Command → RF Confirmation Flow
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, ExternalCommand_StartsMovement_ThenRxConfirms) {
  // Remote sends UP → start_movement → RF confirms MOVING_UP → stays OPENING
  core.start_movement(Op::OPENING, 1000);
  EXPECT_EQ(core.operation, Op::OPENING);

  auto result = core.on_rx_state(packet::state::MOVING_UP, 1500);
  EXPECT_EQ(core.operation, Op::OPENING);
  // No change since already OPENING
  EXPECT_FALSE(result.changed);
}

TEST_F(CoverCoreTest, ExternalCommand_StopFromRemote_ThenRxTransientMovingBlocked) {
  // User sends UP → moving
  core.start_movement(Op::OPENING, 1000);
  EXPECT_EQ(core.operation, Op::OPENING);

  // User sends STOP → IDLE with cooldown
  core.stop_movement(2000);
  EXPECT_EQ(core.operation, Op::IDLE);
  EXPECT_TRUE(core.idle_from_stop);

  // Transient RF "still moving" within cooldown → must NOT re-enter OPENING
  auto result = core.on_rx_state(packet::state::MOVING_UP, 2500);
  EXPECT_EQ(core.operation, Op::IDLE);
  EXPECT_FALSE(result.changed);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Reset
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, Reset_ClearsAllState) {
  core.position = 0.2f;
  core.tilt = 1.0f;
  core.operation = Op::CLOSING;
  core.target_position = 0.0f;
  core.movement_start_ms = 5000;
  core.movement_start_pos = 0.8f;
  core.idle_from_stop = true;
  core.idle_since_ms = 9999;
  core.awaiting_response = true;
  core.last_command_ms = 7777;
  core.reset();
  EXPECT_FLOAT_EQ(core.position, 0.5f);
  EXPECT_FLOAT_EQ(core.tilt, 0.0f);
  EXPECT_EQ(core.operation, Op::IDLE);
  EXPECT_FLOAT_EQ(core.target_position, 1.0f);
  EXPECT_EQ(core.movement_start_ms, 0u);
  EXPECT_FLOAT_EQ(core.movement_start_pos, 0.0f);
  EXPECT_TRUE(core.immediate_poll);
  EXPECT_FALSE(core.idle_from_stop);
  EXPECT_EQ(core.idle_since_ms, 0u);
  EXPECT_FALSE(core.awaiting_response);
  EXPECT_EQ(core.last_command_ms, 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Response-Wait Polling ("Listen First, Poll Later")
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CoverCoreTest, AwaitingResponse_SetByStartMovement) {
  core.start_movement(Op::OPENING, 5000);
  EXPECT_TRUE(core.awaiting_response);
  EXPECT_EQ(core.last_command_ms, 5000u);
}

TEST_F(CoverCoreTest, AwaitingResponse_SetByStopMovement) {
  core.start_movement(Op::OPENING, 1000);
  core.awaiting_response = false;  // reset for clarity
  core.stop_movement(2000);
  EXPECT_TRUE(core.awaiting_response);
  EXPECT_EQ(core.last_command_ms, 2000u);
}

TEST_F(CoverCoreTest, AwaitingResponse_SuppressesPollingDuringWait) {
  core.start_movement(Op::OPENING, 1000);
  EXPECT_TRUE(core.awaiting_response);
  // Within RESPONSE_WAIT_MS — should NOT poll
  EXPECT_FALSE(core.should_poll(2000));
  EXPECT_TRUE(core.awaiting_response);  // still waiting
}

TEST_F(CoverCoreTest, AwaitingResponse_AllowsPollAfterTimeout) {
  core.start_movement(Op::OPENING, 1000);
  EXPECT_TRUE(core.awaiting_response);
  // After RESPONSE_WAIT_MS — should poll (blind didn't respond)
  uint32_t after_timeout = 1000 + packet::timing::RESPONSE_WAIT_MS;
  EXPECT_TRUE(core.should_poll(after_timeout));
  EXPECT_FALSE(core.awaiting_response);  // cleared by should_poll
}

TEST_F(CoverCoreTest, AwaitingResponse_ClearedByRxState) {
  core.start_movement(Op::OPENING, 1000);
  EXPECT_TRUE(core.awaiting_response);
  // Blind responds
  core.on_rx_state(packet::state::MOVING_UP, 1500);
  EXPECT_FALSE(core.awaiting_response);
}

TEST_F(CoverCoreTest, AwaitingResponse_RxResetsPolTimer) {
  core.start_movement(Op::OPENING, 1000);
  core.on_rx_state(packet::state::MOVING_UP, 1500);
  // Poll timer should be reset to response time
  EXPECT_EQ(core.last_poll_ms, 1500u);
}

TEST_F(CoverCoreTest, RxAlwaysResetsPollTimer_EvenWithoutAwaitingResponse) {
  // Unsolicited broadcast from blind should also defer next poll
  core.awaiting_response = false;
  core.last_poll_ms = 0;
  core.operation = Op::OPENING;
  core.on_rx_state(packet::state::MOVING_UP, 5000);
  EXPECT_EQ(core.last_poll_ms, 5000u);  // deferred
}

TEST_F(CoverCoreTest, RxSuppressesCheckWhileBlindBroadcasts) {
  // Blind broadcasts moving_up every ~2s — no CHECKs needed
  core.start_movement(Op::OPENING, 0);
  core.config.poll_interval_ms = 300000;

  // t=2000: timeout fires first CHECK
  EXPECT_TRUE(core.should_poll(2000));
  core.mark_polled(2000);
  core.immediate_poll = false;

  // t=2500: blind broadcasts (unsolicited)
  core.on_rx_state(packet::state::MOVING_UP, 2500);
  EXPECT_FALSE(core.awaiting_response);
  EXPECT_EQ(core.last_poll_ms, 2500u);

  // t=3500: only 1s since broadcast — no poll (POLL_INTERVAL_MOVING=2s)
  EXPECT_FALSE(core.should_poll(3500));

  // t=4000: blind broadcasts again — resets timer
  core.on_rx_state(packet::state::MOVING_UP, 4000);
  EXPECT_EQ(core.last_poll_ms, 4000u);

  // t=5500: only 1.5s since last broadcast — still no poll
  EXPECT_FALSE(core.should_poll(5500));
}

TEST_F(CoverCoreTest, AwaitingResponse_SuppressesImmediatePoll) {
  core.start_movement(Op::OPENING, 1000);
  core.immediate_poll = true;
  // awaiting_response takes priority over immediate_poll
  EXPECT_FALSE(core.should_poll(1500));
}

TEST_F(CoverCoreTest, AwaitingResponse_NormalPollUnaffectedWhenNotAwaiting) {
  // Not awaiting — normal poll behavior
  core.awaiting_response = false;
  core.last_poll_ms = 0;
  core.config.poll_interval_ms = 1000;
  EXPECT_TRUE(core.should_poll(1001));
}

TEST_F(CoverCoreTest, AwaitingResponse_FullFlowCommandThenResponse) {
  // Command sent at t=1000
  core.start_movement(Op::OPENING, 1000);
  EXPECT_TRUE(core.awaiting_response);

  // t=1500: still waiting, no poll
  EXPECT_FALSE(core.should_poll(1500));

  // t=1800: blind responds
  core.on_rx_state(packet::state::MOVING_UP, 1800);
  EXPECT_FALSE(core.awaiting_response);
  EXPECT_EQ(core.last_poll_ms, 1800u);

  // t=2000: too soon for next poll (interval is 2s moving)
  EXPECT_FALSE(core.should_poll(2000));

  // t=3801: next moving poll fires (1800 + 2000 = 3800)
  EXPECT_TRUE(core.should_poll(3801));
}

TEST_F(CoverCoreTest, AwaitingResponse_FullFlowCommandThenTimeout) {
  // Command sent at t=1000
  core.start_movement(Op::OPENING, 1000);
  EXPECT_TRUE(core.awaiting_response);

  // t=2500: still within RESPONSE_WAIT_MS (2000ms from t=1000)
  EXPECT_FALSE(core.should_poll(2500));

  // t=3000: exactly at timeout (1000 + 2000) — should poll
  EXPECT_TRUE(core.should_poll(3000));
  EXPECT_FALSE(core.awaiting_response);  // cleared by should_poll
}

TEST_F(CoverCoreTest, MarkPolled_SetsAwaitingResponse) {
  // Every CHECK poll gets a response-wait window
  core.awaiting_response = false;
  core.mark_polled(5000);
  EXPECT_TRUE(core.awaiting_response);
  EXPECT_EQ(core.last_command_ms, 5000u);
}

TEST_F(CoverCoreTest, AwaitingResponse_ChainedChecksEachGetWaitWindow) {
  // UP at t=0 → timeout at t=2000 → CHECK + mark_polled → timeout at t=4000 → CHECK ...
  core.start_movement(Op::OPENING, 0);

  // t=2000: first timeout fires
  EXPECT_TRUE(core.should_poll(2000));
  core.mark_polled(2000);  // re-arms awaiting_response
  core.immediate_poll = false;
  EXPECT_TRUE(core.awaiting_response);

  // t=3000: within second wait window — no poll
  EXPECT_FALSE(core.should_poll(3000));

  // t=4000: second timeout fires
  EXPECT_TRUE(core.should_poll(4000));
  core.mark_polled(4000);
  core.immediate_poll = false;

  // t=5000: within third wait window — no poll
  EXPECT_FALSE(core.should_poll(5000));
}

TEST_F(CoverCoreTest, AwaitingResponse_StopFlowNoCheckEnqueued) {
  // Simulate STOP: stop_movement sets awaiting_response
  core.start_movement(Op::OPENING, 1000);
  core.stop_movement(2000);
  EXPECT_TRUE(core.awaiting_response);
  EXPECT_EQ(core.last_command_ms, 2000u);

  // Within wait window — no poll (no CHECK should be sent)
  EXPECT_FALSE(core.should_poll(3000));

  // Blind responds to STOP with STOPPED state
  core.on_rx_state(packet::state::STOPPED, 3500);
  EXPECT_FALSE(core.awaiting_response);
  EXPECT_EQ(core.operation, Op::IDLE);
}

TEST_F(CoverCoreTest, AwaitingResponse_NotClearedByBlockedCooldownResponse) {
  // STOP → blind sends transient MOVING_UP during cooldown → must NOT clear awaiting_response
  core.start_movement(Op::OPENING, 1000);
  core.stop_movement(2000);
  EXPECT_TRUE(core.awaiting_response);
  EXPECT_TRUE(core.idle_from_stop);

  // Blind responds with MOVING_UP within cooldown — blocked by post-stop cooldown
  core.on_rx_state(packet::state::MOVING_UP, 2500);
  EXPECT_TRUE(core.awaiting_response);  // NOT cleared — blocked response doesn't count
  EXPECT_EQ(core.operation, Op::IDLE);  // still IDLE (blocked)

  // Eventually blind sends STOPPED — this is accepted, clears awaiting_response
  core.on_rx_state(packet::state::STOPPED, 3500);
  EXPECT_FALSE(core.awaiting_response);
  EXPECT_EQ(core.operation, Op::IDLE);
}

TEST_F(CoverCoreTest, AwaitingResponse_BlockedThenTimeout_FallbackCheck) {
  // STOP → blocked MOVING_UP → no STOPPED arrives → timeout fires CHECK
  core.start_movement(Op::OPENING, 1000);
  core.stop_movement(2000);

  // Blocked response at t=2500
  core.on_rx_state(packet::state::MOVING_UP, 2500);
  EXPECT_TRUE(core.awaiting_response);  // still waiting

  // No STOPPED arrives — timeout at t=4000 (2000 + 2000)
  EXPECT_TRUE(core.should_poll(4000));
  EXPECT_FALSE(core.awaiting_response);  // cleared by timeout
}
