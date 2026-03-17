/// @file matter_constants.h
/// @brief Matter spec constants and Elero ↔ Matter conversion functions.
///
/// Named constants replace magic numbers. Conversion functions are inline
/// for unit testability without esp-matter SDK dependency.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "../elero/cover_sm.h"
#include "../elero/elero_packet.h"

namespace esphome {
namespace elero {
namespace matter {

// ═══════════════════════════════════════════════════════════════════════════════
// MATTER SPEC CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════════

// Cluster IDs (Matter Application Cluster Specification, stable across SDK versions)
inline constexpr uint32_t CLUSTER_WINDOW_COVERING = 0x0102;
inline constexpr uint32_t CLUSTER_ON_OFF = 0x0006;
inline constexpr uint32_t CLUSTER_LEVEL_CONTROL = 0x0008;

// Window Covering attribute IDs
inline constexpr uint32_t ATTR_WC_OPERATIONAL_STATUS = 0x000A;
inline constexpr uint32_t ATTR_WC_TARGET_POS_LIFT = 0x000B;
inline constexpr uint32_t ATTR_WC_CURRENT_POS_LIFT = 0x000E;

// OnOff attribute IDs
inline constexpr uint32_t ATTR_ONOFF = 0x0000;

// LevelControl attribute IDs
inline constexpr uint32_t ATTR_CURRENT_LEVEL = 0x0000;

// Percent100ths position range (Matter spec: 0 = fully open, 10000 = fully closed)
inline constexpr float POSITION_SCALE = 10000.0f;

// Level range (Matter spec: 0–254 for CurrentLevel; 255 is null/reserved)
inline constexpr uint8_t LEVEL_MAX = 254;
inline constexpr float LEVEL_SCALE = 254.0f;

// Position thresholds for fully open/closed detection
inline constexpr float POSITION_OPEN_THRESHOLD = 0.99f;
inline constexpr float POSITION_CLOSED_THRESHOLD = 0.01f;

// Epsilon for "already at target" detection
inline constexpr float POSITION_EPSILON = 0.01f;

// OperationalStatus bitmap field values (bits 0-1: global, 2-3: lift, 4-5: tilt)
inline constexpr uint8_t OP_STATUS_STOPPED = 0;
inline constexpr uint8_t OP_STATUS_OPENING = 1;
inline constexpr uint8_t OP_STATUS_CLOSING = 2;

// ═══════════════════════════════════════════════════════════════════════════════
// CONVERSION FUNCTIONS (clamped — safe for out-of-range inputs)
// ═══════════════════════════════════════════════════════════════════════════════

/// Elero position (0=closed, 1=open) → Matter Percent100ths (0=open, 10000=closed)
inline uint16_t elero_to_matter_position(float elero_pos) {
    float clamped = std::clamp(elero_pos, 0.0f, 1.0f);
    return static_cast<uint16_t>((1.0f - clamped) * POSITION_SCALE);
}

/// Matter Percent100ths (0=open, 10000=closed) → Elero position (0=closed, 1=open)
inline float matter_to_elero_position(uint16_t matter_pos) {
    uint16_t clamped = std::min(matter_pos, static_cast<uint16_t>(10000));
    return 1.0f - (static_cast<float>(clamped) / POSITION_SCALE);
}

/// Elero brightness (0.0–1.0) → Matter level (0–254)
inline uint8_t elero_to_matter_level(float brightness) {
    float clamped = std::clamp(brightness, 0.0f, 1.0f);
    return static_cast<uint8_t>(clamped * LEVEL_SCALE);
}

/// Matter level (0–254) → Elero brightness (0.0–1.0)
/// Level 255 is null/reserved in Matter — clamped to 254.
inline float matter_to_elero_brightness(uint8_t level) {
    uint8_t clamped = std::min(level, LEVEL_MAX);
    return static_cast<float>(clamped) / LEVEL_SCALE;
}

/// Cover operation → Matter OperationalStatus bitmap.
/// Sets both global (bits 0-1) and lift (bits 2-3) fields.
inline uint8_t operation_to_matter_status(cover_sm::Operation op) {
    uint8_t val = OP_STATUS_STOPPED;
    switch (op) {
        case cover_sm::Operation::OPENING:
            val = OP_STATUS_OPENING;
            break;
        case cover_sm::Operation::CLOSING:
            val = OP_STATUS_CLOSING;
            break;
        case cover_sm::Operation::IDLE:
            val = OP_STATUS_STOPPED;
            break;
    }
    return val | static_cast<uint8_t>(val << 2);
}

/// Determine RF command byte from a target Elero position and current position.
/// This is the core decision logic for Matter → Elero cover commands.
/// @param target  Target Elero position (0=closed, 1=open)
/// @param current Current Elero position (0=closed, 1=open)
/// @return UP, DOWN, or INVALID (already at target — caller should skip send)
inline uint8_t target_position_to_command(float target, float current) {
    if (target >= POSITION_OPEN_THRESHOLD) return packet::command::UP;
    if (target <= POSITION_CLOSED_THRESHOLD) return packet::command::DOWN;
    if (std::abs(target - current) < POSITION_EPSILON) return packet::command::INVALID;
    return (target > current) ? packet::command::UP : packet::command::DOWN;
}

}  // namespace matter
}  // namespace elero
}  // namespace esphome
