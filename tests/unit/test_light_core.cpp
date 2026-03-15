/// @file test_light_core.cpp
/// @brief Unit tests for LightCore — brightness tracking, dimming, state mapping.

#include <gtest/gtest.h>
#include "elero/light_core.h"
#include "elero/elero_packet.h"

using namespace esphome::elero;

class LightCoreTest : public ::testing::Test {
 protected:
  LightCore core;

  void SetUp() override {
    core.config.dim_duration_ms = 5000;
  }
};

// ═══════════════════════════════════════════════════════════════════════════════
// State Mapping (on_rx_state)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightCoreTest, OnRxState_LightOnFromOff) {
  core.is_on = false;
  bool changed = core.on_rx_state(packet::state::LIGHT_ON);
  EXPECT_TRUE(changed);
  EXPECT_TRUE(core.is_on);
  EXPECT_FLOAT_EQ(core.brightness, 1.0f);
}

TEST_F(LightCoreTest, OnRxState_LightOffFromOn) {
  core.is_on = true;
  core.brightness = 1.0f;
  bool changed = core.on_rx_state(packet::state::LIGHT_OFF);
  EXPECT_TRUE(changed);
  EXPECT_FALSE(core.is_on);
  EXPECT_FLOAT_EQ(core.brightness, 0.0f);
}

TEST_F(LightCoreTest, OnRxState_AlreadyOnNoChange) {
  core.is_on = true;
  bool changed = core.on_rx_state(packet::state::LIGHT_ON);
  EXPECT_FALSE(changed);
}

TEST_F(LightCoreTest, OnRxState_AlreadyOffNoChange) {
  core.is_on = false;
  bool changed = core.on_rx_state(packet::state::LIGHT_OFF);
  EXPECT_FALSE(changed);
}

TEST_F(LightCoreTest, OnRxState_BottomTiltTurnsOff) {
  core.is_on = true;
  bool changed = core.on_rx_state(packet::state::BOTTOM_TILT);
  EXPECT_TRUE(changed);
  EXPECT_FALSE(core.is_on);
}

TEST_F(LightCoreTest, OnRxState_StopsDimming) {
  core.is_on = true;
  core.dim_direction = DimDirection::UP;
  core.on_rx_state(packet::state::LIGHT_OFF);
  EXPECT_EQ(core.dim_direction, DimDirection::NONE);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Brightness Recomputation
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightCoreTest, RecomputeBrightness_DimmingUp) {
  core.brightness = 0.0f;
  core.dim_direction = DimDirection::UP;
  core.last_recompute_time = 0;
  core.recompute_brightness(2500);  // 50% of 5000ms
  EXPECT_NEAR(core.brightness, 0.5f, 0.01f);
}

TEST_F(LightCoreTest, RecomputeBrightness_DimmingDown) {
  core.brightness = 1.0f;
  core.dim_direction = DimDirection::DOWN;
  core.last_recompute_time = 0;
  core.recompute_brightness(2500);
  EXPECT_NEAR(core.brightness, 0.5f, 0.01f);
}

TEST_F(LightCoreTest, RecomputeBrightness_ClampsTo1) {
  core.brightness = 0.5f;
  core.dim_direction = DimDirection::UP;
  core.last_recompute_time = 0;
  core.recompute_brightness(10000);  // Way past full duration
  EXPECT_FLOAT_EQ(core.brightness, 1.0f);
}

TEST_F(LightCoreTest, RecomputeBrightness_ClampsTo0) {
  core.brightness = 0.5f;
  core.dim_direction = DimDirection::DOWN;
  core.last_recompute_time = 0;
  core.recompute_brightness(10000);
  EXPECT_FLOAT_EQ(core.brightness, 0.0f);
}

TEST_F(LightCoreTest, RecomputeBrightness_NoneDirectionNoChange) {
  core.brightness = 0.5f;
  core.dim_direction = DimDirection::NONE;
  core.recompute_brightness(5000);
  EXPECT_FLOAT_EQ(core.brightness, 0.5f);
}

