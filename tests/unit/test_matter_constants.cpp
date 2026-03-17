/// @file test_matter_constants.cpp
/// @brief Unit tests for Matter ↔ Elero conversion and command routing.
///
/// Tests the production code in matter_constants.h — the same inline functions
/// used by matter_adapter.cpp. Focus on the semantic inversion (Elero open=1
/// vs Matter open=0), clamping, and the target-to-command decision logic.

#include <gtest/gtest.h>
#include "elero_matter/matter_constants.h"
#include "elero/elero_packet.h"

using namespace esphome::elero;

// ═══════════════════════════════════════════════════════════════════════════════
// POSITION CONVERSION — validates the semantic inversion and clamping
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MatterPosition, FullyOpenRoundTrip) {
    EXPECT_EQ(matter::elero_to_matter_position(1.0f), 0);
    EXPECT_FLOAT_EQ(matter::matter_to_elero_position(0), 1.0f);
}

TEST(MatterPosition, FullyClosedRoundTrip) {
    EXPECT_EQ(matter::elero_to_matter_position(0.0f), 10000);
    EXPECT_FLOAT_EQ(matter::matter_to_elero_position(10000), 0.0f);
}

TEST(MatterPosition, Halfway) {
    EXPECT_EQ(matter::elero_to_matter_position(0.5f), 5000);
    EXPECT_FLOAT_EQ(matter::matter_to_elero_position(5000), 0.5f);
}

TEST(MatterPosition, InverseMonotonicity) {
    for (int i = 0; i < 10; ++i) {
        float lo = static_cast<float>(i) / 10.0f;
        float hi = static_cast<float>(i + 1) / 10.0f;
        EXPECT_GT(matter::elero_to_matter_position(lo),
                  matter::elero_to_matter_position(hi));
    }
}

TEST(MatterPosition, ClampsOutOfRange) {
    // Negative → clamped to 0.0 → Matter 10000
    EXPECT_EQ(matter::elero_to_matter_position(-0.1f), 10000);
    // Over 1.0 → clamped to 1.0 → Matter 0
    EXPECT_EQ(matter::elero_to_matter_position(1.1f), 0);
    // Matter > 10000 → clamped
    EXPECT_FLOAT_EQ(matter::matter_to_elero_position(15000), 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BRIGHTNESS / LEVEL CONVERSION — boundaries, clamping, roundtrip
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MatterLevel, Boundaries) {
    EXPECT_EQ(matter::elero_to_matter_level(0.0f), 0);
    EXPECT_EQ(matter::elero_to_matter_level(1.0f), 254);
    EXPECT_FLOAT_EQ(matter::matter_to_elero_brightness(0), 0.0f);
    EXPECT_FLOAT_EQ(matter::matter_to_elero_brightness(254), 1.0f);
}

TEST(MatterLevel, RoundTripFidelity) {
    for (int l = 0; l <= 254; ++l) {
        float b = matter::matter_to_elero_brightness(static_cast<uint8_t>(l));
        uint8_t l2 = matter::elero_to_matter_level(b);
        EXPECT_NEAR(l, l2, 1) << "level=" << l;
    }
}

TEST(MatterLevel, ClampsOutOfRange) {
    // Negative brightness → clamped to 0
    EXPECT_EQ(matter::elero_to_matter_level(-0.5f), 0);
    // Over 1.0 → clamped to 254
    EXPECT_EQ(matter::elero_to_matter_level(1.5f), 254);
    // Level 255 (null/reserved) → clamped to 254 → brightness 1.0
    EXPECT_FLOAT_EQ(matter::matter_to_elero_brightness(255), 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// OPERATIONAL STATUS BITMAP
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MatterOpStatus, BitmapEncoding) {
    EXPECT_EQ(matter::operation_to_matter_status(cover_sm::Operation::IDLE),
              0x00);
    EXPECT_EQ(matter::operation_to_matter_status(cover_sm::Operation::OPENING),
              0x05);
    EXPECT_EQ(matter::operation_to_matter_status(cover_sm::Operation::CLOSING),
              0x0A);
}

// ═══════════════════════════════════════════════════════════════════════════════
// TARGET POSITION → COMMAND BYTE
//
// Production code used by MatterAdapter::handle_cover_command_.
// Tests the full pipeline: Matter position → Elero position → RF command.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MatterCoverCommand, MatterFullyOpen_SendsUp) {
    float target = matter::matter_to_elero_position(0);
    EXPECT_EQ(matter::target_position_to_command(target, 0.5f),
              packet::command::UP);
    EXPECT_EQ(matter::target_position_to_command(target, 0.0f),
              packet::command::UP);
}

TEST(MatterCoverCommand, MatterFullyClosed_SendsDown) {
    float target = matter::matter_to_elero_position(10000);
    EXPECT_EQ(matter::target_position_to_command(target, 0.5f),
              packet::command::DOWN);
    EXPECT_EQ(matter::target_position_to_command(target, 1.0f),
              packet::command::DOWN);
}

TEST(MatterCoverCommand, IntermediateAboveCurrent_SendsUp) {
    EXPECT_EQ(matter::target_position_to_command(0.7f, 0.3f),
              packet::command::UP);
}

TEST(MatterCoverCommand, IntermediateBelowCurrent_SendsDown) {
    EXPECT_EQ(matter::target_position_to_command(0.3f, 0.7f),
              packet::command::DOWN);
}

TEST(MatterCoverCommand, MatterHalfway_FromClosed_SendsUp) {
    float target = matter::matter_to_elero_position(5000);
    EXPECT_EQ(matter::target_position_to_command(target, 0.0f),
              packet::command::UP);
}

TEST(MatterCoverCommand, MatterHalfway_FromOpen_SendsDown) {
    float target = matter::matter_to_elero_position(5000);
    EXPECT_EQ(matter::target_position_to_command(target, 1.0f),
              packet::command::DOWN);
}

TEST(MatterCoverCommand, AlreadyAtTarget_ReturnsInvalid) {
    // When cover is already at target, no command should be sent
    EXPECT_EQ(matter::target_position_to_command(0.5f, 0.5f),
              packet::command::INVALID);
    EXPECT_EQ(matter::target_position_to_command(0.505f, 0.5f),
              packet::command::INVALID);
}

TEST(MatterCoverCommand, ThresholdBoundary_AtOpen) {
    // target ≥ 0.99 → always UP (even if current is 1.0)
    EXPECT_EQ(matter::target_position_to_command(0.99f, 1.0f),
              packet::command::UP);
}

TEST(MatterCoverCommand, ThresholdBoundary_AtClosed) {
    // target ≤ 0.01 → always DOWN (even if current is 0.0)
    EXPECT_EQ(matter::target_position_to_command(0.01f, 0.0f),
              packet::command::DOWN);
}

TEST(MatterCoverCommand, FullPipeline_MatterToRf) {
    // Matter 2500 = 75% open in Elero. Cover at 30%. Expected: UP.
    float target = matter::matter_to_elero_position(2500);
    EXPECT_NEAR(target, 0.75f, 0.001f);
    EXPECT_EQ(matter::target_position_to_command(target, 0.3f),
              packet::command::UP);
}

TEST(MatterCoverCommand, FullPipeline_StopMotion) {
    // StopMotion is handled in the adapter via OperationalStatus = 0.
    // target_position_to_command is not called for STOP — verify it returns
    // INVALID for equal positions so the caller skips the send.
    float pos = matter::matter_to_elero_position(3000);
    EXPECT_EQ(matter::target_position_to_command(pos, pos),
              packet::command::INVALID);
}
