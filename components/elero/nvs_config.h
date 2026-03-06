#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include "device_manager.h"

namespace esphome::elero {

/// NVS config version — bump when struct layout changes
constexpr uint8_t NVS_CONFIG_VERSION = 1;

/// Maximum name length (including null terminator)
constexpr size_t NVS_NAME_MAX = 24;

/// Fixed-size device configuration stored in NVS.
/// Layout is stable for binary serialization.
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
  uint8_t rf_reserved[2]{0, 0};

  // Timing (12 bytes)
  uint32_t open_duration_ms{0};
  uint32_t close_duration_ms{0};
  uint32_t poll_interval_ms{300000};  ///< 5 minutes default

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
    strncpy(name, n, NVS_NAME_MAX - 1);
    name[NVS_NAME_MAX - 1] = '\0';
  }
};

static_assert(sizeof(NvsDeviceConfig) == 56, "NvsDeviceConfig must be 56 bytes for NVS storage");

/// Abstract NVS storage interface (testable without ESP32 hardware)
class NvsStorage {
 public:
  virtual ~NvsStorage() = default;

  virtual bool load_devices(DeviceType type, std::vector<NvsDeviceConfig> &out) = 0;
  virtual bool save_devices(DeviceType type, const std::vector<NvsDeviceConfig> &configs) = 0;
  virtual bool save_device(const NvsDeviceConfig &config) = 0;
  virtual bool remove_device(DeviceType type, uint32_t dst_address) = 0;
  virtual bool erase_all() = 0;
};

/// ESP32 NVS implementation
class EspNvsStorage : public NvsStorage {
 public:
  bool load_devices(DeviceType type, std::vector<NvsDeviceConfig> &out) override;
  bool save_devices(DeviceType type, const std::vector<NvsDeviceConfig> &configs) override;
  bool save_device(const NvsDeviceConfig &config) override;
  bool remove_device(DeviceType type, uint32_t dst_address) override;
  bool erase_all() override;

 private:
  static const char *type_key(DeviceType type);
  static const char *count_key(DeviceType type);
};

}  // namespace esphome::elero
