#pragma once

#include <string>

#include "../elero/elero_strings.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"

namespace esphome::elero {

inline void write_rf_command_fields(JsonObject root, uint8_t type, uint8_t key) {
  root["type"] = hex_str8(type);
  root["command"] = hex_str8(key);
  if (const char *name = elero_rf_command_name(type, key)) root["command_name"] = name;
}

// Device lists and backups can exceed ESPHome's build_json() serialization cap.
// Use its allocator, but serialize the complete document into the output string.
template<typename Build> std::string build_web_json(Build &&build) {
  JsonDocument doc(json::heap_json_allocator());
  build(doc.to<JsonObject>());
  if (doc.overflowed()) {
    ESP_LOGE("elero.web", "Web JSON document overflow");
    return "{}";
  }
  std::string output;
  output.reserve(measureJson(doc));
  serializeJson(doc, output);
  return output;
}

}  // namespace esphome::elero
