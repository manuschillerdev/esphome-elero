/// @file mqtt_adapter.h
/// @brief MQTT output adapter — publishes HA discovery, state, and subscribes to commands.
///
/// Implements OutputAdapter for MQTT mode. Reads device state from the registry
/// on demand, publishes in HA's MQTT discovery format, and routes incoming MQTT
/// commands back through the registry's command senders.

#pragma once

#include "../elero/output_adapter.h"
#include "mqtt_context.h"
#include "mqtt_publisher.h"
#include "esphome_mqtt_adapter.h"

namespace esphome {
namespace elero {

class MqttAdapter : public OutputAdapter {
 public:
    // ═════════════════════════════════════════════════════════════════════════
    // CONFIGURATION (called by codegen before setup)
    // ═════════════════════════════════════════════════════════════════════════

    void set_topic_prefix(const std::string &prefix) { ctx_.topic_prefix = prefix; }
    void set_discovery_prefix(const std::string &prefix) { ctx_.discovery_prefix = prefix; }
    void set_device_name(const std::string &name) { ctx_.device_name = name; }

    // ═════════════════════════════════════════════════════════════════════════
    // OUTPUT ADAPTER INTERFACE
    // ═════════════════════════════════════════════════════════════════════════

    void setup(DeviceRegistry &registry) override;
    void loop() override;

    void on_device_added(const Device &dev) override;
    void on_device_removed(const Device &dev) override;
    void on_state_changed(const Device &dev) override;
    void on_config_changed(const Device &dev) override;
    void on_rf_packet(const RfPacketInfo &pkt) override {}  // MQTT doesn't forward raw RF

 private:
    // ── Cover helpers ──
    void publish_cover_discovery_(const Device &dev);
    void publish_cover_state_(const Device &dev);
    void subscribe_cover_commands_(const Device &dev);

    // ── Light helpers ──
    void publish_light_discovery_(const Device &dev);
    void publish_light_state_(const Device &dev);
    void subscribe_light_commands_(const Device &dev);

    // ── Remote helpers ──
    void publish_remote_discovery_(const Device &dev);
    void publish_remote_state_(const Device &dev);

    // ── Reconnect ──
    void republish_all_();

    MqttContext ctx_;
    EspHomeMqttAdapter mqtt_adapter_;
    bool mqtt_was_connected_{false};
    DeviceRegistry *registry_{nullptr};
};

}  // namespace elero
}  // namespace esphome
