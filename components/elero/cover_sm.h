/// @file cover_sm.h
/// @brief Cover state machine — variant-based FSM with derived position.
///
/// States carry exactly the data needed for that state.
/// Position is always DERIVED from (state, now, durations), never stored separately.
/// Invalid transitions don't compile — std::visit forces exhaustive handling.
///
/// States: Idle, Opening, Closing, Stopping
/// Stopping = post-stop cooldown (replaces the old idle_from_stop boolean)

#pragma once

#include <cstdint>
#include <variant>

namespace esphome::elero::cover_sm {

// ═══════════════════════════════════════════════════════════════════════════════
// POSITION CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════════

constexpr float POSITION_CLOSED = 0.0f;   ///< Fully closed (bottom)
constexpr float POSITION_OPEN = 1.0f;     ///< Fully open (top)
constexpr float NO_TARGET = -1.0f;        ///< No intermediate target position

// ═══════════════════════════════════════════════════════════════════════════════
// STATES — each carries exactly its own data
// ═══════════════════════════════════════════════════════════════════════════════

struct Idle {
    float position{POSITION_CLOSED};  ///< Known position (0.0 = closed, 1.0 = open)
};

struct Opening {
    float    start_position{POSITION_CLOSED};
    uint32_t start_ms{0};
};

struct Closing {
    float    start_position{POSITION_CLOSED};
    uint32_t start_ms{0};
};

/// Post-stop cooldown state. Transient RF "still moving" responses are
/// blocked for post_stop_cooldown_ms after an explicit STOP.
/// User commands override the cooldown.
struct Stopping {
    float    position{POSITION_CLOSED};  ///< Frozen at moment of stop
    uint32_t stop_ms{0};
};

using State = std::variant<Idle, Opening, Closing, Stopping>;

// ═══════════════════════════════════════════════════════════════════════════════
// CONTEXT — immutable device parameters, passed to transition functions
// ═══════════════════════════════════════════════════════════════════════════════

struct Context {
    uint32_t open_duration_ms{0};       ///< Time for full open (0 = no position tracking)
    uint32_t close_duration_ms{0};      ///< Time for full close (0 = no position tracking)
    uint32_t movement_timeout_ms{120000};     ///< Force idle after this long
    uint32_t post_stop_cooldown_ms{3000};     ///< Ignore transient RF after STOP
};

// ═══════════════════════════════════════════════════════════════════════════════
// DERIVED VALUES — pure functions, no mutation
// ═══════════════════════════════════════════════════════════════════════════════

/// Current cover position (0.0 = closed, 1.0 = open).
/// Computed from state + elapsed time. Never stored.
float position(const State &state, uint32_t now, const Context &ctx);

/// Whether the cover supports position tracking (both durations non-zero).
bool has_position_tracking(const Context &ctx);

/// Whether the cover is currently in motion.
bool is_moving(const State &state);

/// Whether the cover is in a stable state (Idle or Stopping).
bool is_idle(const State &state);

/// ESPHome-compatible operation enum.
enum class Operation : uint8_t { IDLE = 0, OPENING = 1, CLOSING = 2 };

/// Map current state to ESPHome operation enum.
Operation operation(const State &state);

// ═══════════════════════════════════════════════════════════════════════════════
// TRANSITIONS — pure functions returning new state
// ═══════════════════════════════════════════════════════════════════════════════

/// Process an RF status packet from the blind.
/// @param state Current state
/// @param state_byte The Elero state byte (packet::state::*)
/// @param now Current millis()
/// @param ctx Device parameters
/// @return New state (may be unchanged)
State on_rf_status(const State &state, uint8_t state_byte, uint32_t now, const Context &ctx);

/// Process a user command (from HA, web UI, MQTT, Matter, etc.).
/// @param state Current state
/// @param cmd_byte The Elero command byte (packet::command::*)
/// @param now Current millis()
/// @param ctx Device parameters
/// @return New state (may be unchanged)
State on_command(const State &state, uint8_t cmd_byte, uint32_t now, const Context &ctx);

/// Process a time tick — checks movement timeout and cooldown expiration.
/// Call this periodically from loop().
/// @return New state (may be unchanged)
State on_tick(const State &state, uint32_t now, const Context &ctx);

}  // namespace esphome::elero::cover_sm
