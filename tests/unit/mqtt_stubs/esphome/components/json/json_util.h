#pragma once

#include <ArduinoJson.h>
#include <string>

namespace esphome::json {

template<typename F> std::string build_json(F &&builder) {
    JsonDocument doc;
    builder(doc.to<JsonObject>());
    std::string output;
    serializeJson(doc, output);
    return output;
}

template<typename F> bool parse_json(const std::string &payload, F &&parser) {
    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;
    return parser(doc.as<JsonObject>());
}

}  // namespace esphome::json
