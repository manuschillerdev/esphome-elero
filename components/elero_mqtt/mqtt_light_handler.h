#pragma once

#include "mqtt_context.h"
#include "../elero/EleroDynamicLight.h"
#include <functional>
#include <vector>

namespace esphome {
namespace elero {
namespace mqtt_light {

using LightFinder = std::function<EleroDynamicLight *(uint32_t)>;

void publish_discovery(const MqttContext &ctx, EleroDynamicLight *light);
void remove_discovery(const MqttContext &ctx, uint32_t addr);
void publish_state(const MqttContext &ctx, EleroDynamicLight *light);
void subscribe_commands(const MqttContext &ctx, uint32_t addr, LightFinder finder);

/// Convenience: publish_discovery + subscribe_commands + publish_state.
void activate(const MqttContext &ctx, EleroDynamicLight *light, LightFinder finder);

/// Returns all discovery config topics this handler publishes for the given address.
void discovery_topics(const MqttContext &ctx, uint32_t addr, std::vector<std::string> &out);

}  // namespace mqtt_light
}  // namespace elero
}  // namespace esphome
