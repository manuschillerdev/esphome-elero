#pragma once

/// @file nvs_config.h
/// @brief NVS storage for dynamic Elero device configuration.
///
/// Provides persistent storage for dynamically discovered covers and lights,
/// enabling runtime add/remove without reflashing. Configuration survives
/// reboots via ESP32 Non-Volatile Storage (NVS).
///
/// Storage layout:
/// - Namespace: "elero"
/// - Key "covers": blob array of NvsDeviceConfig (up to MAX_DYNAMIC_COVERS)
/// - Key "lights": blob array of NvsDeviceConfig (up to MAX_DYNAMIC_LIGHTS)
/// - Key "num_covers": uint8_t count
/// - Key "num_lights": uint8_t count

#include <cstdint>
#include <cstring>
#include <array>
#include <vector>
#include <functional>
#include "elero_packet.h"

namespace esphome::elero {

// ═══════════════════════════════════════════════════════════════════════════════
// LIMITS
// ═══════════════════════════════════════════════════════════════════════════════

/// Maximum number of dynamically configured covers
constexpr uint8_t MAX_DYNAMIC_COVERS = 32;

/// Maximum number of dynamically configured lights
constexpr uint8_t MAX_DYNAMIC_LIGHTS = 32;

/// Maximum device name length (including null terminator)
constexpr uint8_t MAX_DEVICE_NAME_LEN = 24;

/// NVS namespace for Elero configuration
constexpr const char* NVS_NAMESPACE = "elero";

// ═══════════════════════════════════════════════════════════════════════════════
// DEVICE TYPE
// ═══════════════════════════════════════════════════════════════════════════════

/// Device type discriminator for NVS storage
enum class DeviceType : uint8_t {
  COVER = 0x01,  ///< Blind/roller cover
  LIGHT = 0x02,  ///< Wireless light
};

// ═══════════════════════════════════════════════════════════════════════════════
// NVS DEVICE CONFIG (56 bytes)
// ═══════════════════════════════════════════════════════════════════════════════

/// Persistent configuration for a dynamic cover or light.
///
/// This struct is stored directly in NVS as a blob. The layout is carefully
/// designed for:
/// - Fixed size (56 bytes) for array storage without metadata
/// - Natural alignment to avoid padding surprises
/// - Version field for future schema migration
///
/// @note All multi-byte integers are stored in native byte order (little-endian
///       on ESP32). NVS handles endianness correctly for the platform.
struct NvsDeviceConfig {
  // ─── Header (4 bytes) ───
  uint8_t version{1};           ///< Config version (for future migrations)
  DeviceType type{DeviceType::COVER};  ///< Device type
  uint8_t flags{0};             ///< Bit flags: [0]=enabled, [1-7]=reserved
  uint8_t reserved_0{0};        ///< Alignment padding

  // ─── RF Addressing (16 bytes) ───
  uint32_t dst_address{0};      ///< Destination address (blind/light)
  uint32_t src_address{0};      ///< Source address (emulated remote)
  uint8_t channel{0};           ///< RF channel
  uint8_t hop{packet::defaults::HOP};  ///< Hop count
  uint8_t payload_1{packet::defaults::PAYLOAD_1};  ///< Payload byte 1
  uint8_t payload_2{packet::defaults::PAYLOAD_2};  ///< Payload byte 2
  uint8_t type_byte{packet::msg_type::COMMAND};    ///< Message type
  uint8_t type2{packet::defaults::TYPE2};          ///< Secondary type
  std::array<uint8_t, 2> reserved_1{0, 0};  ///< Alignment padding

  // ─── Timing (12 bytes) ───
  uint32_t open_duration_ms{0};   ///< Time to fully open (for position tracking)
  uint32_t close_duration_ms{0};  ///< Time to fully close
  uint32_t poll_interval_ms{300000};  ///< Status poll interval (default 5 min)

  // ─── Name (24 bytes) ───
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays) - C-string interop required
  char name[MAX_DEVICE_NAME_LEN]{0};  ///< Human-readable name (null-terminated)

  // ─── Total: 4 + 16 + 12 + 24 = 56 bytes ───

  /// Check if this config is enabled
  [[nodiscard]] bool is_enabled() const { return (flags & 0x01) != 0; }

  /// Set the enabled flag
  void set_enabled(bool enabled) {
    if (enabled) {
      flags |= 0x01;
    } else {
      flags &= ~0x01;
    }
  }

  /// Check if config is valid (has required fields)
  [[nodiscard]] bool is_valid() const {
    return dst_address != 0 && src_address != 0 && channel != 0;
  }

  /// Check if this is a cover type
  [[nodiscard]] bool is_cover() const { return type == DeviceType::COVER; }

  /// Check if this is a light type
  [[nodiscard]] bool is_light() const { return type == DeviceType::LIGHT; }

  /// Set name safely (with null termination)
  void set_name(const char* new_name) {
    if (new_name == nullptr) {
      name[0] = '\0';
      return;
    }
    std::strncpy(name, new_name, MAX_DEVICE_NAME_LEN - 1);
    name[MAX_DEVICE_NAME_LEN - 1] = '\0';
  }

