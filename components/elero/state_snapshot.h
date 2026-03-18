/// @file state_snapshot.h
/// @brief State projection layer — single source of truth for what HA sees.
///
/// Snapshots are ephemeral: computed on demand from (Device, now), never persisted.
/// All output adapters (native, MQTT, WebSocket, Matter) consume these structs
/// instead of independently deriving state. This eliminates inconsistencies.

#pragma once

#include "device.h"
#include "cover_sm.h"
#include "light_sm.h"
#include "elero_strings.h"
#include "device_type.h"
#include "esphome/components/json/json_util.h"

namespace esphome {
namespace elero {

// ═══════════════════════════════════════════════════════════════════════════════
// COVER SNAPSHOT
// ═══════════════════════════════════════════════════════════════════════════════

struct CoverStateSnapshot {
    float position;              ///< 0.0–1.0
    const char *ha_state;        ///< "open"/"closed"/"opening"/"closing"/"stopped"
    cover_sm::Operation operation;  ///< IDLE/OPENING/CLOSING
    bool tilted;
    bool is_problem;
    const char *problem_type;    ///< "blocking"/"overheated"/"timeout"/PROBLEM_TYPE_NONE
    float rssi;
    const char *state_string;    ///< Raw elero state name ("top", "moving_up", etc.)
    const char *command_source;  ///< "hub"/"remote"/"unknown"
    uint32_t last_seen_ms;
    const char *device_class;    ///< "shutter"/"blind"/"awning"/etc.

    /// Write snapshot fields to a JSON object. Caller adds identity/config fields.
    void to_json(JsonObject obj) const;
};

// ═══════════════════════════════════════════════════════════════════════════════
// LIGHT SNAPSHOT
// ═══════════════════════════════════════════════════════════════════════════════

struct LightStateSnapshot {
    bool is_on;
    float brightness;            ///< 0.0–1.0
    bool is_problem;
    const char *problem_type;
    float rssi;
    const char *state_string;
    const char *command_source;
    uint32_t last_seen_ms;

    /// Write snapshot fields to a JSON object. Caller adds identity/config fields.
    void to_json(JsonObject obj) const;
};

// ═══════════════════════════════════════════════════════════════════════════════
// SHARED HELPERS
// ═══════════════════════════════════════════════════════════════════════════════

/// String returned when no problem is active.
inline constexpr const char *PROBLEM_TYPE_NONE = "none";

/// Check if an RF state byte represents a problem condition.
inline constexpr bool is_problem_state(uint8_t raw_state) {
    return raw_state == packet::state::BLOCKING ||
           raw_state == packet::state::OVERHEATED ||
           raw_state == packet::state::TIMEOUT;
}

/// Get problem type string from RF state byte. Returns nullptr if not a problem.
inline constexpr const char *problem_type_str(uint8_t raw_state) {
    switch (raw_state) {
        case packet::state::BLOCKING: return "blocking";
        case packet::state::OVERHEATED: return "overheated";
        case packet::state::TIMEOUT: return "timeout";
    }
    return nullptr;
}

/// Map cover operation + position to HA state string.
/// HA expects: "open", "closed", "opening", "closing", "stopped"
inline constexpr const char *ha_cover_state_str(cover_sm::Operation op, float position) {
    switch (op) {
        case cover_sm::Operation::OPENING: return "opening";
        case cover_sm::Operation::CLOSING: return "closing";
        case cover_sm::Operation::IDLE:
            if (position >= cover_sm::POSITION_OPEN) return "open";
            if (position <= cover_sm::POSITION_CLOSED) return "closed";
            return "stopped";
    }
    return "stopped";
}

// ═══════════════════════════════════════════════════════════════════════════════
// COMPUTE FUNCTIONS — pure, no ESPHome dependencies
// ═══════════════════════════════════════════════════════════════════════════════

/// Compute a cover state snapshot from a Device. Single source of truth.
CoverStateSnapshot compute_cover_snapshot(const Device &dev, uint32_t now);

/// Compute a light state snapshot from a Device. Single source of truth.
LightStateSnapshot compute_light_snapshot(const Device &dev, uint32_t now);

}  // namespace elero
}  // namespace esphome
