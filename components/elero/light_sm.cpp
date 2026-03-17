/// @file light_sm.cpp
/// @brief Light state machine transition implementations.

#include "light_sm.h"
#include "overloaded.h"
#include "elero_packet.h"
#include <algorithm>
#include <cmath>

namespace esphome::elero::light_sm {

// ─── Derived values ─────────────────────────────────────────────────────────

float brightness(const State &state, uint32_t now, const Context &ctx) {
    return std::visit(overloaded{
        [](const Off &) -> float { return 0.0f; },
        [](const On &s) -> float { return s.brightness; },
        [&](const DimmingUp &s) -> float {
            if (ctx.dim_duration_ms == 0) return s.target_brightness;
            float elapsed = static_cast<float>(now - s.start_ms) /
                            static_cast<float>(ctx.dim_duration_ms);
            return std::min(s.target_brightness, s.start_brightness + elapsed);
        },
        [&](const DimmingDown &s) -> float {
            if (ctx.dim_duration_ms == 0) return s.target_brightness;
            float elapsed = static_cast<float>(now - s.start_ms) /
                            static_cast<float>(ctx.dim_duration_ms);
            return std::max(s.target_brightness, s.start_brightness - elapsed);
        },
    }, state);
}

bool is_on(const State &state) {
    return !std::holds_alternative<Off>(state);
}

bool is_dimming(const State &state) {
    return std::holds_alternative<DimmingUp>(state) ||
           std::holds_alternative<DimmingDown>(state);
}

bool supports_brightness(const Context &ctx) {
    return ctx.dim_duration_ms > 0;
}

// ─── RF Status transitions ─────────────────────────────────────────────────

State on_rf_status(const State &state, uint8_t state_byte, uint32_t now,
                   const Context &ctx) {
    // Definitive on/off from RF
    if (state_byte == packet::state::LIGHT_ON ||
        state_byte == packet::state::TOP) {
        return On{1.0f};
    }
    if (state_byte == packet::state::LIGHT_OFF ||
        state_byte == packet::state::BOTTOM) {
        return Off{};
    }
    // Everything else — no change
    return state;
}

// ─── Set Brightness ─────────────────────────────────────────────────────────

State on_set_brightness(const State &state, float target, uint32_t now,
                        const Context &ctx) {
    // Clamp target
    target = std::clamp(target, 0.0f, 1.0f);

    // Turn off
    if (target < BRIGHTNESS_EPSILON) {
        return Off{};
    }

    // No brightness control — instant on/off
    if (ctx.dim_duration_ms == 0) {
        return On{target};
    }

    // Get current brightness
    float current = brightness(state, now, ctx);

    // Already at target
    if (std::fabs(target - current) < BRIGHTNESS_EPSILON) {
        return On{target};
    }

    // Start dimming in the right direction
    if (target > current) {
        return DimmingUp{current, target, now};
    } else {
        return DimmingDown{current, target, now};
    }
}

// ─── Turn On / Off ──────────────────────────────────────────────────────────

State on_turn_on(const State & /*state*/, uint32_t /*now*/, const Context & /*ctx*/) {
    return On{1.0f};
}

State on_turn_off(const State & /*state*/) {
    return Off{};
}

// ─── Tick (dimming completion check) ────────────────────────────────────────

State on_tick(const State &state, uint32_t now, const Context &ctx) {
    return std::visit(overloaded{
        [&](const Off &) -> State { return state; },
        [&](const On &) -> State { return state; },

        [&](const DimmingUp &s) -> State {
            float current = brightness(state, now, ctx);
            if (current >= s.target_brightness - BRIGHTNESS_EPSILON) {
                return On{s.target_brightness};
            }
            return state;
        },

        [&](const DimmingDown &s) -> State {
            float current = brightness(state, now, ctx);
            if (current <= s.target_brightness + BRIGHTNESS_EPSILON) {
                if (s.target_brightness < BRIGHTNESS_EPSILON) {
                    return Off{};
                }
                return On{s.target_brightness};
            }
            return state;
        },
    }, state);
}

}  // namespace esphome::elero::light_sm
