/// @file test_light_sm.cpp
/// @brief Unit tests for light_sm — variant-based light state machine.

#include <gtest/gtest.h>
#include "elero/light_sm.h"
#include "elero/elero_packet.h"

namespace sm = esphome::elero::light_sm;
namespace pkt = esphome::elero::packet;

// ═══════════════════════════════════════════════════════════════════════════════
// FIXTURES
// ═══════════════════════════════════════════════════════════════════════════════

class LightSmTest : public ::testing::Test {
 protected:
    sm::Context ctx{.dim_duration_ms = 5000};   // 5s for full dim range
    sm::Context no_dim{.dim_duration_ms = 0};   // on/off only
};

// ═══════════════════════════════════════════════════════════════════════════════
// BRIGHTNESS DERIVATION
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightSmTest, BrightnessInterpolatesDuringDimmingUp) {
    sm::State s = sm::DimmingUp{0.0f, 1.0f, 1000};
    // 2.5s of 5s → 0.5
    EXPECT_NEAR(sm::brightness(s, 3500, ctx), 0.5f, 0.01f);
}

TEST_F(LightSmTest, BrightnessInterpolatesDuringDimmingDown) {
    sm::State s = sm::DimmingDown{1.0f, 0.0f, 1000};
    EXPECT_NEAR(sm::brightness(s, 3500, ctx), 0.5f, 0.01f);
}

TEST_F(LightSmTest, BrightnessClampsAtTarget) {
    // DimmingUp past target
    sm::State up = sm::DimmingUp{0.0f, 0.8f, 1000};
    EXPECT_FLOAT_EQ(sm::brightness(up, 20000, ctx), 0.8f);

    // DimmingDown past target
    sm::State down = sm::DimmingDown{1.0f, 0.3f, 1000};
    EXPECT_FLOAT_EQ(sm::brightness(down, 20000, ctx), 0.3f);
}

TEST_F(LightSmTest, BrightnessPartialRange) {
    // Dimming from 0.2 to 0.6 — range is 0.4, full duration is 5s
    // So this dim covers 0.4/1.0 * 5s = 2s of progress
    // After 1s: 0.2 + 1/5 = 0.4
    sm::State s = sm::DimmingUp{0.2f, 0.6f, 0};
    EXPECT_NEAR(sm::brightness(s, 1000, ctx), 0.4f, 0.01f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// RF STATUS — ON/OFF FROM ANY STATE
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightSmTest, RfLightOnTurnsOnFromOff) {
    auto s = sm::on_rf_status(sm::Off{}, pkt::state::LIGHT_ON, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::On>(s));
    EXPECT_FLOAT_EQ(std::get<sm::On>(s).brightness, 1.0f);
}

TEST_F(LightSmTest, RfLightOffTurnsOffFromOn) {
    auto s = sm::on_rf_status(sm::On{0.5f}, pkt::state::LIGHT_OFF, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::Off>(s));
}

TEST_F(LightSmTest, RfLightOffCancelsDimmingUp) {
    // Mid-dimming → RF says off → dimming intent discarded
    auto s = sm::on_rf_status(sm::DimmingUp{0.3f, 0.8f, 0}, pkt::state::LIGHT_OFF, 5000, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::Off>(s));
}

TEST_F(LightSmTest, RfLightOnCancelsDimmingDown) {
    // Dimming down → RF says on → jumps to On{1.0}
    auto s = sm::on_rf_status(sm::DimmingDown{0.8f, 0.3f, 0}, pkt::state::LIGHT_ON, 5000, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::On>(s));
    EXPECT_FLOAT_EQ(std::get<sm::On>(s).brightness, 1.0f);
}

TEST_F(LightSmTest, RfTopAliasesLightOnRfBottomAliasesLightOff) {
    auto on = sm::on_rf_status(sm::Off{}, pkt::state::TOP, 100, ctx);
    EXPECT_TRUE(std::holds_alternative<sm::On>(on));

    auto off = sm::on_rf_status(sm::On{1.0f}, pkt::state::BOTTOM, 100, ctx);
    EXPECT_TRUE(std::holds_alternative<sm::Off>(off));
}

TEST_F(LightSmTest, RfUnrelatedStatesAreNoOp) {
    auto s1 = sm::on_rf_status(sm::On{0.5f}, pkt::state::INTERMEDIATE, 100, ctx);
    EXPECT_TRUE(std::holds_alternative<sm::On>(s1));

    auto s2 = sm::on_rf_status(sm::Off{}, pkt::state::MOVING_UP, 100, ctx);
    EXPECT_TRUE(std::holds_alternative<sm::Off>(s2));

    auto s3 = sm::on_rf_status(sm::DimmingUp{0.0f, 1.0f, 0}, pkt::state::UNKNOWN, 100, ctx);
    EXPECT_TRUE(std::holds_alternative<sm::DimmingUp>(s3));
}

// ═══════════════════════════════════════════════════════════════════════════════
// SET BRIGHTNESS — TRANSITIONS
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightSmTest, SetBrightnessZeroTurnsOff) {
    auto s = sm::on_set_brightness(sm::On{0.5f}, 0.0f, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::Off>(s));
}

