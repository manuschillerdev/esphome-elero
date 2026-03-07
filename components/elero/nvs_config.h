#pragma once

#include <cstdint>
#include <cstring>
#include "device_manager.h"

namespace esphome {
namespace elero {

/// NVS config version — bump when struct layout changes
constexpr uint8_t NVS_CONFIG_VERSION = 2;

/// Maximum name length (including null terminator)
constexpr size_t NVS_NAME_MAX = 24;

/// Fixed-size device configuration persisted via ESPHome preferences.
/// Each pre-allocated slot stores its own config independently.
/// Layout is stable — bump NVS_CONFIG_VERSION when changing fields.
struct NvsDeviceConfig {
  // Header (4 bytes)
  uint8_t version{NVS_CONFIG_VERSION};
  DeviceType type{DeviceType::COVER};
  uint8_t flags{0x01};  ///< bit 0 = enabled
  uint8_t reserved{0};

  // RF addressing (16 bytes)
  uint32_t dst_address{0};
  uint32_t src_address{0};
  uint8_t channel{0};
  uint8_t hop{0x0a};
  uint8_t payload_1{0x00};
  uint8_t payload_2{0x04};
  uint8_t type_byte{0x6a};
  uint8_t type2{0x00};
  uint8_t supports_tilt{0};  ///< Cover: 1 = tilt supported
  uint8_t rf_reserved{0};

  // Timing (16 bytes)
  uint32_t open_duration_ms{0};
  uint32_t close_duration_ms{0};
  uint32_t poll_interval_ms{300000};  ///< 5 minutes default
  uint32_t dim_duration_ms{0};        ///< Light: 0 = on/off only, >0 = brightness control

  // Name (24 bytes)
  char name[NVS_NAME_MAX]{};

  // ─── Helpers ───

  bool is_enabled() const { return flags & 0x01; }
  void set_enabled(bool en) { en ? (flags |= 0x01) : (flags &= ~0x01); }

  bool is_valid() const { return version == NVS_CONFIG_VERSION && dst_address != 0; }
  bool is_cover() const { return type == DeviceType::COVER; }
  bool is_light() const { return type == DeviceType::LIGHT; }
  bool is_remote() const { return type == DeviceType::REMOTE; }

  void set_name(const char *n) {
    if (n == nullptr) {
      name[0] = '\0';
      return;
    }
    strncpy(name, n, NVS_NAME_MAX - 1);
    name[NVS_NAME_MAX - 1] = '\0';
  }
};

static_assert(sizeof(NvsDeviceConfig) == 60, "NvsDeviceConfig must be 60 bytes for NVS storage");

}  // namespace elero
}  // namespace esphome
