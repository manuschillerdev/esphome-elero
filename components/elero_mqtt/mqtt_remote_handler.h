#pragma once

#include "mqtt_context.h"
#include "../elero/EleroRemoteControl.h"
#include <vector>

namespace esphome {
namespace elero {
namespace mqtt_remote {

void publish_discovery(const MqttContext &ctx, EleroRemoteControl *remote);
void remove_discovery(const MqttContext &ctx, uint32_t addr);
void publish_state(const MqttContext &ctx, EleroRemoteControl *remote);

/// Returns all discovery config topics this handler publishes for the given address.
void discovery_topics(const MqttContext &ctx, uint32_t addr, std::vector<std::string> &out);

}  // namespace mqtt_remote
}  // namespace elero
}  // namespace esphome
