#pragma once

#include "../elero/learn_in_manager.h"
#include <ArduinoJson.h>
#include <charconv>
#include <string_view>

namespace esphome::elero {

// Preserve the existing power-cycle request. Other operations use the hub's own remote.
template<typename SavedLink>
const char *parse_training_request(JsonObject root, uint32_t owned, SavedLink saved_link, LearnInStartRequest &request) {
  if (root["mode"].isNull()) return nullptr;
  const std::string_view mode = root["mode"] | "";
  if (mode == "power_cycle") return nullptr;
  if (mode != "train" && mode != "open_standby" && mode != "remove_link") return "Unknown learn-in mode";

  const std::string_view address = root["receiver_address"] | "";
  if (owned == 0 || request.src_addr != owned || !root["channel"].is<uint8_t>() || request.channel == 0 ||
      address.size() != 8 || !address.starts_with("0x")) return "Select the gateway remote, receiver and channel";
  const char *end = address.data() + address.size();
  auto parsed = std::from_chars(address.data() + 2, end, request.receiver, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != end || request.receiver == 0) return "Invalid receiver address";
  request.packets = packet::program::PACKETS;
  if (mode == "train") {
    request.mode = LearnInMode::TRAIN;
    return nullptr;
  }
  if (!saved_link(request.receiver, owned, request.channel)) return "Select a saved cover with this gateway channel";
  if (mode == "remove_link" && !(root["confirm_all_receivers"].is<bool>() && root["confirm_all_receivers"].as<bool>()))
    return "Confirm removal from all receivers that use this gateway remote and channel";
  request.mode = mode == "open_standby" ? LearnInMode::OPEN_STANDBY : LearnInMode::REMOVE_LINK;
  return nullptr;
}

}  // namespace esphome::elero
