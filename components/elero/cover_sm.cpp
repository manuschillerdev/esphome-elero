/// @file cover_sm.cpp
/// @brief Cover state machine transition implementations.

#include "cover_sm.h"
#include "overloaded.h"
#include "elero_packet.h"
#include <algorithm>

namespace esphome::elero::cover_sm {

// ─── Position helpers ───────────────────────────────────────────────────────

static float position_during_opening(float start, uint32_t start_ms,
                                     uint32_t now, uint32_t dur) {
    if (dur == 0) return start;
    float elapsed = static_cast<float>(now - start_ms) / static_cast<float>(dur);
    return std::min(1.0f, start + elapsed);
}

static float position_during_closing(float start, uint32_t start_ms,
                                     uint32_t now, uint32_t dur) {
    if (dur == 0) return start;
    float elapsed = static_cast<float>(now - start_ms) / static_cast<float>(dur);
    return std::max(0.0f, start - elapsed);
}

// ─── Derived values ─────────────────────────────────────────────────────────

float position(const State &state, uint32_t now, const Context &ctx) {
    return std::visit(overloaded{
        [](const Idle &s) -> float { return s.position; },
        [](const Stopping &s) -> float { return s.position; },
        [&](const Opening &s) -> float {
            return position_during_opening(s.start_position, s.start_ms,
                                           now, ctx.open_duration_ms);
        },
        [&](const Closing &s) -> float {
            return position_during_closing(s.start_position, s.start_ms,
                                           now, ctx.close_duration_ms);
        },
    }, state);
}

bool has_position_tracking(const Context &ctx) {
    return ctx.open_duration_ms > 0 && ctx.close_duration_ms > 0;
}

bool is_moving(const State &state) {
    return std::holds_alternative<Opening>(state) ||
           std::holds_alternative<Closing>(state);
}

bool is_idle(const State &state) {
    return std::holds_alternative<Idle>(state) ||
           std::holds_alternative<Stopping>(state);
}

Operation operation(const State &state) {
    return std::visit(overloaded{
        [](const Idle &) { return Operation::IDLE; },
        [](const Stopping &) { return Operation::IDLE; },
        [](const Opening &) { return Operation::OPENING; },
        [](const Closing &) { return Operation::CLOSING; },
    }, state);
}

// ─── Helpers for transitions ────────────────────────────────────────────────

/// Classify an RF state byte as indicating upward movement.
static bool is_rf_moving_up(uint8_t s) {
    return s == packet::state::MOVING_UP || s == packet::state::START_MOVING_UP;
}

/// Classify an RF state byte as indicating downward movement.
static bool is_rf_moving_down(uint8_t s) {
    return s == packet::state::MOVING_DOWN || s == packet::state::START_MOVING_DOWN;
}

/// Classify an RF state byte as indicating movement stopped (any reason).
static bool is_rf_stopped(uint8_t s) {
    return s == packet::state::STOPPED ||
           s == packet::state::INTERMEDIATE ||
           s == packet::state::BLOCKING ||
           s == packet::state::OVERHEATED ||
           s == packet::state::TIMEOUT;
}

// ─── RF Status transitions ─────────────────────────────────────────────────

State on_rf_status(const State &state, uint8_t state_byte, uint32_t now,
                   const Context &ctx) {
    const uint8_t s = state_byte;

    return std::visit(overloaded{
        // ── From Idle ──────────────────────────────────────────────
        [&](const Idle &idle) -> State {
            if (s == packet::state::TOP) return Idle{1.0f};
            if (s == packet::state::BOTTOM) return Idle{0.0f};
            if (is_rf_moving_up(s)) return Opening{idle.position, now};
            if (is_rf_moving_down(s)) return Closing{idle.position, now};
            // TILT, INTERMEDIATE, warnings — stay idle
            return state;
        },

        // ── From Opening ───────────────────────────────────────────
        [&](const Opening &opening) -> State {
            if (s == packet::state::TOP) return Idle{1.0f};
            if (s == packet::state::BOTTOM) return Idle{0.0f};
            if (is_rf_stopped(s)) {
                float pos = position_during_opening(
                    opening.start_position, opening.start_ms,
                    now, ctx.open_duration_ms);
                return Stopping{pos, now};
            }
            // Direction change from blind itself
            if (is_rf_moving_down(s)) {
                float pos = position_during_opening(
                    opening.start_position, opening.start_ms,
                    now, ctx.open_duration_ms);
                return Closing{pos, now};
            }
            // Still opening — no change
            return state;
        },

        // ── From Closing ───────────────────────────────────────────
        [&](const Closing &closing) -> State {
            if (s == packet::state::BOTTOM) return Idle{0.0f};
            if (s == packet::state::TOP) return Idle{1.0f};
            if (is_rf_stopped(s)) {
                float pos = position_during_closing(
                    closing.start_position, closing.start_ms,
                    now, ctx.close_duration_ms);
                return Stopping{pos, now};
            }
            // Direction change from blind itself
            if (is_rf_moving_up(s)) {
                float pos = position_during_closing(
                    closing.start_position, closing.start_ms,
                    now, ctx.close_duration_ms);
                return Opening{pos, now};
            }
            // Still closing — no change
            return state;
        },

        // ── From Stopping (cooldown) ───────────────────────────────
        [&](const Stopping &) -> State {
            // During cooldown, only definitive endpoint states are respected.
            // Transient MOVING_UP/DOWN are ignored (they're echoes from before STOP).
            if (s == packet::state::TOP) return Idle{1.0f};
            if (s == packet::state::BOTTOM) return Idle{0.0f};
            return state;
        },
    }, state);
}

// ─── User Command transitions ──────────────────────────────────────────────

State on_command(const State &state, uint8_t cmd_byte, uint32_t now,
                 const Context &ctx) {
    const uint8_t cmd = cmd_byte;
    float pos = position(state, now, ctx);

    return std::visit(overloaded{
        [&](const Idle &) -> State {
            if (cmd == packet::command::UP || cmd == packet::command::TILT)
                return Opening{pos, now};
            if (cmd == packet::command::DOWN)
                return Closing{pos, now};
            return state;  // CHECK, STOP while idle — no change
        },

        [&](const Opening &) -> State {
            if (cmd == packet::command::STOP)
                return Stopping{pos, now};
            if (cmd == packet::command::DOWN)
                return Closing{pos, now};  // Reverse
            return state;
        },

        [&](const Closing &) -> State {
            if (cmd == packet::command::STOP)
                return Stopping{pos, now};
            if (cmd == packet::command::UP || cmd == packet::command::TILT)
                return Opening{pos, now};  // Reverse
            return state;
        },

        [&](const Stopping &) -> State {
            // User commands override cooldown
            if (cmd == packet::command::UP || cmd == packet::command::TILT)
                return Opening{pos, now};
            if (cmd == packet::command::DOWN)
                return Closing{pos, now};
            return state;
        },
    }, state);
}

// ─── Tick transitions (timeouts) ────────────────────────────────────────────

State on_tick(const State &state, uint32_t now, const Context &ctx) {
    return std::visit(overloaded{
        [&](const Idle &) -> State { return state; },

        [&](const Opening &s) -> State {
            if ((now - s.start_ms) >= ctx.movement_timeout_ms) {
                float pos = position_during_opening(
                    s.start_position, s.start_ms, now, ctx.open_duration_ms);
                return Idle{pos};
            }
            return state;
        },

        [&](const Closing &s) -> State {
            if ((now - s.start_ms) >= ctx.movement_timeout_ms) {
                float pos = position_during_closing(
                    s.start_position, s.start_ms, now, ctx.close_duration_ms);
                return Idle{pos};
            }
            return state;
        },

        [&](const Stopping &s) -> State {
            if ((now - s.stop_ms) >= ctx.post_stop_cooldown_ms) {
                return Idle{s.position};
            }
            return state;
        },
    }, state);
}

}  // namespace esphome::elero::cover_sm
