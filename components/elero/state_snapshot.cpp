/// @file state_snapshot.cpp
/// @brief State snapshot compute functions — pure, no ESPHome dependencies.

#include "state_snapshot.h"
#include "overloaded.h"

namespace esphome {
namespace elero {

CoverStateSnapshot compute_cover_snapshot(const Device &dev, uint32_t now) {
    const auto &cover = std::get<CoverDevice>(dev.logic);
    auto ctx = cover_context(dev.config);

    float pos = cover_sm::position(cover.state, now, ctx);
    auto op = cover_sm::operation(cover.state);

    auto *pt = problem_type_str(dev.rf.last_state_raw);

    return CoverStateSnapshot{
        .position = pos,
        .ha_state = ha_cover_state_str(op, pos),
        .operation = op,
        .tilted = cover.tilted,
        .is_problem = is_problem_state(dev.rf.last_state_raw),
        .problem_type = pt != nullptr ? pt : PROBLEM_TYPE_NONE,
        .rssi = dev.rf.last_rssi,
        .state_string = elero_state_to_string(dev.rf.last_state_raw),
        .command_source = command_source_str(cover.last_command_source),
        .device_class = ha_cover_class_str(static_cast<HaCoverClass>(dev.config.ha_device_class)),
    };
}

LightStateSnapshot compute_light_snapshot(const Device &dev, uint32_t now) {
    const auto &light = std::get<LightDevice>(dev.logic);
    auto ctx = light_context(dev.config);

    auto *lpt = problem_type_str(dev.rf.last_state_raw);

    return LightStateSnapshot{
        .is_on = light_sm::is_on(light.state),
        .brightness = light_sm::brightness(light.state, now, ctx),
        .is_problem = is_problem_state(dev.rf.last_state_raw),
        .problem_type = lpt != nullptr ? lpt : PROBLEM_TYPE_NONE,
        .rssi = dev.rf.last_rssi,
        .state_string = elero_state_to_string(dev.rf.last_state_raw),
        .command_source = command_source_str(light.last_command_source),
    };
}

// ═══════════════════════════════════════════════════════════════════════════════
// JSON SERIALIZATION — single mapping from snapshot fields to JSON keys
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef USE_JSON
void CoverStateSnapshot::to_json(JsonObject obj) const {
    obj["position"] = position;
    obj["ha_state"] = ha_state;
    obj["tilted"] = tilted;
    obj["is_problem"] = is_problem;
    obj["problem_type"] = problem_type;
    obj["rssi"] = round_rssi(rssi);
    obj["state"] = state_string;
    obj["command_source"] = command_source;
    obj["device_class"] = device_class;
}

void LightStateSnapshot::to_json(JsonObject obj) const {
    obj["is_on"] = is_on;
    obj["brightness"] = brightness;
    obj["is_problem"] = is_problem;
    obj["problem_type"] = problem_type;
    obj["rssi"] = round_rssi(rssi);
    obj["state"] = state_string;
    obj["command_source"] = command_source;
}
#endif  // USE_JSON

// ═══════════════════════════════════════════════════════════════════════════════
// DIFF FUNCTIONS — compare snapshot against Published cache
// ═══════════════════════════════════════════════════════════════════════════════

uint16_t diff_and_update_cover(const CoverStateSnapshot &snap, CoverDevice::Published &pub) {
    // Pointer comparison is intentional — all state/problem/command strings are
    // compile-time string literals returned by pure functions (pointer identity = value identity).
    uint16_t changes = 0;

    int pos_pct = static_cast<int>(snap.position * PERCENT_SCALE);
    if (pub.position_pct != pos_pct) {
        changes |= state_change::POSITION;
        pub.position_pct = pos_pct;
    }
    if (pub.ha_state != snap.ha_state) {
        changes |= state_change::HA_STATE;
        pub.ha_state = snap.ha_state;
    }
    if (pub.operation != snap.operation) {
        changes |= state_change::OPERATION;
        pub.operation = snap.operation;
    }
    if (pub.state_string != snap.state_string) {
        changes |= state_change::STATE_STRING;
        pub.state_string = snap.state_string;
    }
    if (pub.tilted != snap.tilted) {
        changes |= state_change::TILT;
        pub.tilted = snap.tilted;
    }
    if (pub.is_problem != snap.is_problem || pub.problem_type != snap.problem_type) {
        changes |= state_change::PROBLEM;
        pub.is_problem = snap.is_problem;
        pub.problem_type = snap.problem_type;
    }
    int rssi_int = static_cast<int>(round_rssi(snap.rssi));
    if (pub.rssi_rounded != rssi_int) {
        changes |= state_change::RSSI;
        pub.rssi_rounded = rssi_int;
    }
    if (pub.command_source != snap.command_source) {
        changes |= state_change::COMMAND_SOURCE;
        pub.command_source = snap.command_source;
    }

    return changes;
}

uint16_t diff_and_update_light(const LightStateSnapshot &snap, LightDevice::Published &pub) {
    // Pointer comparison — see comment in diff_and_update_cover.
    uint16_t changes = 0;

    int brightness_pct = static_cast<int>(snap.brightness * PERCENT_SCALE);
    if (pub.is_on != snap.is_on || pub.brightness_pct != brightness_pct) {
        changes |= state_change::BRIGHTNESS;
        pub.is_on = snap.is_on;
        pub.brightness_pct = brightness_pct;
    }
    if (pub.state_string != snap.state_string) {
        changes |= state_change::STATE_STRING;
        pub.state_string = snap.state_string;
    }
    if (pub.is_problem != snap.is_problem || pub.problem_type != snap.problem_type) {
        changes |= state_change::PROBLEM;
        pub.is_problem = snap.is_problem;
        pub.problem_type = snap.problem_type;
    }
    int rssi_int = static_cast<int>(round_rssi(snap.rssi));
    if (pub.rssi_rounded != rssi_int) {
        changes |= state_change::RSSI;
        pub.rssi_rounded = rssi_int;
    }
    if (pub.command_source != snap.command_source) {
        changes |= state_change::COMMAND_SOURCE;
        pub.command_source = snap.command_source;
    }

    return changes;
}

}  // namespace elero
}  // namespace esphome
