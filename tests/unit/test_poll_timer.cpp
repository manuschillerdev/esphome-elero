/// @file test_poll_timer.cpp
/// @brief Unit tests for PollTimer — poll timing logic for cover status queries.

#include <gtest/gtest.h>
#include <climits>

#include "elero/elero_packet.h"
#include "elero/poll_timer.h"

using namespace esphome::elero;

namespace timing = packet::timing;

// =============================================================================
// FIXTURES
// =============================================================================

class PollTimerTest : public ::testing::Test {
 protected:
    PollTimer timer{};

    /// Timer with short interval for easier testing
    PollTimer fast_timer{.interval_ms = 5000};

    /// Timer with stagger offset
    PollTimer stagger_timer{.interval_ms = 10000, .offset_ms = 3000};
};

// =============================================================================
// 1. BASIC INTERVAL POLLING
// =============================================================================

TEST_F(PollTimerTest, ShouldNotPollBeforeIntervalElapsed) {
    // Simulate that we already polled at t=1000
    timer.on_poll_sent(1000);
    timer.on_rf_received(1500);  // Got response, clear awaiting

    // Well before default interval (300s), should not poll
    EXPECT_FALSE(timer.should_poll(2000, false));
    EXPECT_FALSE(timer.should_poll(100000, false));
    EXPECT_FALSE(timer.should_poll(200000, false));
}

TEST_F(PollTimerTest, ShouldPollAfterIntervalElapsed) {
    timer.on_poll_sent(1000);
    timer.on_rf_received(1500);

    // on_rf_received sets last_poll_ms = 1500, so interval measured from there
    EXPECT_TRUE(timer.should_poll(1500 + timing::DEFAULT_POLL_INTERVAL_MS, false));
    EXPECT_TRUE(timer.should_poll(1500 + timing::DEFAULT_POLL_INTERVAL_MS + 1, false));
}

TEST_F(PollTimerTest, ShouldPollWithCustomInterval) {
    fast_timer.on_poll_sent(1000);
    fast_timer.on_rf_received(1500);

    // on_rf_received sets last_poll_ms = 1500, interval = 5000ms
    EXPECT_FALSE(fast_timer.should_poll(6000, false));
    EXPECT_TRUE(fast_timer.should_poll(6501, false));
}

// =============================================================================
// 2. MOVING VS IDLE INTERVAL
// =============================================================================

TEST_F(PollTimerTest, UsesMovingIntervalWhenMoving) {
    fast_timer.on_poll_sent(1000);
    fast_timer.on_rf_received(1500);

    // With is_moving=true, uses POLL_INTERVAL_MOVING (2000ms) not interval_ms (5000ms)
    EXPECT_FALSE(fast_timer.should_poll(2500, true));   // 1500ms since last poll
    EXPECT_TRUE(fast_timer.should_poll(3501, true));    // 2001ms since last_poll (rf_received set it to 1500)
}

TEST_F(PollTimerTest, UsesIdleIntervalWhenNotMoving) {
    fast_timer.on_poll_sent(1000);
    fast_timer.on_rf_received(1500);

    // With is_moving=false, uses interval_ms (5000ms)
    EXPECT_FALSE(fast_timer.should_poll(3500, false));  // 2000ms < 5000ms
    EXPECT_TRUE(fast_timer.should_poll(6501, false));   // 5001ms >= 5000ms
}

TEST_F(PollTimerTest, MovingIntervalIs2Seconds) {
    EXPECT_EQ(timing::POLL_INTERVAL_MOVING, 2000u);
}

// =============================================================================
// 3. RESPONSE WAIT (awaiting_response)
// =============================================================================

TEST_F(PollTimerTest, AfterCommandSentShouldNotPollDuringResponseWait) {
    fast_timer.on_poll_sent(1000);
    fast_timer.on_rf_received(1500);

    fast_timer.on_command_sent(10000);

    // Within RESPONSE_WAIT_MS (2000ms), should_poll returns false
    EXPECT_FALSE(fast_timer.should_poll(10500, false));
    EXPECT_FALSE(fast_timer.should_poll(11000, false));
    EXPECT_FALSE(fast_timer.should_poll(11999, false));
}

TEST_F(PollTimerTest, AfterPollSentShouldNotPollDuringResponseWait) {
    fast_timer.on_poll_sent(10000);

    // awaiting_response is true, within RESPONSE_WAIT_MS
    EXPECT_FALSE(fast_timer.should_poll(10500, false));
    EXPECT_FALSE(fast_timer.should_poll(11999, false));
}

TEST_F(PollTimerTest, TimeoutForcesRepollAfterResponseWait) {
    fast_timer.on_command_sent(10000);

    // After RESPONSE_WAIT_MS, timeout forces re-poll
    uint32_t timeout_time = 10000 + timing::RESPONSE_WAIT_MS;
    EXPECT_TRUE(fast_timer.should_poll(timeout_time, false));
}

