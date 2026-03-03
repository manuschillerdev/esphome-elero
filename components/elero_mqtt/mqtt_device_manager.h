#pragma once

/// @file mqtt_device_manager.h
/// @brief MQTT mode device manager - handles NVS loading and MQTT publishing.
///
/// This implementation of IDeviceManager is used when mqtt: is configured.
/// It takes full ownership of device lifecycle:
/// - Loads devices from NVS on startup
/// - Activates dynamic cover/light slots
/// - Publishes MQTT discovery payloads
/// - Handles state publishing on RF packets
/// - Supports runtime add/remove/update via CRUD methods
///
/// ## Separation of Concerns
///
/// - **MqttDeviceManager**: Device lifecycle (load, save, activate, deactivate)
/// - **MqttPublisher**: MQTT transport abstraction (publish, subscribe)
/// - **NvsStorage**: Persistent storage abstraction
/// - **Elero hub**: RF protocol only

#include "../elero/device_manager.h"
#include "../elero/nvs_config.h"
#include "mqtt_publisher.h"
#include "esphome/core/component.h"
#include <memory>
#include <vector>
#include <map>

namespace esphome::elero {

// Forward declarations
class Elero;
class EleroDynamicCover;
class EleroDynamicLight;

/// MQTT mode device manager implementation.
///
/// This is an ESPHome Component that manages dynamic Elero devices via MQTT.
/// It implements IDeviceManager for integration with the Elero hub.
///
/// ## Responsibilities
/// - Load device configs from NVS on setup
/// - Activate pre-allocated dynamic cover/light slots
/// - Publish MQTT discovery payloads for Home Assistant
/// - Publish state updates when RF packets arrive
/// - Handle CRUD operations for runtime device management
///
/// ## ESPHome Lifecycle
/// - setup(): Initialize NVS, load devices, subscribe to commands
/// - loop(): Check MQTT connection, republish discoveries on reconnect
///
/// ## Slot Management
///
/// Dynamic covers and lights are pre-allocated at compile time (via YAML count
/// configuration). This manager activates slots as devices are loaded from NVS
/// or added via the web UI.
///
/// ## MQTT Discovery
///
/// Follows Home Assistant MQTT Discovery protocol:
/// - `homeassistant/cover/elero/<addr>/config` - Cover discovery
/// - `homeassistant/light/elero/<addr>/config` - Light discovery
/// - `elero/<addr>/state` - State updates
/// - `elero/<addr>/cmd` - Command subscription
class MqttDeviceManager : public Component, public IDeviceManager {
 public:
  /// Default constructor - use setters to configure.
  MqttDeviceManager() = default;

  // Prevent copying, allow moving
  MqttDeviceManager(const MqttDeviceManager&) = delete;
  MqttDeviceManager& operator=(const MqttDeviceManager&) = delete;
  MqttDeviceManager(MqttDeviceManager&&) = default;
  MqttDeviceManager& operator=(MqttDeviceManager&&) = default;
  ~MqttDeviceManager() override = default;

  // ─── ESPHome Component Interface ───

  /// Setup priority - after network and MQTT are ready.
  [[nodiscard]] float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  /// Dump configuration for ESPHome logs.
  void dump_config() override;

  // ─── Setters for Python Codegen ───

  /// Set the Elero hub (required).
  void set_hub(Elero* hub) { hub_ = hub; }

  /// Set the MQTT topic prefix (default: "elero").
  void set_topic_prefix(const std::string& prefix) { topic_prefix_ = prefix; }

  /// Set the discovery prefix (default: "homeassistant").
  void set_discovery_prefix(const std::string& prefix) { discovery_prefix_ = prefix; }

  /// Set the device name for MQTT discovery (default: "Elero Gateway").
  void set_device_name(const std::string& name) { device_name_ = name; }

  /// Set dynamic cover slots (pre-allocated array from Python codegen).
  void set_cover_slots(EleroDynamicCover* slots, size_t count) {
    cover_slots_ = slots;
    cover_slot_count_ = count;
  }

  /// Set dynamic light slots (pre-allocated array from Python codegen).
  void set_light_slots(EleroDynamicLight* slots, size_t count) {
    light_slots_ = slots;
    light_slot_count_ = count;
  }

  // ─── IDeviceManager interface ───
  void setup() override;
  void loop() override;
  void on_rf_packet(const RfPacketInfo& pkt) override;

