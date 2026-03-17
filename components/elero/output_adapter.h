/// @file output_adapter.h
/// @brief Abstract interface for output adapters (WebSocket, MQTT, HA native API, Matter).
///
/// Adapters are thin, stateless translators. They:
/// - Read device state on demand (never cache it)
/// - Publish in their own format (MQTT topics, ESPHome entities, WS JSON, Matter attributes)
/// - Route commands FROM consumers back TO the registry
///
/// Adding a new output mode (e.g., Matter) = implementing this interface.
/// Zero changes to Layers 1-3.

#pragma once

#include "device.h"

namespace esphome::elero {

struct RfPacketInfo;  // Forward declaration (defined in elero.h)

class DeviceRegistry;  // Forward declaration

class OutputAdapter {
 public:
    virtual ~OutputAdapter() = default;

    /// Called once during ESPHome setup phase.
    /// @param registry The device registry (for CRUD, iteration, command routing)
    virtual void setup(DeviceRegistry &registry) = 0;

    /// Called every ESPHome loop iteration.
    virtual void loop() = 0;

    /// A device was added to the registry (activated from NVS or CRUD).
    virtual void on_device_added(const Device &dev) = 0;

    /// A device is about to be removed from the registry.
    virtual void on_device_removed(const Device &dev) = 0;

    /// A device's state changed (RF status update, command issued, timeout, etc.).
    virtual void on_state_changed(const Device &dev) = 0;

    /// A device's config was updated (CRUD update, not state change).
    virtual void on_config_changed(const Device &dev) {}

    /// A raw RF packet was decoded (for web UI forwarding, logging, etc.).
    /// Not all adapters need this — default is no-op.
    virtual void on_rf_packet(const RfPacketInfo &pkt) {}
};

}  // namespace esphome::elero
