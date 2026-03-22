/// @file device_type.h
/// @brief Shared device type, hub mode enums, and their string representations.

#pragma once

#include <cstdint>

namespace esphome {
namespace elero {

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
inline constexpr const char *device_type_str(DeviceType t) {
    switch (t) {
        case DeviceType::COVER: return "cover";
        case DeviceType::LIGHT: return "light";
        case DeviceType::REMOTE: return "remote";
    }
    return "unknown";  // Unreachable — satisfies -Wreturn-type without default
}

/// String representation of HubMode (for JSON, web API).
inline constexpr const char *hub_mode_str(HubMode m) {
    switch (m) {
        case HubMode::NATIVE: return "native";
        case HubMode::MQTT: return "mqtt";
        case HubMode::NATIVE_NVS: return "native_nvs";
    }
    return "native";  // Unreachable — satisfies -Wreturn-type without default
}

/// HA device_class for covers (maps to NvsDeviceConfig.ha_device_class byte).
/// Default 0 = "shutter" is backward compatible with existing NVS data.
enum class HaCoverClass : uint8_t {
    SHUTTER = 0,
    BLIND = 1,
    AWNING = 2,
    CURTAIN = 3,
    SHADE = 4,
    GARAGE = 5,
};

/// String representation of HaCoverClass (for HA discovery and entity config).
inline constexpr const char *ha_cover_class_str(HaCoverClass v) {
    switch (v) {
        case HaCoverClass::SHUTTER: return "shutter";
        case HaCoverClass::BLIND: return "blind";
        case HaCoverClass::AWNING: return "awning";
        case HaCoverClass::CURTAIN: return "curtain";
        case HaCoverClass::SHADE: return "shade";
        case HaCoverClass::GARAGE: return "garage";
    }
    return "shutter";
}

/// Default name format for auto-discovered remotes.
inline constexpr const char *DEFAULT_REMOTE_NAME_FMT = "Remote 0x%06x";

}  // namespace elero
}  // namespace esphome
