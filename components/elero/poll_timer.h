/// @file poll_timer.h
/// @brief Simple poll timing logic for cover status queries.
///
/// Matches the old CoverCore::should_poll / mark_polled behavior:
/// - Boolean `awaiting_response` cleared on RF response, not by timeout
/// - Timeout clears awaiting flag, falls through to normal interval check
/// - Staggered offset for first poll per cover
/// - Fast polling while moving (2s)

#pragma once

#include <cstdint>
#include "elero_packet.h"

namespace esphome::elero {

struct PollTimer {
    uint32_t interval_ms{packet::timing::DEFAULT_POLL_INTERVAL_MS};
    uint32_t offset_ms{0};              ///< Stagger offset for first poll
    uint32_t last_poll_ms{0};
    uint32_t last_command_ms{0};        ///< When we last sent a command/CHECK
    bool     awaiting_response{false};  ///< Waiting for blind to respond

    /// Check if it's time to poll.
    /// Mirrors old CoverCore::should_poll() exactly.
    bool should_poll(uint32_t now, bool is_moving) {
        // Response-wait: defer polling after command TX to keep radio in RX
        if (awaiting_response) {
            if ((now - last_command_ms) < packet::timing::RESPONSE_WAIT_MS) {
                return false;  // stay in RX, don't poll yet
            }
            // Timeout expired — blind didn't respond. Clear flag and fall
            // through to normal interval check (don't force immediate re-poll,
            // that causes TX storms when multiple covers time out together).
            awaiting_response = false;
        }

        // Choose interval based on movement state
        uint32_t effective_interval = is_moving
            ? packet::timing::POLL_INTERVAL_MOVING
            : interval_ms;

        // Guard: 0 means misconfigured (e.g. stale NVS) — don't poll
        if (effective_interval == 0) {
            return false;
        }

        // First poll uses offset for staggering
        if (last_poll_ms == 0) {
            return now >= offset_ms;
        }

        return (now - last_poll_ms) >= effective_interval;
    }

    /// Mark that a poll (CHECK) was just sent.
    /// Sets awaiting_response so we stay in RX until blind responds or timeout.
    void on_poll_sent(uint32_t now) {
        last_poll_ms = now;
        last_command_ms = now;
        awaiting_response = true;
    }

    /// Mark that a user command was just sent (suppresses polls briefly).
    void on_command_sent(uint32_t now) {
        last_command_ms = now;
        awaiting_response = true;
    }

    /// Mark that an RF response was received.
    /// Clears awaiting_response so we fall back to interval-based polling.
    void on_rf_received(uint32_t now) {
        last_poll_ms = now;
        awaiting_response = false;
    }
};

}  // namespace esphome::elero
