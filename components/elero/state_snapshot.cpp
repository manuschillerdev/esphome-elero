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

    // Stopping = user just hit stop. Don't let stale last_state_raw (still
    // BOTTOM from before movement) show "closed" — blind is mid-travel.
    const char *ha_state = std::holds_alternative<cover_sm::Stopping>(cover.state)
        ? "open"
        : ha_cover_state_str(op, dev.rf.last_state_raw);

    auto *pt = problem_type_str(dev.rf.last_state_raw);

    return CoverStateSnapshot{
        .position = pos,
        .ha_state = ha_state,
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

RemoteStateSnapshot compute_remote_snapshot(const Device &dev) {
    const auto &remote = std::get<RemoteDevice>(dev.logic);
    return RemoteStateSnapshot{
        .last_command = remote.last_command,
        .last_target = remote.last_target,
        .last_channel = remote.last_channel,
        .rssi = dev.rf.last_rssi,
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

#ifdef ELERO_HAS_JSON
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
#endif  // ELERO_HAS_JSON

// ═══════════════════════════════════════════════════════════════════════════════
// DIFF FUNCTIONS — compare snapshot against Published cache
// ═══════════════════════════════════════════════════════════════════════════════

const char *state_change_str(uint16_t changes) {
    if (changes == state_change::ALL) return "ALL";

    static char buf[96];
    buf[0] = '\0';
    size_t pos = 0;

    auto append = [&](const char *name) {
        if (pos > 0 && pos < sizeof(buf) - 1) buf[pos++] = '|';
        size_t len = strlen(name);
        if (pos + len < sizeof(buf) - 1) {
            memcpy(buf + pos, name, len);
            pos += len;
        }
        buf[pos] = '\0';
    };

    if (changes & state_change::POSITION)       append("POS");
    if (changes & state_change::HA_STATE)        append("HA");
    if (changes & state_change::OPERATION)       append("OP");
    if (changes & state_change::TILT)            append("TILT");
    if (changes & state_change::PROBLEM)         append("PROB");
    if (changes & state_change::RSSI)            append("RSSI");
    if (changes & state_change::STATE_STRING)    append("STATE");
    if (changes & state_change::COMMAND_SOURCE)  append("CMD");
    if (changes & state_change::BRIGHTNESS)      append("BRI");
    if (changes & state_change::REMOTE_ACTIVITY) append("REMOTE");

    return buf;
}

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

uint16_t diff_and_update_remote(const RemoteStateSnapshot &snap, RemoteDevice::Published &pub) {
    // Remotes publish a single JSON blob (MQTT) covering all fields, so the bitmask
    // is only used as a "should publish?" gate. Without this dedup, every mesh-relayed
    // echo of our own TX fires a full publish — see ELERO_GROUP_INVESTIGATION.md §8.1.
    // RSSI is bucketed to whole dB so link-quality jitter doesn't churn publishes.
    uint16_t changes = 0;

    if (pub.last_command != snap.last_command) {
        changes |= state_change::REMOTE_ACTIVITY;
        pub.last_command = snap.last_command;
    }
    if (pub.last_target != snap.last_target) {
        changes |= state_change::REMOTE_ACTIVITY;
        pub.last_target = snap.last_target;
    }
    if (pub.last_channel != snap.last_channel) {
        changes |= state_change::REMOTE_ACTIVITY;
        pub.last_channel = snap.last_channel;
    }
    int rssi_int = static_cast<int>(round_rssi(snap.rssi));
    if (pub.rssi_rounded != rssi_int) {
        changes |= state_change::RSSI;
        pub.rssi_rounded = rssi_int;
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
