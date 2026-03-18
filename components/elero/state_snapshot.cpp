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
        .last_seen_ms = dev.rf.last_seen_ms,
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
        .last_seen_ms = dev.rf.last_seen_ms,
    };
}

}  // namespace elero
}  // namespace esphome