  /// Initialize as a cover with default values
  static NvsDeviceConfig create_cover(uint32_t dst, uint32_t src, uint8_t ch, const char* n = nullptr) {
    NvsDeviceConfig cfg;
    cfg.type = DeviceType::COVER;
    cfg.dst_address = dst;
    cfg.src_address = src;
    cfg.channel = ch;
    cfg.set_enabled(true);
    if (n != nullptr) {
      cfg.set_name(n);
    }
    return cfg;
  }

  /// Initialize as a light with default values
  static NvsDeviceConfig create_light(uint32_t dst, uint32_t src, uint8_t ch, const char* n = nullptr) {
    NvsDeviceConfig cfg;
    cfg.type = DeviceType::LIGHT;
    cfg.dst_address = dst;
    cfg.src_address = src;
    cfg.channel = ch;
    cfg.set_enabled(true);
    if (n != nullptr) {
      cfg.set_name(n);
    }
    return cfg;
  }
};

// Compile-time size check
static_assert(sizeof(NvsDeviceConfig) == 56, "NvsDeviceConfig must be exactly 56 bytes");

// ═══════════════════════════════════════════════════════════════════════════════
// NVS STORAGE INTERFACE
// ═══════════════════════════════════════════════════════════════════════════════

/// Result of NVS operations
enum class NvsResult : uint8_t {
  OK = 0,
  NOT_FOUND,       ///< Key not found (not an error for optional data)
  INVALID_DATA,    ///< Data corruption or version mismatch
  STORAGE_FULL,    ///< NVS partition full
  WRITE_ERROR,     ///< Failed to write
  READ_ERROR,      ///< Failed to read
  NOT_INITIALIZED, ///< NVS not initialized
};

/// Abstract interface for NVS operations (enables testing with mock)
class NvsStorage {
 public:
  virtual ~NvsStorage() = default;
  NvsStorage() = default;
  NvsStorage(const NvsStorage&) = default;
  NvsStorage& operator=(const NvsStorage&) = default;
  NvsStorage(NvsStorage&&) = default;
  NvsStorage& operator=(NvsStorage&&) = default;

  /// Load all cover configs from NVS
  /// @param out Vector to populate with configs
  /// @return Result code
  [[nodiscard]] virtual NvsResult load_covers(std::vector<NvsDeviceConfig>& out) = 0;

  /// Load all light configs from NVS
  /// @param out Vector to populate with configs
  /// @return Result code
  [[nodiscard]] virtual NvsResult load_lights(std::vector<NvsDeviceConfig>& out) = 0;

  /// Save all cover configs to NVS
  /// @param configs Configs to save
  /// @return Result code
  [[nodiscard]] virtual NvsResult save_covers(const std::vector<NvsDeviceConfig>& configs) = 0;

  /// Save all light configs to NVS
  /// @param configs Configs to save
  /// @return Result code
  [[nodiscard]] virtual NvsResult save_lights(const std::vector<NvsDeviceConfig>& configs) = 0;

  /// Add or update a single device config
  /// @param config Config to save
  /// @return Result code
  [[nodiscard]] virtual NvsResult save_device(const NvsDeviceConfig& config) = 0;

  /// Remove a device by address
  /// @param address Device destination address
  /// @param type Device type
  /// @return Result code
  [[nodiscard]] virtual NvsResult remove_device(uint32_t address, DeviceType type) = 0;

  /// Erase all stored configurations
  [[nodiscard]] virtual NvsResult erase_all() = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ESP32 NVS IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════════

/// ESP32 NVS implementation of NvsStorage interface
class EspNvsStorage : public NvsStorage {
 public:
  EspNvsStorage() = default;

  /// Initialize NVS (call once at startup)
  /// @return true if successful
  [[nodiscard]] bool init();

  [[nodiscard]] NvsResult load_covers(std::vector<NvsDeviceConfig>& out) override;
  [[nodiscard]] NvsResult load_lights(std::vector<NvsDeviceConfig>& out) override;
  [[nodiscard]] NvsResult save_covers(const std::vector<NvsDeviceConfig>& configs) override;
  [[nodiscard]] NvsResult save_lights(const std::vector<NvsDeviceConfig>& configs) override;
  [[nodiscard]] NvsResult save_device(const NvsDeviceConfig& config) override;
  [[nodiscard]] NvsResult remove_device(uint32_t address, DeviceType type) override;
  [[nodiscard]] NvsResult erase_all() override;

 private:
  bool initialized_{false};

  /// Load devices of a given type
  [[nodiscard]] NvsResult load_devices(const char* key, const char* count_key,
                                       std::vector<NvsDeviceConfig>& out);

  /// Save devices of a given type
  [[nodiscard]] NvsResult save_devices(const char* key, const char* count_key,
                                       const std::vector<NvsDeviceConfig>& configs, uint8_t max_count);
};

// ═══════════════════════════════════════════════════════════════════════════════
// UTILITY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════════

/// Convert NvsResult to human-readable string
const char* nvs_result_to_string(NvsResult result);

}  // namespace esphome::elero
