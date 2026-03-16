/// @file light_sm.h
/// @brief Light state machine — variant-based FSM with derived brightness.
///
/// States carry exactly the data needed for that state.
/// Brightness is always DERIVED from (state, now, dim_duration), never stored separately.
///
/// States: Off, On, DimmingUp, DimmingDown

#pragma once

#include <cstdint>
#include <variant>

namespace esphome::elero::light_sm {

// ═══════════════════════════════════════════════════════════════════════════════
// STATES
// ═══════════════════════════════════════════════════════════════════════════════

struct Off {};

struct On {
    float brightness{1.0f};  ///< Static brightness (0.0–1.0)
};

struct DimmingUp {
    float    start_brightness{0.0f};
    float    target_brightness{1.0f};
    uint32_t start_ms{0};
};

struct DimmingDown {
    float    start_brightness{1.0f};
    float    target_brightness{0.0f};
    uint32_t start_ms{0};
};

using State = std::variant<Off, On, DimmingUp, DimmingDown>;

// ═══════════════════════════════════════════════════════════════════════════════
// CONTEXT
// ═══════════════════════════════════════════════════════════════════════════════

struct Context {
    uint32_t dim_duration_ms{0};  ///< Time for 0%→100% dim (0 = on/off only, no brightness)
};

// ═══════════════════════════════════════════════════════════════════════════════
// DERIVED VALUES
// ═══════════════════════════════════════════════════════════════════════════════

/// Current brightness (0.0–1.0). Derived from state + elapsed time.
float brightness(const State &state, uint32_t now, const Context &ctx);

/// Whether the light is on (any state except Off).
bool is_on(const State &state);

/// Whether the light is actively dimming.
bool is_dimming(const State &state);

/// Whether the light supports brightness control (dim_duration > 0).
bool supports_brightness(const Context &ctx);

/// Tolerance for brightness comparisons.
constexpr float BRIGHTNESS_EPSILON = 0.01f;

// ═══════════════════════════════════════════════════════════════════════════════
// TRANSITIONS
// ═══════════════════════════════════════════════════════════════════════════════

/// Process an RF status packet from the light receiver.
State on_rf_status(const State &state, uint8_t state_byte, uint32_t now, const Context &ctx);

/// Set target brightness. Starts dimming if brightness control is enabled.
/// @param target Target brightness (0.0–1.0). 0.0 = turn off.
State on_set_brightness(const State &state, float target, uint32_t now, const Context &ctx);

/// Turn on at full brightness (or last brightness).
State on_turn_on(const State &state, uint32_t now, const Context &ctx);

/// Turn off.
State on_turn_off(const State &state);

/// Time tick — check if dimming has reached target and stop.
State on_tick(const State &state, uint32_t now, const Context &ctx);

}  // namespace esphome::elero::light_sm