  [[nodiscard]] bool add_device(const NvsDeviceConfig& config) override;
  [[nodiscard]] bool remove_device(uint32_t address, DeviceType type) override;
  [[nodiscard]] bool update_device(const NvsDeviceConfig& config) override;
  [[nodiscard]] bool set_device_enabled(uint32_t address, DeviceType type, bool enabled) override;

  [[nodiscard]] std::vector<NvsDeviceConfig> get_cover_configs() const override;
  [[nodiscard]] std::vector<NvsDeviceConfig> get_light_configs() const override;

  [[nodiscard]] bool supports_crud() const override { return true; }
  [[nodiscard]] EleroMode mode() const override { return EleroMode::MQTT; }

  // ─── MQTT Publishing ───

  /// Publish discovery payload for a cover.
  void publish_cover_discovery(uint32_t addr, const char* name);

  /// Publish discovery payload for a light.
  void publish_light_discovery(uint32_t addr, const char* name, bool has_brightness);

  /// Remove a device from Home Assistant (empty discovery payload).
  void remove_discovery(uint32_t addr, bool is_cover);

  /// Publish all discoveries for loaded devices.
  void publish_all_discoveries();

  /// Publish cover state update.
  void publish_cover_state(uint32_t addr, const char* state, float position);

  /// Publish light state update.
  void publish_light_state(uint32_t addr, bool on, float brightness);

 private:
  // ─── Slot Management ───

  /// Find a free cover slot.
  /// @return Pointer to free slot, or nullptr if all slots are used
  [[nodiscard]] EleroDynamicCover* find_free_cover_slot();

  /// Find a free light slot.
  /// @return Pointer to free slot, or nullptr if all slots are used
  [[nodiscard]] EleroDynamicLight* find_free_light_slot();

  /// Find an active cover by address.
  [[nodiscard]] EleroDynamicCover* find_cover(uint32_t address);

  /// Find an active light by address.
  [[nodiscard]] EleroDynamicLight* find_light(uint32_t address);

  /// Activate a cover slot with config.
  [[nodiscard]] bool activate_cover(const NvsDeviceConfig& config);

  /// Activate a light slot with config.
  [[nodiscard]] bool activate_light(const NvsDeviceConfig& config);

  /// Deactivate a cover slot.
  [[nodiscard]] bool deactivate_cover(uint32_t address);

  /// Deactivate a light slot.
  [[nodiscard]] bool deactivate_light(uint32_t address);

  // ─── MQTT Topics ───

  [[nodiscard]] std::string cover_cmd_topic(uint32_t addr) const;
  [[nodiscard]] std::string cover_state_topic(uint32_t addr) const;
  [[nodiscard]] std::string cover_position_topic(uint32_t addr) const;
  [[nodiscard]] std::string cover_set_position_topic(uint32_t addr) const;
  [[nodiscard]] std::string light_cmd_topic(uint32_t addr) const;
  [[nodiscard]] std::string light_state_topic(uint32_t addr) const;
  [[nodiscard]] std::string discovery_topic(const char* type, uint32_t addr) const;
  [[nodiscard]] static std::string addr_to_hex(uint32_t addr);

  // ─── Command Handling ───

  void subscribe_to_commands();
  void on_cover_command(uint32_t addr, const std::string& payload);
  void on_cover_position(uint32_t addr, const std::string& payload);
  void on_light_command(uint32_t addr, const std::string& payload);

  // ─── State ───

  // Dependencies (created in setup())
  std::unique_ptr<NvsStorage> nvs_;
  std::unique_ptr<MqttPublisher> mqtt_;
  Elero* hub_{nullptr};  // Borrowed, not owned

  // Configuration (set via setters from Python codegen)
  std::string topic_prefix_{"elero"};
  std::string discovery_prefix_{"homeassistant"};
  std::string device_name_{"Elero Gateway"};
  std::string device_mac_;

  // Pre-allocated slots (set via set_cover_slots/set_light_slots)
  EleroDynamicCover* cover_slots_{nullptr};
  size_t cover_slot_count_{0};
  EleroDynamicLight* light_slots_{nullptr};
  size_t light_slot_count_{0};

  // Track published discoveries (for reconnection handling)
  std::map<uint32_t, bool> cover_discovery_published_;
  std::map<uint32_t, bool> light_discovery_published_;

  bool setup_complete_{false};
  bool was_connected_{false};
};

}  // namespace esphome::elero
