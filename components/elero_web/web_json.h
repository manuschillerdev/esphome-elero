#pragma once

#include <string>

#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"

namespace esphome::elero {

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
