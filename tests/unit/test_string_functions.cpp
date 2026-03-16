/// @file test_string_functions.cpp
/// @brief Tests for string conversion functions used in HA state display and web UI.
///
/// Tests elero_state_to_string(), elero_command_to_string(), and
/// elero_action_to_command() — all pure functions with zero hardware deps.

#include <gtest/gtest.h>
#include <cstring>
#include "elero/elero_strings.h"
#include "elero/elero_packet.h"

using namespace esphome::elero;
using namespace esphome::elero::packet;

// =============================================================================
// elero_state_to_string — maps state bytes to Home Assistant text sensor values
// =============================================================================

TEST(StateToString, AllKnownStates) {
  EXPECT_STREQ(elero_state_to_string(state::TOP), "top");
  EXPECT_STREQ(elero_state_to_string(state::BOTTOM), "bottom");
  EXPECT_STREQ(elero_state_to_string(state::INTERMEDIATE), "intermediate");
  EXPECT_STREQ(elero_state_to_string(state::TILT), "tilt");
  EXPECT_STREQ(elero_state_to_string(state::BLOCKING), "blocking");
  EXPECT_STREQ(elero_state_to_string(state::OVERHEATED), "overheated");
  EXPECT_STREQ(elero_state_to_string(state::TIMEOUT), "timeout");
  EXPECT_STREQ(elero_state_to_string(state::START_MOVING_UP), "start_moving_up");
  EXPECT_STREQ(elero_state_to_string(state::START_MOVING_DOWN), "start_moving_down");
  EXPECT_STREQ(elero_state_to_string(state::MOVING_UP), "moving_up");
  EXPECT_STREQ(elero_state_to_string(state::MOVING_DOWN), "moving_down");
  EXPECT_STREQ(elero_state_to_string(state::STOPPED), "stopped");
  EXPECT_STREQ(elero_state_to_string(state::TOP_TILT), "top_tilt");
  EXPECT_STREQ(elero_state_to_string(state::BOTTOM_TILT), "bottom_tilt");
  EXPECT_STREQ(elero_state_to_string(state::LIGHT_ON), "on");
}

TEST(StateToString, UnknownState) {
  EXPECT_STREQ(elero_state_to_string(state::UNKNOWN), "unknown");
  EXPECT_STREQ(elero_state_to_string(0xFF), "unknown");
  EXPECT_STREQ(elero_state_to_string(0x99), "unknown");
}

TEST(StateToString, LightOffAliasesBottomTilt) {
  // LIGHT_OFF and BOTTOM_TILT are both 0x0f
  EXPECT_EQ(state::LIGHT_OFF, state::BOTTOM_TILT);
  EXPECT_STREQ(elero_state_to_string(state::LIGHT_OFF), "bottom_tilt");
}

// =============================================================================
// elero_command_to_string — maps command bytes to log-friendly names
// =============================================================================

TEST(CommandToString, AllKnownCommands) {
  EXPECT_STREQ(elero_command_to_string(command::CHECK), "CHECK");
  EXPECT_STREQ(elero_command_to_string(command::STOP), "STOP");
  EXPECT_STREQ(elero_command_to_string(command::UP), "UP");
  EXPECT_STREQ(elero_command_to_string(command::TILT), "TILT");
  EXPECT_STREQ(elero_command_to_string(command::DOWN), "DOWN");
  EXPECT_STREQ(elero_command_to_string(command::INTERMEDIATE), "INTERMEDIATE");
}

TEST(CommandToString, UnknownCommand) {
  EXPECT_STREQ(elero_command_to_string(0xFF), "UNKNOWN");
  EXPECT_STREQ(elero_command_to_string(0x99), "UNKNOWN");
}

// =============================================================================
// elero_action_to_command — web UI action string to RF command byte
// =============================================================================

TEST(ActionToCommand, PrimaryActions) {
  EXPECT_EQ(elero_action_to_command("up"), command::UP);
  EXPECT_EQ(elero_action_to_command("down"), command::DOWN);
  EXPECT_EQ(elero_action_to_command("stop"), command::STOP);
  EXPECT_EQ(elero_action_to_command("check"), command::CHECK);
  EXPECT_EQ(elero_action_to_command("tilt"), command::TILT);
  EXPECT_EQ(elero_action_to_command("int"), command::INTERMEDIATE);
}

TEST(ActionToCommand, Aliases) {
  EXPECT_EQ(elero_action_to_command("open"), command::UP);
  EXPECT_EQ(elero_action_to_command("close"), command::DOWN);
}

TEST(ActionToCommand, InvalidActions) {
  EXPECT_EQ(elero_action_to_command(nullptr), command::INVALID);
  EXPECT_EQ(elero_action_to_command(""), command::INVALID);
  EXPECT_EQ(elero_action_to_command("UP"), command::INVALID);  // Case sensitive
  EXPECT_EQ(elero_action_to_command("Down"), command::INVALID);
  EXPECT_EQ(elero_action_to_command("garbage"), command::INVALID);
  EXPECT_EQ(elero_action_to_command("  up"), command::INVALID);  // Leading space
}

TEST(ActionToCommand, RoundtripWithCommandToString) {
  // Verify that action names match the command bytes they map to
  EXPECT_STREQ(elero_command_to_string(elero_action_to_command("up")), "UP");
  EXPECT_STREQ(elero_command_to_string(elero_action_to_command("down")), "DOWN");
  EXPECT_STREQ(elero_command_to_string(elero_action_to_command("stop")), "STOP");
  EXPECT_STREQ(elero_command_to_string(elero_action_to_command("check")), "CHECK");
  EXPECT_STREQ(elero_command_to_string(elero_action_to_command("tilt")), "TILT");
  EXPECT_STREQ(elero_command_to_string(elero_action_to_command("int")), "INTERMEDIATE");
}

// =============================================================================
// elero_command_to_action — command byte to action string (inverse of above)
// =============================================================================

TEST(CommandToAction, AllKnownCommands) {
  EXPECT_STREQ(elero_command_to_action(command::UP), "up");
  EXPECT_STREQ(elero_command_to_action(command::DOWN), "down");
  EXPECT_STREQ(elero_command_to_action(command::STOP), "stop");
  EXPECT_STREQ(elero_command_to_action(command::CHECK), "check");
  EXPECT_STREQ(elero_command_to_action(command::TILT), "tilt");
  EXPECT_STREQ(elero_command_to_action(command::INTERMEDIATE), "int");
}

TEST(CommandToAction, UnknownCommand) {
  EXPECT_EQ(elero_command_to_action(0xFF), nullptr);
  EXPECT_EQ(elero_command_to_action(0x99), nullptr);
}

TEST(CommandToAction, RoundtripWithActionToCommand) {
  // command byte → action string → command byte
  EXPECT_EQ(elero_action_to_command(elero_command_to_action(command::UP)), command::UP);
  EXPECT_EQ(elero_action_to_command(elero_command_to_action(command::DOWN)), command::DOWN);
  EXPECT_EQ(elero_action_to_command(elero_command_to_action(command::STOP)), command::STOP);
  EXPECT_EQ(elero_action_to_command(elero_command_to_action(command::CHECK)), command::CHECK);
  EXPECT_EQ(elero_action_to_command(elero_command_to_action(command::TILT)), command::TILT);
  EXPECT_EQ(elero_action_to_command(elero_command_to_action(command::INTERMEDIATE)), command::INTERMEDIATE);
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
