#pragma once

#include "mqtt_context.h"
#include "../elero/EleroDynamicCover.h"
#include <functional>
#include <vector>

namespace esphome {
namespace elero {
namespace mqtt_cover {

using CoverFinder = std::function<EleroDynamicCover *(uint32_t)>;

void publish_discovery(const MqttContext &ctx, EleroDynamicCover *cover);
void remove_discovery(const MqttContext &ctx, uint32_t addr);
void publish_state(const MqttContext &ctx, EleroDynamicCover *cover);
void subscribe_commands(const MqttContext &ctx, uint32_t addr, CoverFinder finder);

/// Returns all discovery config topics this handler publishes for the given address.
void discovery_topics(const MqttContext &ctx, uint32_t addr, std::vector<std::string> &out);

/// Convenience: publish_discovery + subscribe_commands + publish_state.
void activate(const MqttContext &ctx, EleroDynamicCover *cover, CoverFinder finder);

}  // namespace mqtt_cover
}  // namespace elero
}  // namespace esphome
