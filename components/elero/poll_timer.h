/// @file poll_timer.h
/// @brief Simple poll timing logic for cover status queries.
///
/// Not a state machine — just timing. Handles:
/// - Regular poll interval (configurable, default 5 min)
/// - Fast polling while moving (2s)
/// - Response wait window (suppress polls briefly after sending a command)
/// - Immediate poll request (after remote command detected)
/// - Staggered offset (prevent all covers from polling simultaneously)

#pragma once

#include <cstdint>
#include "elero_packet.h"

namespace esphome::elero {

struct PollTimer {
    uint32_t interval_ms{packet::timing::DEFAULT_POLL_INTERVAL_MS};
    uint32_t offset_ms{0};           ///< Stagger offset
    uint32_t last_poll_ms{0};
    uint32_t last_command_ms{0};     ///< When we last sent a command (for response wait)
    bool     immediate_poll{false};  ///< Poll ASAP on next check

    /// Check if it's time to poll.
    /// @param now Current millis()
    /// @param is_moving Whether the cover is currently moving (uses fast interval)
    bool should_poll(uint32_t now, bool is_moving) const {
        // Don't poll if we recently sent a command (wait for response)
        if (last_command_ms > 0 &&
            (now - last_command_ms) < packet::timing::RESPONSE_WAIT_MS) {
            return false;
        }

        // Immediate poll overrides interval
        if (immediate_poll) {
            return true;
        }

        // Choose interval based on movement state
        uint32_t effective_interval = is_moving
            ? packet::timing::POLL_INTERVAL_MOVING
            : interval_ms;

        // Never poll if interval is max uint32 ("never")
        if (effective_interval == UINT32_MAX) {
            return false;
        }

        // First poll uses offset for staggering
        if (last_poll_ms == 0) {
            return now >= offset_ms;
        }

        return (now - last_poll_ms) >= effective_interval;
    }

    /// Mark that a poll was just sent.
    void on_poll_sent(uint32_t now) {
        last_poll_ms = now;
        immediate_poll = false;
    }

    /// Mark that a command was just sent (suppresses polls briefly).
    void on_command_sent(uint32_t now) {
        last_command_ms = now;
    }

    /// Mark that an RF response was received (resets poll timer).
    void on_rf_received(uint32_t now) {
        last_poll_ms = now;
        immediate_poll = false;
    }

    /// Request an immediate poll on next check.
    void request_immediate_poll() {
        immediate_poll = true;
    }
};

}  // namespace esphome::elero
