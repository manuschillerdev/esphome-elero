/// @file device_type.h
/// @brief Shared device type, hub mode enums, and their string representations.

#pragma once

#include <cstdint>
#include <cstdio>

namespace esphome::elero {

enum class DeviceType : uint8_t {
    COVER = 0,
    LIGHT = 1,
    REMOTE = 2,
};

enum class HubMode : uint8_t {
    NATIVE = 0,
    MQTT = 1,
    NATIVE_NVS = 2,
};

/// String representation of DeviceType (for JSON, logs, web API).
inline const char *device_type_str(DeviceType t) {
    switch (t) {
        case DeviceType::COVER: return "cover";
        case DeviceType::LIGHT: return "light";
        case DeviceType::REMOTE: return "remote";
        default: return "unknown";
    }
}

/// String representation of HubMode (for JSON, web API).
inline const char *hub_mode_str(HubMode m) {
    switch (m) {
        case HubMode::MQTT: return "mqtt";
        case HubMode::NATIVE_NVS: return "native_nvs";
        default: return "native";
    }
}

/// Default name format for auto-discovered remotes.
inline constexpr const char *DEFAULT_REMOTE_NAME_FMT = "Remote 0x%06x";

}  // namespace esphome::elero
