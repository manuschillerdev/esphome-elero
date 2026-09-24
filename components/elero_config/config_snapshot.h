#pragma once
#include "esphome/core/defines.h"
#ifdef USE_JSON
#include "esphome/components/elero/device_registry.h"
#include "esphome/components/json/json_util.h"
namespace esphome::elero::config_snapshot {
inline constexpr uint8_t VERSION = 2;
bool parse_device_config(JsonObject root, NvsDeviceConfig &config, std::string &error);
bool parse_group_config(JsonObject root, NvsGroupConfig &config, std::string &error);
void build_device(const NvsDeviceConfig &config, JsonObject out);
std::string export_json(DeviceRegistry &registry, const char *version);
// Callers validate the snapshot version; preserves web import's merge/upsert semantics.
std::string import_json(DeviceRegistry &registry, JsonObject snapshot);
}  // namespace esphome::elero::config_snapshot
#endif
