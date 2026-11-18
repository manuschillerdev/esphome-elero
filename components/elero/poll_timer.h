/// @file poll_timer.h
/// @brief Schedule cover status queries and allow time for a radio response.

#pragma once

#include <cstdint>
#include <optional>
#include "elero_packet.h"

namespace esphome::elero {

class PollTimer {
 public:
    uint32_t interval_ms{packet::timing::DEFAULT_POLL_INTERVAL_MS};
    uint32_t offset_ms{0};  ///< Stagger the first poll after boot.

    bool should_poll(uint32_t now, bool moving) {
        if (response_wait_start_) {
            if (now - *response_wait_start_ < packet::timing::RESPONSE_WAIT_MS) return false;
            response_wait_start_.reset();
        }

        const auto interval = moving ? packet::timing::POLL_INTERVAL_MOVING : interval_ms;
        if (interval == 0) return false;
        if (!last_poll_ms_) return now >= offset_ms;
        return now - *last_poll_ms_ >= interval;
    }

    void on_poll_queued(uint32_t now) {
        last_poll_ms_ = now;
        on_command_queued(now);
    }

    void on_command_queued(uint32_t now) { response_wait_start_ = now; }

    void on_rf_received(uint32_t now) {
        last_poll_ms_ = now;
        response_wait_start_.reset();
    }

 private:
    std::optional<uint32_t> last_poll_ms_;  ///< Most recent poll or radio response.
    std::optional<uint32_t> response_wait_start_;
};

}  // namespace esphome::elero
