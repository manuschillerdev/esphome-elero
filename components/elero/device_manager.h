#pragma once

/// @file device_manager.h
/// @brief Abstract interface for device lifecycle management.
///
/// This interface abstracts the difference between:
/// - **Native mode**: Devices loaded from YAML at compile time, registered with ESPHome
/// - **MQTT mode**: Devices loaded from NVS at runtime, published via MQTT discovery
///
/// The Elero hub delegates device operations to an IDeviceManager implementation,
/// keeping RF protocol code completely separate from device management concerns.
///
/// ## Architecture
///
/// ```
///                    ┌─────────────────┐
///                    │   Elero Hub     │
///                    │   (RF only)     │
///                    └────────┬────────┘
///                             │
///                    set_device_manager()
///                             │
///              ┌──────────────┴──────────────┐
///              ▼                             ▼
///   ┌─────────────────────┐       ┌─────────────────────┐
///   │ NativeDeviceManager │       │  MqttDeviceManager  │
///   │  (no-op for CRUD)   │       │  (full NVS + MQTT)  │
///   └─────────────────────┘       └─────────────────────┘
/// ```
///
/// ## Usage
///
/// Native mode: Hub uses default NativeDeviceManager (no-op for add/remove)
/// MQTT mode: EleroMqttBridge injects MqttDeviceManager that handles NVS + MQTT

#include <cstdint>
#include <vector>
#include <functional>
#include "nvs_config.h"

namespace esphome::elero {

// Forward declarations
class EleroBlindBase;
class EleroDynamicCover;
class EleroLightBase;
class EleroDynamicLight;
struct RfPacketInfo;

/// Operating mode for the Elero system.
/// Determined at compile time based on config.
enum class EleroMode : uint8_t {
  NATIVE,  ///< YAML devices, ESPHome native API (no mqtt: section)
  MQTT,    ///< NVS devices, MQTT discovery (mqtt: section present)
};

/// Abstract interface for device lifecycle management.
///
/// This interface is designed for testability:
/// - All methods are virtual for mocking
/// - No direct dependencies on ESPHome globals
/// - Clear separation of concerns
class IDeviceManager {
 public:
  virtual ~IDeviceManager() = default;
  IDeviceManager() = default;
  IDeviceManager(const IDeviceManager&) = default;
  IDeviceManager& operator=(const IDeviceManager&) = default;
  IDeviceManager(IDeviceManager&&) = default;
  IDeviceManager& operator=(IDeviceManager&&) = default;

  // ─── Lifecycle ───

  /// Called during ESPHome setup phase.
  /// Native mode: No-op (devices already registered from YAML)
  /// MQTT mode: Load from NVS, activate slots, publish discoveries
  virtual void setup() = 0;

  /// Called every loop iteration.
  /// Native mode: No-op
  /// MQTT mode: Check MQTT connection, republish if needed
  virtual void loop() = 0;

  // ─── RF Packet Handling ───

  /// Called when an RF packet is received.
  /// Native mode: Routes to ESPHome entities (already handled by hub)
  /// MQTT mode: Publishes state to MQTT topics
  virtual void on_rf_packet(const RfPacketInfo& pkt) = 0;

  // ─── Device CRUD (MQTT mode only) ───

  /// Add a device and persist to storage.
  /// @param config Device configuration
  /// @return true if successful, false if not supported or failed
  [[nodiscard]] virtual bool add_device(const NvsDeviceConfig& config) = 0;

  /// Remove a device from storage and deactivate.
  /// @param address Device destination address
  /// @param type Device type
  /// @return true if successful, false if not supported or not found
  [[nodiscard]] virtual bool remove_device(uint32_t address, DeviceType type) = 0;

  /// Update an existing device configuration.
  /// @param config Updated configuration
  /// @return true if successful, false if not found or failed
  [[nodiscard]] virtual bool update_device(const NvsDeviceConfig& config) = 0;

  /// Enable or disable a device without removing it.
  /// @param address Device destination address
  /// @param type Device type
  /// @param enabled New enabled state
  /// @return true if successful, false if not found or failed
  [[nodiscard]] virtual bool set_device_enabled(uint32_t address, DeviceType type, bool enabled) = 0;

  // ─── Device Query ───

  /// Get all configured cover configs.
  [[nodiscard]] virtual std::vector<NvsDeviceConfig> get_cover_configs() const = 0;

  /// Get all configured light configs.
  [[nodiscard]] virtual std::vector<NvsDeviceConfig> get_light_configs() const = 0;

  /// Check if CRUD operations are supported.
  /// @return true for MQTT mode, false for native mode
  [[nodiscard]] virtual bool supports_crud() const = 0;

  /// Get the current operating mode.
  [[nodiscard]] virtual EleroMode mode() const = 0;
};

/// Native mode device manager - no-op implementation.
///
/// In native mode, devices are:
/// - Defined in YAML at compile time
/// - Registered directly with the Elero hub via register_cover()/register_light()
/// - Exposed via ESPHome's native API
///
/// This manager does nothing because all device management is handled by
/// ESPHome's standard component lifecycle.
class NativeDeviceManager : public IDeviceManager {
 public:
  void setup() override {}
  void loop() override {}
  void on_rf_packet(const RfPacketInfo& /*pkt*/) override {}

  [[nodiscard]] bool add_device(const NvsDeviceConfig& /*config*/) override { return false; }
  [[nodiscard]] bool remove_device(uint32_t /*address*/, DeviceType /*type*/) override { return false; }
  [[nodiscard]] bool update_device(const NvsDeviceConfig& /*config*/) override { return false; }
  [[nodiscard]] bool set_device_enabled(uint32_t /*address*/, DeviceType /*type*/, bool /*enabled*/) override {
    return false;
  }

  [[nodiscard]] std::vector<NvsDeviceConfig> get_cover_configs() const override { return {}; }
  [[nodiscard]] std::vector<NvsDeviceConfig> get_light_configs() const override { return {}; }

  [[nodiscard]] bool supports_crud() const override { return false; }
  [[nodiscard]] EleroMode mode() const override { return EleroMode::NATIVE; }
};

}  // namespace esphome::elero
