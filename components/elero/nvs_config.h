#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "device_type.h"
#include "elero_packet.h"

namespace esphome {
namespace elero {

/// NVS config version — bump when struct layout changes (v3: added updated_at)
constexpr uint8_t NVS_CONFIG_VERSION = 3;

/// NVS group config version — separate compound objects, not device slots.
constexpr uint8_t NVS_GROUP_CONFIG_VERSION = 1;

/// Maximum name length (including null terminator)
constexpr size_t NVS_NAME_MAX = 24;

/// Maximum group id length (including null terminator)
constexpr size_t NVS_GROUP_ID_MAX = 24;

/// Maximum group members. Device ids are stable hardware destination addresses.
constexpr size_t NVS_GROUP_MAX_MEMBERS = 48;

/// NVS preference hash keys — must be unique per manager type to avoid collisions.
/// MQTT mode uses "elero_cover", NVS mode uses "elero_nvs_cover", etc.
namespace nvs_pref_key {
inline constexpr const char *COVER = "elero_cover";
inline constexpr const char *LIGHT = "elero_light";
inline constexpr const char *REMOTE = "elero_remote";
inline constexpr const char *NVS_COVER = "elero_nvs_cover";
inline constexpr const char *NVS_LIGHT = "elero_nvs_light";
inline constexpr const char *NVS_REMOTE = "elero_nvs_remote";
inline constexpr const char *GROUP = "elero_group";
}  // namespace nvs_pref_key

/// Fixed-size device configuration persisted via ESPHome preferences.
/// Each pre-allocated slot stores its own config independently.
/// Layout is stable — bump NVS_CONFIG_VERSION when changing fields.
struct NvsDeviceConfig {
  // Header (4 bytes)
  uint8_t version{NVS_CONFIG_VERSION};
  DeviceType type{DeviceType::COVER};
  static constexpr uint8_t FLAG_ENABLED = 0x01;
  uint8_t flags{FLAG_ENABLED};  ///< bit 0 = enabled
  uint8_t ha_device_class{0};  ///< HaCoverClass enum value (0 = shutter, backward compatible)

  // RF addressing (16 bytes)
  uint32_t dst_address{0};
  uint32_t src_address{0};
  uint8_t channel{0};
  uint8_t hop{packet::defaults::HOP};
  uint8_t payload_1{packet::defaults::PAYLOAD_1};
  uint8_t payload_2{packet::defaults::PAYLOAD_2};
  uint8_t type_byte{packet::msg_type::COMMAND};
  uint8_t type2{packet::defaults::TYPE2};
  uint8_t supports_tilt{0};  ///< Cover: 1 = tilt supported
  uint8_t rf_reserved{0};

  // Timing (16 bytes)
  uint32_t open_duration_ms{0};
  uint32_t close_duration_ms{0};
  uint32_t poll_interval_ms_reserved{0};  ///< DEPRECATED: kept for NVS struct layout compat, ignored at runtime
  uint32_t dim_duration_ms{0};        ///< Light: 0 = on/off only, >0 = brightness control

  // Metadata (4 bytes)
  uint32_t updated_at{0};  ///< millis() when last persisted (0 = never)

  // Name (24 bytes)
  char name[NVS_NAME_MAX]{};

  // ─── Helpers ───

  bool is_enabled() const { return flags & FLAG_ENABLED; }
  void set_enabled(bool en) { en ? (flags |= FLAG_ENABLED) : (flags &= ~FLAG_ENABLED); }

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

static_assert(sizeof(NvsDeviceConfig) == 64, "NvsDeviceConfig must be 64 bytes for NVS storage");

/// Fixed-size group configuration persisted via ESPHome preferences.
/// Groups are pure membership metadata: id + display name + stable device ids.
/// Device type is intentionally not stored; it is derived from referenced devices
/// and guarded during upsert/command validation.
struct NvsGroupConfig {
  uint8_t version{NVS_GROUP_CONFIG_VERSION};
  uint8_t member_count{0};
  uint16_t reserved{0};

  char id[NVS_GROUP_ID_MAX]{};
  char name[NVS_NAME_MAX]{};
  uint32_t device_ids[NVS_GROUP_MAX_MEMBERS]{};

  bool is_valid() const {
    return version == NVS_GROUP_CONFIG_VERSION && id[0] != '\0' &&
           member_count > 0 && member_count <= NVS_GROUP_MAX_MEMBERS;
  }

  void set_id(const char *value) {
    if (value == nullptr) {
      id[0] = '\0';
      return;
    }
    strncpy(id, value, NVS_GROUP_ID_MAX - 1);
    id[NVS_GROUP_ID_MAX - 1] = '\0';
  }

  void set_name(const char *value) {
    if (value == nullptr) {
      name[0] = '\0';
      return;
    }
    strncpy(name, value, NVS_NAME_MAX - 1);
    name[NVS_NAME_MAX - 1] = '\0';
  }
};

static_assert(sizeof(NvsGroupConfig) == 244, "NvsGroupConfig layout changed unexpectedly");

}  // namespace elero
}  // namespace esphome
