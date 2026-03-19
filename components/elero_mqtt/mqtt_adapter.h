/// @file mqtt_adapter.h
/// @brief MQTT output adapter — publishes HA discovery, state, and subscribes to commands.
///
/// Implements OutputAdapter for MQTT mode. Reads device state from the registry
/// on demand, publishes in HA's MQTT discovery format, and routes incoming MQTT
/// commands back through the registry's command senders.
///
/// On MQTT (re)connect, runs stale discovery cleanup:
/// 1. Subscribes to wildcard discovery topics to collect retained messages
/// 2. After a delay, diffs collected vs expected active topics
/// 3. Publishes empty retained to remove stale discoveries
/// 4. Then publishes all active discoveries

#pragma once

#include "../elero/output_adapter.h"
#include "mqtt_context.h"
#include "mqtt_publisher.h"
#include "esphome_mqtt_adapter.h"
#include <vector>
#include <string>

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

    // ── Shared helpers ──
    void remove_all_discovery_(const Device &dev);

    // ── Gateway sensor ──
    void publish_gateway_discovery_();
    void publish_gateway_state_();

    // ── Stale discovery cleanup ──
    void start_stale_collection_();
    void finish_stale_cleanup_();
    void collect_expected_topics_(std::vector<std::string> &out) const;

    // ── Reconnect ──
    void republish_all_();

    MqttContext ctx_;
    EspHomeMqttAdapter mqtt_adapter_;
    bool mqtt_was_connected_{false};
    DeviceRegistry *registry_{nullptr};

    // ── Stale cleanup state ──
    enum class CleanupState : uint8_t { IDLE, COLLECTING, DONE };
    static constexpr uint32_t STALE_COLLECT_DELAY_MS = 500;
    static constexpr const char *DISCOVERY_DOMAINS[] = {
        "cover", "light", "sensor", "binary_sensor"
    };
    static constexpr size_t DISCOVERY_DOMAIN_COUNT = 4;
    CleanupState cleanup_state_{CleanupState::IDLE};
    uint32_t collect_start_ms_{0};
    std::vector<std::string> collected_topics_;
};

}  // namespace elero
}  // namespace esphome