TEST_F(PollTimerTest, TimeoutClearsAwaitingResponse) {
    // Establish a baseline: poll, receive response, then send a command
    fast_timer.on_poll_sent(5000);
    fast_timer.on_rf_received(5500);  // last_poll_ms = 5500

    fast_timer.on_command_sent(10000);

    uint32_t timeout_time = 10000 + timing::RESPONSE_WAIT_MS;
    EXPECT_TRUE(fast_timer.should_poll(timeout_time, false));

    // After timeout cleared awaiting_response, behavior returns to interval-based.
    // last_poll_ms is still 5500 (on_command_sent doesn't update it), so
    // the caller must call on_poll_sent() after acting on the re-poll.
    // Simulate that: on_poll_sent resets last_poll_ms, then verify interval.
    fast_timer.on_poll_sent(timeout_time);
    fast_timer.on_rf_received(timeout_time + 500);  // last_poll_ms = timeout_time + 500

    // Now interval-based: should not poll until interval_ms (5000) elapses
    EXPECT_FALSE(fast_timer.should_poll(timeout_time + 1000, false));
    EXPECT_TRUE(fast_timer.should_poll(timeout_time + 500 + 5000, false));
}

// =============================================================================
// 6. RF RESPONSE CLEARS AWAITING_RESPONSE
// =============================================================================

TEST_F(PollTimerTest, RfReceivedClearsAwaitingResponse) {
    fast_timer.on_command_sent(10000);
    EXPECT_TRUE(fast_timer.awaiting_response);

    fast_timer.on_rf_received(10500);
    EXPECT_FALSE(fast_timer.awaiting_response);
}

TEST_F(PollTimerTest, RfReceivedResetsLastPollMs) {
    fast_timer.on_poll_sent(10000);
    fast_timer.on_rf_received(10500);

    // last_poll_ms is now 10500, so next poll should be at 10500 + 5000
    EXPECT_FALSE(fast_timer.should_poll(15000, false));
    EXPECT_TRUE(fast_timer.should_poll(15501, false));
}

TEST_F(PollTimerTest, RfReceivedClearsImmediatePoll) {
    fast_timer.request_immediate_poll();
    EXPECT_TRUE(fast_timer.immediate_poll);

    fast_timer.on_rf_received(5000);
    EXPECT_FALSE(fast_timer.immediate_poll);
}

// =============================================================================
// 7. IMMEDIATE POLL
// =============================================================================

TEST_F(PollTimerTest, ImmediatePollReturnsTrueOnNextCheck) {
    fast_timer.on_poll_sent(1000);
    fast_timer.on_rf_received(1500);

    fast_timer.request_immediate_poll();
    // Even though interval hasn't elapsed, should_poll returns true
    EXPECT_TRUE(fast_timer.should_poll(2000, false));
}

TEST_F(PollTimerTest, ImmediatePollSetsFlag) {
    fast_timer.request_immediate_poll();
    EXPECT_TRUE(fast_timer.immediate_poll);
}

// =============================================================================
// 8. IMMEDIATE POLL DEBOUNCE
// =============================================================================

TEST_F(PollTimerTest, ImmediatePollIgnoredWhileAwaitingResponse) {
    fast_timer.on_command_sent(10000);
    EXPECT_TRUE(fast_timer.awaiting_response);

    fast_timer.request_immediate_poll();
    // Should be ignored — immediate_poll stays false
    EXPECT_FALSE(fast_timer.immediate_poll);
}

TEST_F(PollTimerTest, ImmediatePollWorksAfterResponseReceived) {
    fast_timer.on_command_sent(10000);
    fast_timer.on_rf_received(10500);
    EXPECT_FALSE(fast_timer.awaiting_response);

    fast_timer.request_immediate_poll();
    EXPECT_TRUE(fast_timer.immediate_poll);
    EXPECT_TRUE(fast_timer.should_poll(11000, false));
}

// =============================================================================
// 9. STAGGER OFFSET
// =============================================================================

TEST_F(PollTimerTest, FirstPollRespectsStaggerOffset) {
    // last_poll_ms is 0 (never polled), so first poll uses offset
    EXPECT_FALSE(stagger_timer.should_poll(0, false));
    EXPECT_FALSE(stagger_timer.should_poll(2999, false));
    EXPECT_TRUE(stagger_timer.should_poll(3000, false));
    EXPECT_TRUE(stagger_timer.should_poll(5000, false));
}

TEST_F(PollTimerTest, SubsequentPollsIgnoreOffset) {
    stagger_timer.on_poll_sent(3000);
    stagger_timer.on_rf_received(3500);

    // After first poll, uses interval_ms (10000ms), not offset
    EXPECT_FALSE(stagger_timer.should_poll(12000, false));
    EXPECT_TRUE(stagger_timer.should_poll(13501, false));
}

// =============================================================================
// 10. ON_POLL_SENT SETS AWAITING_RESPONSE
// =============================================================================

TEST_F(PollTimerTest, OnPollSentSetsAwaitingResponse) {
    EXPECT_FALSE(fast_timer.awaiting_response);

    fast_timer.on_poll_sent(5000);
    EXPECT_TRUE(fast_timer.awaiting_response);
}

TEST_F(PollTimerTest, OnPollSentSetsLastPollMs) {
    fast_timer.on_poll_sent(5000);
    EXPECT_EQ(fast_timer.last_poll_ms, 5000u);
}