TEST_F(LightCoreTest, RecomputeBrightness_ZeroDurationNoChange) {
  core.config.dim_duration_ms = 0;
  core.brightness = 0.5f;
  core.dim_direction = DimDirection::UP;
  core.recompute_brightness(5000);
  EXPECT_FLOAT_EQ(core.brightness, 0.5f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Target Detection
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightCoreTest, IsAtTarget_DimmingUpReached) {
  core.target_brightness = 0.5f;
  core.brightness = 0.5f;
  core.dim_direction = DimDirection::UP;
  EXPECT_TRUE(core.is_at_target());
}

TEST_F(LightCoreTest, IsAtTarget_DimmingDownReached) {
  core.target_brightness = 0.3f;
  core.brightness = 0.3f;
  core.dim_direction = DimDirection::DOWN;
  EXPECT_TRUE(core.is_at_target());
}

TEST_F(LightCoreTest, IsAtTarget_NotYetReached) {
  core.target_brightness = 0.8f;
  core.brightness = 0.5f;
  core.dim_direction = DimDirection::UP;
  EXPECT_FALSE(core.is_at_target());
}

TEST_F(LightCoreTest, IsAtTarget_NoDimmingAlwaysFalse) {
  core.dim_direction = DimDirection::NONE;
  EXPECT_FALSE(core.is_at_target());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Write Action Computation
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightCoreTest, ComputeWriteAction_TurnOff) {
  auto action = core.compute_write_action(false, 0.0f);
  EXPECT_EQ(action.command, packet::command::DOWN);
  EXPECT_FALSE(action.start_dimming);
}

TEST_F(LightCoreTest, ComputeWriteAction_TurnOnNoDim) {
  core.config.dim_duration_ms = 0;
  auto action = core.compute_write_action(true, 1.0f);
  EXPECT_EQ(action.command, packet::command::UP);
  EXPECT_FALSE(action.start_dimming);
}

TEST_F(LightCoreTest, ComputeWriteAction_FullBrightness) {
  auto action = core.compute_write_action(true, 1.0f);
  EXPECT_EQ(action.command, packet::command::UP);
  EXPECT_FALSE(action.start_dimming);
}

TEST_F(LightCoreTest, ComputeWriteAction_DimUpFromCurrent) {
  core.brightness = 0.3f;
  core.is_on = true;
  auto action = core.compute_write_action(true, 0.8f);
  EXPECT_EQ(action.command, core.command_dim_up);
  EXPECT_TRUE(action.start_dimming);
  EXPECT_EQ(action.dim_dir, DimDirection::UP);
}

TEST_F(LightCoreTest, ComputeWriteAction_DimDownFromCurrent) {
  core.brightness = 0.8f;
  core.is_on = true;
  auto action = core.compute_write_action(true, 0.3f);
  EXPECT_EQ(action.command, core.command_dim_down);
  EXPECT_TRUE(action.start_dimming);
  EXPECT_EQ(action.dim_dir, DimDirection::DOWN);
}

TEST_F(LightCoreTest, ComputeWriteAction_WithinToleranceNoAction) {
  core.brightness = 0.5f;
  core.is_on = true;
  auto action = core.compute_write_action(true, 0.505f);
  EXPECT_EQ(action.command, 0);
  EXPECT_FALSE(action.start_dimming);
}

TEST_F(LightCoreTest, ComputeWriteAction_FromOffTurnsOnFirst) {
  core.brightness = 0.0f;
  core.is_on = false;
  auto action = core.compute_write_action(true, 0.5f);
  EXPECT_EQ(action.command, core.command_on);
  EXPECT_FALSE(action.start_dimming);  // Needs second pass
}

// ═══════════════════════════════════════════════════════════════════════════════
// Apply Write Action
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightCoreTest, ApplyWriteAction_TurnOff) {
  core.is_on = true;
  core.brightness = 1.0f;
  LightCore::WriteAction action;
  action.command = core.command_off;
  core.apply_write_action(false, 0.0f, action, 0);
  EXPECT_FALSE(core.is_on);
  EXPECT_FLOAT_EQ(core.brightness, 0.0f);
}

TEST_F(LightCoreTest, ApplyWriteAction_StartDimming) {
  core.is_on = true;
  core.brightness = 0.3f;
  LightCore::WriteAction action;
  action.command = core.command_dim_up;
  action.start_dimming = true;
  action.dim_dir = DimDirection::UP;
  core.apply_write_action(true, 0.8f, action, 1000);
  EXPECT_EQ(core.dim_direction, DimDirection::UP);
  EXPECT_FLOAT_EQ(core.target_brightness, 0.8f);
  EXPECT_EQ(core.dimming_start, 1000u);
  EXPECT_EQ(core.last_recompute_time, 1000u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Turn Off
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightCoreTest, TurnOff_ResetsAllState) {
  core.is_on = true;
  core.brightness = 0.7f;
  core.dim_direction = DimDirection::UP;
  core.turn_off();
  EXPECT_FALSE(core.is_on);
  EXPECT_FLOAT_EQ(core.brightness, 0.0f);
  EXPECT_EQ(core.dim_direction, DimDirection::NONE);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Supports Brightness
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightCoreTest, SupportsBrightness_WithDimDuration) {
  core.config.dim_duration_ms = 5000;
  EXPECT_TRUE(core.supports_brightness());
}

TEST_F(LightCoreTest, SupportsBrightness_WithoutDimDuration) {
  core.config.dim_duration_ms = 0;
  EXPECT_FALSE(core.supports_brightness());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Operation String
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightCoreTest, OperationStr_DimmingUp) {
  core.dim_direction = DimDirection::UP;
  EXPECT_STREQ(core.operation_str(), "dimming_up");
}

TEST_F(LightCoreTest, OperationStr_DimmingDown) {
  core.dim_direction = DimDirection::DOWN;
  EXPECT_STREQ(core.operation_str(), "dimming_down");
}

TEST_F(LightCoreTest, OperationStr_Idle) {
  core.dim_direction = DimDirection::NONE;
  EXPECT_STREQ(core.operation_str(), "idle");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Reset
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightCoreTest, Reset_ClearsAllState) {
  core.is_on = true;
  core.brightness = 0.7f;
  core.dim_direction = DimDirection::UP;
  core.target_brightness = 0.9f;
  core.dimming_start = 1000;
  core.last_recompute_time = 2000;
  core.reset();
  EXPECT_FALSE(core.is_on);
  EXPECT_FLOAT_EQ(core.brightness, 0.0f);
  EXPECT_EQ(core.dim_direction, DimDirection::NONE);
  EXPECT_FLOAT_EQ(core.target_brightness, 0.0f);
  EXPECT_EQ(core.dimming_start, 0u);
  EXPECT_EQ(core.last_recompute_time, 0u);
}
