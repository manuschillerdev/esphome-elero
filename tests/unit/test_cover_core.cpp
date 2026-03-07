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

TEST_F(CoverCoreTest, OnRxState_MovingUpSetsOpening) {
  auto result = core.on_rx_state(packet::state::MOVING_UP, 1000);
  EXPECT_TRUE(result.changed);
  EXPECT_EQ(core.operation, Op::OPENING);
}

TEST_F(CoverCoreTest, OnRxState_MovingDownSetsClosing) {
  auto result = core.on_rx_state(packet::state::MOVING_DOWN, 1000);
  EXPECT_TRUE(result.changed);
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
  core.on_rx_state(packet::state::MOVING_UP, 5000);
  EXPECT_EQ(core.movement_start_ms, 5000u);
  EXPECT_FLOAT_EQ(core.movement_start_pos, 0.5f);  // pos unchanged by MOVING_UP
  EXPECT_EQ(core.last_direction, Op::OPENING);
}

TEST_F(CoverCoreTest, OnRxState_MovementStartNotResetOnSameOp) {
  core.on_rx_state(packet::state::MOVING_UP, 5000);
  // Same operation again — movement start should NOT be reset
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

TEST_F(CoverCoreTest, Reset_ClearsAllState) {
  core.position = 0.2f;
  core.tilt = 1.0f;
  core.operation = Op::CLOSING;
  core.target_position = 0.0f;
  core.movement_start_ms = 5000;
  core.movement_start_pos = 0.8f;
  core.reset();
  EXPECT_FLOAT_EQ(core.position, 0.5f);
  EXPECT_FLOAT_EQ(core.tilt, 0.0f);
  EXPECT_EQ(core.operation, Op::IDLE);
  EXPECT_FLOAT_EQ(core.target_position, 1.0f);
  EXPECT_EQ(core.movement_start_ms, 0u);
  EXPECT_FLOAT_EQ(core.movement_start_pos, 0.0f);
}