TEST_F(LightSmTest, SetBrightnessNoDimInstantOn) {
    auto s = sm::on_set_brightness(sm::Off{}, 0.7f, 100, no_dim);
    ASSERT_TRUE(std::holds_alternative<sm::On>(s));
    EXPECT_FLOAT_EQ(std::get<sm::On>(s).brightness, 0.7f);
}

TEST_F(LightSmTest, SetBrightnessUpStartsDimmingWithCorrectParams) {
    auto s = sm::on_set_brightness(sm::On{0.3f}, 0.8f, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingUp>(s));
    auto &dim = std::get<sm::DimmingUp>(s);
    EXPECT_FLOAT_EQ(dim.start_brightness, 0.3f);
    EXPECT_FLOAT_EQ(dim.target_brightness, 0.8f);
    EXPECT_EQ(dim.start_ms, 100u);
}

TEST_F(LightSmTest, SetBrightnessDownStartsDimmingDown) {
    auto s = sm::on_set_brightness(sm::On{0.8f}, 0.3f, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingDown>(s));
    auto &dim = std::get<sm::DimmingDown>(s);
    EXPECT_FLOAT_EQ(dim.start_brightness, 0.8f);
    EXPECT_FLOAT_EQ(dim.target_brightness, 0.3f);
}

TEST_F(LightSmTest, SetBrightnessFromOffStartsDimmingUp) {
    auto s = sm::on_set_brightness(sm::Off{}, 0.6f, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingUp>(s));
    auto &dim = std::get<sm::DimmingUp>(s);
    EXPECT_FLOAT_EQ(dim.start_brightness, 0.0f);
    EXPECT_FLOAT_EQ(dim.target_brightness, 0.6f);
}

TEST_F(LightSmTest, SetBrightnessWithinEpsilonIsNoOp) {
    // Difference < BRIGHTNESS_EPSILON (0.01) → stays On at target
    auto s = sm::on_set_brightness(sm::On{0.5f}, 0.505f, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::On>(s));
    EXPECT_FLOAT_EQ(std::get<sm::On>(s).brightness, 0.505f);
}

TEST_F(LightSmTest, SetBrightnessJustOutsideEpsilonStartsDimming) {
    // Difference > BRIGHTNESS_EPSILON → starts dimming
    auto s = sm::on_set_brightness(sm::On{0.5f}, 0.52f, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingUp>(s));
}

// ═══════════════════════════════════════════════════════════════════════════════
// SET BRIGHTNESS — CLAMPING
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightSmTest, NegativeBrightnessClampedToOff) {
    auto s = sm::on_set_brightness(sm::On{0.5f}, -0.5f, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::Off>(s));
}

TEST_F(LightSmTest, BrightnessAboveOneClampedToOne) {
    auto s = sm::on_set_brightness(sm::On{0.5f}, 1.5f, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingUp>(s));
    EXPECT_FLOAT_EQ(std::get<sm::DimmingUp>(s).target_brightness, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SET BRIGHTNESS — RE-TARGETING MID-DIM
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightSmTest, RetargetMidDimReverses) {
    // Dimming up from 0.0 to 0.8, at t=2500 brightness ≈ 0.5
    sm::State s = sm::DimmingUp{0.0f, 0.8f, 0};

    // Re-target to 0.3 at t=2500 → should reverse to DimmingDown from ~0.5
    s = sm::on_set_brightness(s, 0.3f, 2500, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingDown>(s));
    auto &dim = std::get<sm::DimmingDown>(s);
    EXPECT_NEAR(dim.start_brightness, 0.5f, 0.01f);
    EXPECT_FLOAT_EQ(dim.target_brightness, 0.3f);
}

TEST_F(LightSmTest, RetargetMidDimSameDirection) {
    // Dimming up from 0.0 to 0.8, at t=1500 brightness ≈ 0.3
    sm::State s = sm::DimmingUp{0.0f, 0.8f, 0};

    // Re-target to 0.6 at t=1500 → still DimmingUp but from ~0.3 to 0.6
    s = sm::on_set_brightness(s, 0.6f, 1500, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingUp>(s));
    auto &dim = std::get<sm::DimmingUp>(s);
    EXPECT_NEAR(dim.start_brightness, 0.3f, 0.01f);
    EXPECT_FLOAT_EQ(dim.target_brightness, 0.6f);
}

TEST_F(LightSmTest, RetargetToZeroCancelsDimmingAndTurnsOff) {
    sm::State s = sm::DimmingUp{0.3f, 0.8f, 0};
    s = sm::on_set_brightness(s, 0.0f, 2000, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::Off>(s));
}

// ═══════════════════════════════════════════════════════════════════════════════
// TURN ON / OFF — EDGE CASES
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightSmTest, TurnOnResetsToFullBrightness) {
    // Light dimmed to 30% → turn_on should set to 100%, not preserve 30%
    auto s = sm::on_turn_on(sm::On{0.3f}, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::On>(s));
    EXPECT_FLOAT_EQ(std::get<sm::On>(s).brightness, 1.0f);
}

