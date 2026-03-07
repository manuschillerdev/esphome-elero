#pragma once

#include <cstdint>
#include <functional>

namespace esphome {
namespace elero {

struct RfPacketInfo;      // Forward declaration
struct NvsDeviceConfig;   // Forward declaration

/// Callback for notifying external observers (e.g., web server) of CRUD events.
using CrudEventCallback = std::function<void(const char *event, const char *json)>;

/// CRUD event type strings (used in CrudEventCallback and WebSocket protocol).
namespace crud_event {
inline constexpr const char *DEVICE_UPSERTED = "device_upserted";
inline constexpr const char *DEVICE_REMOVED = "device_removed";
}  // namespace crud_event

/// Default name format for auto-discovered remotes.
inline constexpr const char *DEFAULT_REMOTE_NAME_FMT = "Remote 0x%06x";

/// Device type stored in NVS
enum class DeviceType : uint8_t {
  COVER = 0,
  LIGHT = 1,
  REMOTE = 2,
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

/// Operating mode of the hub
enum class HubMode : uint8_t {
  NATIVE = 0,      ///< YAML-defined devices, ESPHome native API
  MQTT = 1,        ///< NVS-stored devices, MQTT discovery
  NATIVE_NVS = 2,  ///< NVS-stored devices, ESPHome native API (reboot to apply)
};

/// String representation of HubMode (for JSON, web API).
inline const char *hub_mode_str(HubMode m) {
  switch (m) {
    case HubMode::MQTT: return "mqtt";
    case HubMode::NATIVE_NVS: return "native_nvs";
    default: return "native";
  }
}

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
  [[nodiscard]] virtual bool upsert_device(const NvsDeviceConfig &config) { return false; }
  [[nodiscard]] virtual bool remove_device(DeviceType type, uint32_t dst_address) { return false; }

  /// Register a callback for CRUD event notifications (e.g., for WS broadcast)
  virtual void set_crud_callback(CrudEventCallback cb) { (void)cb; }
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

}  // namespace elero
}  // namespace esphome
