#pragma once

#include <cstdint>

namespace esphome::elero {

struct RfPacketInfo;  // Forward declaration
struct NvsDeviceConfig;  // Forward declaration

/// Device type stored in NVS
enum class DeviceType : uint8_t {
  COVER = 0,
  LIGHT = 1,
  REMOTE = 2,
};

/// Operating mode of the hub
enum class HubMode : uint8_t {
  NATIVE = 0,  ///< YAML-defined devices, ESPHome native API
  MQTT = 1,    ///< NVS-stored devices, MQTT discovery
};

/// Abstract device manager interface.
/// The hub delegates device lifecycle and RF packet routing to this.
/// Native mode uses a no-op implementation; MQTT mode uses MqttDeviceManager.
class IDeviceManager {
 public:
  virtual ~IDeviceManager() = default;

  virtual void setup() = 0;
  virtual void loop() = 0;

  /// Called by the hub for every decoded RF packet
  virtual void on_rf_packet(const RfPacketInfo &pkt) = 0;

  /// Current operating mode
  virtual HubMode mode() const = 0;

  /// Whether this manager supports runtime add/remove/update
  virtual bool supports_crud() const = 0;

  /// Device CRUD (only meaningful when supports_crud() is true)
  virtual bool add_device(const NvsDeviceConfig &config) { return false; }
  virtual bool remove_device(DeviceType type, uint32_t dst_address) { return false; }
};

/// No-op device manager for native (YAML) mode.
/// Devices are registered at compile time via ESPHome codegen.
class NativeDeviceManager : public IDeviceManager {
 public:
  void setup() override {}
  void loop() override {}
  void on_rf_packet(const RfPacketInfo &) override {}
  HubMode mode() const override { return HubMode::NATIVE; }
  bool supports_crud() const override { return false; }
};

}  // namespace esphome::elero