TEST_F(LightSmTest, TurnOnCancelsDimming) {
    auto s = sm::on_turn_on(sm::DimmingDown{0.8f, 0.3f, 0}, 100, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::On>(s));
    EXPECT_FLOAT_EQ(std::get<sm::On>(s).brightness, 1.0f);
}

TEST_F(LightSmTest, TurnOffFromDimmingDown) {
    auto s = sm::on_turn_off(sm::DimmingDown{0.8f, 0.3f, 100});
    ASSERT_TRUE(std::holds_alternative<sm::Off>(s));
}

TEST_F(LightSmTest, TurnOffFromDimmingUp) {
    auto s = sm::on_turn_off(sm::DimmingUp{0.3f, 0.8f, 100});
    ASSERT_TRUE(std::holds_alternative<sm::Off>(s));
}

// ═══════════════════════════════════════════════════════════════════════════════
// TICK — DIMMING COMPLETION
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightSmTest, DimmingUpCompletesAtTargetBrightness) {
    sm::State s = sm::DimmingUp{0.0f, 0.8f, 1000};
    auto result = sm::on_tick(s, 10000, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::On>(result));
    EXPECT_FLOAT_EQ(std::get<sm::On>(result).brightness, 0.8f);
}

TEST_F(LightSmTest, DimmingUpNotYetCompleteStaysDimming) {
    sm::State s = sm::DimmingUp{0.0f, 1.0f, 1000};
    auto result = sm::on_tick(s, 2000, ctx);
    EXPECT_TRUE(std::holds_alternative<sm::DimmingUp>(result));
}

TEST_F(LightSmTest, DimmingDownCompletesAtTargetBrightness) {
    sm::State s = sm::DimmingDown{1.0f, 0.3f, 1000};
    auto result = sm::on_tick(s, 10000, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::On>(result));
    EXPECT_FLOAT_EQ(std::get<sm::On>(result).brightness, 0.3f);
}

TEST_F(LightSmTest, DimmingDownToZeroTurnsOff) {
    sm::State s = sm::DimmingDown{0.5f, 0.0f, 1000};
    auto result = sm::on_tick(s, 10000, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::Off>(result));
}

// ═══════════════════════════════════════════════════════════════════════════════
// MULTI-STEP: FULL DIM CYCLE
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightSmTest, FullDimCycleOffToBrightnessToOff) {
    // Off → set 50% brightness
    sm::State s = sm::on_set_brightness(sm::Off{}, 0.5f, 0, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingUp>(s));

    // After 2.5s of 5s → brightness ≈ 0.5
    EXPECT_NEAR(sm::brightness(s, 2500, ctx), 0.5f, 0.02f);

    // Tick at completion → On{0.5}
    s = sm::on_tick(s, 3000, ctx);  // 0.5 target reached within epsilon
    ASSERT_TRUE(std::holds_alternative<sm::On>(s));
    EXPECT_FLOAT_EQ(std::get<sm::On>(s).brightness, 0.5f);

    // Now dim to zero → Off
    s = sm::on_set_brightness(s, 0.0f, 4000, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::Off>(s));
}

TEST_F(LightSmTest, FullCycleWithRfInterrupt) {
    // Start dimming up
    sm::State s = sm::on_set_brightness(sm::Off{}, 0.8f, 0, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingUp>(s));

    // At t=1000, RF says light is off (physical switch)
    s = sm::on_rf_status(s, pkt::state::LIGHT_OFF, 1000, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::Off>(s));

    // Re-start dim from off
    s = sm::on_set_brightness(s, 0.8f, 2000, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingUp>(s));
    EXPECT_FLOAT_EQ(std::get<sm::DimmingUp>(s).start_brightness, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// MULTI-STEP: SLIDER DRAG (rapid re-targeting)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(LightSmTest, SliderDragMultipleRetargets) {
    // User drags slider: Off → 0.3 → 0.6 → 0.4
    sm::State s = sm::on_set_brightness(sm::Off{}, 0.3f, 0, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingUp>(s));

    // After 500ms, brightness ≈ 0.1, retarget to 0.6
    s = sm::on_set_brightness(s, 0.6f, 500, ctx);
    ASSERT_TRUE(std::holds_alternative<sm::DimmingUp>(s));
    EXPECT_NEAR(std::get<sm::DimmingUp>(s).start_brightness, 0.1f, 0.02f);
    EXPECT_FLOAT_EQ(std::get<sm::DimmingUp>(s).target_brightness, 0.6f);

    // After another 500ms, retarget down to 0.4
    float current = sm::brightness(s, 1000, ctx);
    s = sm::on_set_brightness(s, 0.4f, 1000, ctx);
    // Should reverse direction if current > 0.4
    if (current > 0.4f) {
        EXPECT_TRUE(std::holds_alternative<sm::DimmingDown>(s));
    } else {
        EXPECT_TRUE(std::holds_alternative<sm::DimmingUp>(s));
    }
}