TEST_F(PollTimerTest, OnPollSentSetsLastCommandMs) {
    fast_timer.on_poll_sent(5000);
    EXPECT_EQ(fast_timer.last_command_ms, 5000u);
}

TEST_F(PollTimerTest, OnPollSentClearsImmediatePoll) {
    fast_timer.request_immediate_poll();
    EXPECT_TRUE(fast_timer.immediate_poll);

    fast_timer.on_poll_sent(5000);
    EXPECT_FALSE(fast_timer.immediate_poll);
}

// =============================================================================
// 11. TIMEOUT -> RE-POLL -> ON_POLL_SENT CYCLE
// =============================================================================

TEST_F(PollTimerTest, TimeoutClearsAwaitingButDoesNotForceRepoll) {
    // Step 1: Send initial poll (fast_timer.interval_ms = 5000)
    fast_timer.on_poll_sent(1000);
    EXPECT_TRUE(fast_timer.awaiting_response);

    // Step 2: Blind doesn't respond, we're in response wait
    EXPECT_FALSE(fast_timer.should_poll(2000, false));
    EXPECT_FALSE(fast_timer.should_poll(2999, false));

    // Step 3: Timeout at 1000 + 2000 = 3000 — clears awaiting but does NOT
    // force immediate re-poll (that caused TX storms). Falls through to
    // interval check: (3000 - 1000) = 2000 < 5000 → false.
    EXPECT_FALSE(fast_timer.should_poll(3000, false));
    EXPECT_FALSE(fast_timer.awaiting_response);

    // Step 4: Normal interval eventually fires (1000 + 5000 = 6000)
    EXPECT_TRUE(fast_timer.should_poll(6000, false));
}

// =============================================================================
// 12. RF RECEIVED DURING RESPONSE WAIT STOPS THE CYCLE
// =============================================================================

TEST_F(PollTimerTest, RfResponseDuringWaitReturnToIntervalPolling) {
    // Step 1: Send command
    fast_timer.on_command_sent(1000);
    EXPECT_TRUE(fast_timer.awaiting_response);

    // Step 2: In response wait
    EXPECT_FALSE(fast_timer.should_poll(1500, false));

    // Step 3: RF response received at t=1800 (within wait period)
    fast_timer.on_rf_received(1800);
    EXPECT_FALSE(fast_timer.awaiting_response);

    // Step 4: Now back to interval-based polling from t=1800
    // Should not poll yet (interval_ms = 5000, last_poll = 1800)
    EXPECT_FALSE(fast_timer.should_poll(3000, false));
    EXPECT_FALSE(fast_timer.should_poll(6000, false));

    // Step 5: Should poll after interval from last rf received
    EXPECT_TRUE(fast_timer.should_poll(6801, false));
}

TEST_F(PollTimerTest, CommandThenRfThenIntervalCycle) {
    // Full cycle: command -> wait -> RF response -> interval -> poll -> wait -> RF
    fast_timer.on_command_sent(1000);
    EXPECT_FALSE(fast_timer.should_poll(1500, false));

    fast_timer.on_rf_received(1800);

    // Wait for interval (5000ms from t=1800)
    EXPECT_FALSE(fast_timer.should_poll(5000, false));
    EXPECT_TRUE(fast_timer.should_poll(6801, false));

    // Send poll
    fast_timer.on_poll_sent(6801);
    EXPECT_TRUE(fast_timer.awaiting_response);
    EXPECT_FALSE(fast_timer.should_poll(7000, false));

    // RF response
    fast_timer.on_rf_received(7200);
    EXPECT_FALSE(fast_timer.awaiting_response);

    // Back to interval from 7200
    EXPECT_FALSE(fast_timer.should_poll(10000, false));
    EXPECT_TRUE(fast_timer.should_poll(12201, false));
}

// =============================================================================
// EDGE CASES
// =============================================================================

TEST_F(PollTimerTest, DefaultTimerValues) {
    PollTimer t{};
    EXPECT_EQ(t.interval_ms, timing::DEFAULT_POLL_INTERVAL_MS);
    EXPECT_EQ(t.offset_ms, 0u);
    EXPECT_EQ(t.last_poll_ms, 0u);
    EXPECT_EQ(t.last_command_ms, 0u);
    EXPECT_FALSE(t.awaiting_response);
    EXPECT_FALSE(t.immediate_poll);
}

TEST_F(PollTimerTest, FreshTimerFirstPollAtTimeZero) {
    // last_poll_ms == 0, offset_ms == 0 => should poll at now >= 0
    PollTimer t{.interval_ms = 5000};
    EXPECT_TRUE(t.should_poll(0, false));
}

TEST_F(PollTimerTest, OnCommandSentDoesNotResetLastPollMs) {
    fast_timer.on_poll_sent(1000);
    fast_timer.on_rf_received(1500);
    EXPECT_EQ(fast_timer.last_poll_ms, 1500u);

    fast_timer.on_command_sent(3000);
    // last_poll_ms should still be 1500, not overwritten by command
    EXPECT_EQ(fast_timer.last_poll_ms, 1500u);
}
