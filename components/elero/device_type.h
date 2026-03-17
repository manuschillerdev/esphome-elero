/// @file device_type.h
/// @brief Shared device type, hub mode enums, and their string representations.

#pragma once

#include <cstdint>

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
    MATTER = 3,
};

/// String representation of DeviceType (for JSON, logs, web API).
inline const char *device_type_str(DeviceType t) {
    switch (t) {
        case DeviceType::COVER: return "cover";
        case DeviceType::LIGHT: return "light";
        case DeviceType::REMOTE: return "remote";
    }
    return "unknown";  // Unreachable — satisfies -Wreturn-type without default
}

/// String representation of HubMode (for JSON, web API).
inline const char *hub_mode_str(HubMode m) {
    switch (m) {
        case HubMode::NATIVE: return "native";
        case HubMode::MQTT: return "mqtt";
        case HubMode::NATIVE_NVS: return "native_nvs";
        case HubMode::MATTER: return "matter";
    }
    return "native";  // Unreachable — satisfies -Wreturn-type without default
}

/// Default name format for auto-discovered remotes.
inline constexpr const char *DEFAULT_REMOTE_NAME_FMT = "Remote 0x%06x";

}  // namespace esphome::elero
