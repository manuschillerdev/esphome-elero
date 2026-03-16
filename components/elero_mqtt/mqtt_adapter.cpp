/// @file mqtt_adapter.cpp
/// @brief MQTT output adapter implementation.
///
/// Publishes HA discovery configs, state updates, and subscribes to command topics.
/// Reads all state from Device structs via the DeviceRegistry — no old entity classes.

#include "mqtt_adapter.h"
#include "../elero/device.h"
#include "../elero/device_registry.h"
#include "../elero/cover_sm.h"
#include "../elero/light_sm.h"
#include "../elero/overloaded.h"
#include "../elero/elero_strings.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/json/json_util.h"

namespace esphome {
namespace elero {

static const char *const TAG = "elero.mqtt.adapter";

// ═══════════════════════════════════════════════════════════════════════════════
// TOPIC / ID HELPERS
// ═══════════════════════════════════════════════════════════════════════════════

static std::string cover_base_topic(const MqttContext &ctx, uint32_t addr) {
    return ctx.topic_prefix + "/cover/" + addr_hex(addr);
}

static std::string light_base_topic(const MqttContext &ctx, uint32_t addr) {
    return ctx.topic_prefix + "/light/" + addr_hex(addr);
}

static std::string remote_state_topic(const MqttContext &ctx, uint32_t addr) {
    return ctx.topic_prefix + "/remote/" + addr_hex(addr) + "/state";
}

static std::string cover_object_id(const MqttContext &ctx, uint32_t addr) {
    return ctx.device_id + "_cover_" + addr_hex(addr);
}

static std::string cover_rssi_object_id(const MqttContext &ctx, uint32_t addr) {
    return ctx.device_id + "_cover_" + addr_hex(addr) + "_rssi";
}

static std::string cover_state_sensor_object_id(const MqttContext &ctx, uint32_t addr) {
    return ctx.device_id + "_cover_" + addr_hex(addr) + "_state";
}

static std::string light_object_id(const MqttContext &ctx, uint32_t addr) {
    return ctx.device_id + "_light_" + addr_hex(addr);
}

static std::string light_rssi_object_id(const MqttContext &ctx, uint32_t addr) {
    return ctx.device_id + "_light_" + addr_hex(addr) + "_rssi";
}

static std::string remote_object_id(const MqttContext &ctx, uint32_t addr) {
    return ctx.device_id + "_remote_" + addr_hex(addr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// OPERATION STRING MAPPING
// ═══════════════════════════════════════════════════════════════════════════════

static const char *operation_to_mqtt_str(cover_sm::Operation op) {
    switch (op) {
        case cover_sm::Operation::OPENING: return "opening";
        case cover_sm::Operation::CLOSING: return "closing";
        default: return "stopped";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::setup(DeviceRegistry &registry) {
    registry_ = &registry;

    // Wire up the MQTT publisher
    ctx_.mqtt = &mqtt_adapter_;

    // Derive device_id from ESPHome app name
    ctx_.device_id = App.get_name();

    ESP_LOGD(TAG, "MQTT adapter setup: prefix=%s, discovery=%s, device=%s",
             ctx_.topic_prefix.c_str(), ctx_.discovery_prefix.c_str(),
             ctx_.device_name.c_str());

    // Publish birth if already connected
    if (ctx_.mqtt->is_connected()) {
        ctx_.publish_birth();
        mqtt_was_connected_ = true;
    }
}

void MqttAdapter::loop() {
    bool connected = ctx_.mqtt->is_connected();
    if (connected && !mqtt_was_connected_) {
        // Just reconnected — republish everything
        ESP_LOGI(TAG, "MQTT reconnected, republishing all discoveries");
        ctx_.publish_birth();
        republish_all_();
    }
    mqtt_was_connected_ = connected;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ADAPTER CALLBACKS
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::on_device_added(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;

    if (dev.is_cover()) {
        publish_cover_discovery_(dev);
        subscribe_cover_commands_(dev);
        publish_cover_state_(dev);
    } else if (dev.is_light()) {
        publish_light_discovery_(dev);
        subscribe_light_commands_(dev);
        publish_light_state_(dev);
    } else if (dev.is_remote()) {
        publish_remote_discovery_(dev);
        publish_remote_state_(dev);
    }
}

void MqttAdapter::on_device_removed(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;

    uint32_t addr = dev.config.dst_address;

    if (dev.is_cover()) {
        // Unsubscribe from command topics
        auto base = cover_base_topic(ctx_, addr);
        ctx_.mqtt->unsubscribe((base + "/set").c_str());
        ctx_.mqtt->unsubscribe((base + "/tilt").c_str());

        // Remove discovery configs (empty retained payload)
        ctx_.remove_discovery("cover", cover_object_id(ctx_, addr));
        ctx_.remove_discovery("sensor", cover_rssi_object_id(ctx_, addr));
        ctx_.remove_discovery("sensor", cover_state_sensor_object_id(ctx_, addr));
    } else if (dev.is_light()) {
        auto base = light_base_topic(ctx_, addr);
        ctx_.mqtt->unsubscribe((base + "/set").c_str());

        ctx_.remove_discovery("light", light_object_id(ctx_, addr));
        ctx_.remove_discovery("sensor", light_rssi_object_id(ctx_, addr));
    } else if (dev.is_remote()) {
        ctx_.remove_discovery("sensor", remote_object_id(ctx_, addr));
    }

    ESP_LOGD(TAG, "Removed discovery for 0x%06x", addr);
}

void MqttAdapter::on_state_changed(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;

    if (dev.is_cover()) {
        publish_cover_state_(dev);
    } else if (dev.is_light()) {
        publish_light_state_(dev);
    } else if (dev.is_remote()) {
        publish_remote_state_(dev);
    }
}

void MqttAdapter::on_config_changed(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;

    // Re-publish discovery with updated config (name, tilt support, etc.)
    if (dev.is_cover()) {
        publish_cover_discovery_(dev);
    } else if (dev.is_light()) {
        publish_light_discovery_(dev);
    } else if (dev.is_remote()) {
        publish_remote_discovery_(dev);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// COVER
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::publish_cover_discovery_(const Device &dev) {
    uint32_t addr = dev.config.dst_address;
    auto hex = addr_hex(addr);
    auto oid = cover_object_id(ctx_, addr);
    auto base = cover_base_topic(ctx_, addr);
    bool tilt = dev.config.supports_tilt != 0;

    // Cover entity
    std::string payload = json::build_json([&](JsonObject root) {
        root["name"] = dev.config.name;
        root["unique_id"] = ctx_.device_id + "_cover_" + hex;
        root["command_topic"] = base + "/set";
        root["state_topic"] = base + "/state";
        root["position_topic"] = base + "/position";
        root["payload_open"] = action::OPEN;
        root["payload_close"] = action::CLOSE;
        root["payload_stop"] = action::STOP;
        root["position_open"] = 100;
        root["position_closed"] = 0;
        if (tilt) {
            root["tilt_command_topic"] = base + "/tilt";
        }
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.mqtt->publish(
        (ctx_.discovery_prefix + "/cover/" + oid + "/config").c_str(),
        payload.c_str(), true);

    // RSSI sensor entity (grouped under same HA device)
    std::string rssi_payload = json::build_json([&](JsonObject root) {
        root["name"] = std::string(dev.config.name) + " RSSI";
        root["unique_id"] = cover_rssi_object_id(ctx_, addr);
        root["state_topic"] = base + "/rssi";
        root["unit_of_measurement"] = "dBm";
        root["device_class"] = "signal_strength";
        root["entity_category"] = "diagnostic";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.mqtt->publish(
        (ctx_.discovery_prefix + "/sensor/" + cover_rssi_object_id(ctx_, addr) + "/config").c_str(),
        rssi_payload.c_str(), true);

    // Blind state sensor entity (e.g. top, bottom, moving_up, ...)
    std::string state_payload = json::build_json([&](JsonObject root) {
        root["name"] = std::string(dev.config.name) + " State";
        root["unique_id"] = cover_state_sensor_object_id(ctx_, addr);
        root["state_topic"] = base + "/blind_state";
        root["entity_category"] = "diagnostic";
        root["icon"] = "mdi:state-machine";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.mqtt->publish(
        (ctx_.discovery_prefix + "/sensor/" + cover_state_sensor_object_id(ctx_, addr) + "/config").c_str(),
        state_payload.c_str(), true);

    ESP_LOGD(TAG, "Published cover discovery for 0x%06x", addr);
}

void MqttAdapter::publish_cover_state_(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;

    auto base = cover_base_topic(ctx_, dev.config.dst_address);
    const auto &cover = std::get<CoverDevice>(dev.logic);
    auto ctx = cover_context(dev.config);
    uint32_t now = millis();

    // Operation state (opening/closing/stopped)
    auto op = cover_sm::operation(cover.state);
    ctx_.mqtt->publish((base + "/state").c_str(), operation_to_mqtt_str(op), false);

    // Position (0-100)
    float pos = cover_sm::position(cover.state, now, ctx);
    char pos_buf[8];
    snprintf(pos_buf, sizeof(pos_buf), "%d", static_cast<int>(pos * PERCENT_SCALE));
    ctx_.mqtt->publish((base + "/position").c_str(), pos_buf, false);

    // RSSI
    char rssi_buf[12];
    snprintf(rssi_buf, sizeof(rssi_buf), "%.0f", round_rssi(dev.rf.last_rssi));
    ctx_.mqtt->publish((base + "/rssi").c_str(), rssi_buf, false);

    // Blind state string (top, bottom, moving_up, etc.)
    ctx_.mqtt->publish((base + "/blind_state").c_str(),
                       elero_state_to_string(dev.rf.last_state_raw), false);
}

void MqttAdapter::subscribe_cover_commands_(const Device &dev) {
    uint32_t addr = dev.config.dst_address;
    auto base = cover_base_topic(ctx_, addr);

    // Command topic (open/close/stop)
    ctx_.mqtt->subscribe((base + "/set").c_str(),
        [this, addr](const char *, const char *payload) {
            Device *d = registry_->find(addr, DeviceType::COVER);
            if (d == nullptr) return;

            uint8_t cmd_byte = elero_action_to_command(payload);
            if (cmd_byte == packet::command::INVALID) {
                ESP_LOGW(TAG, "Unknown cover action: %s", payload);
                return;
            }

            // Transition state machine
            auto &cover = std::get<CoverDevice>(d->logic);
            auto ctx = cover_context(d->config);
            uint32_t now = millis();
            cover.state = cover_sm::on_command(cover.state, cmd_byte, now, ctx);

            // Enqueue TX command
            if (cmd_byte == packet::command::STOP) {
                d->sender.clear_queue();
            }
            d->sender.enqueue(cmd_byte);
            cover.poll.on_command_sent(now);
        });

    // Tilt topic
    ctx_.mqtt->subscribe((base + "/tilt").c_str(),
        [this, addr](const char *, const char *) {
            Device *d = registry_->find(addr, DeviceType::COVER);
            if (d == nullptr) return;

            uint8_t cmd_byte = elero_action_to_command(action::TILT);
            if (cmd_byte == packet::command::INVALID) return;

            auto &cover = std::get<CoverDevice>(d->logic);
            auto ctx = cover_context(d->config);
            uint32_t now = millis();
            cover.state = cover_sm::on_command(cover.state, cmd_byte, now, ctx);

            d->sender.enqueue(cmd_byte);
            cover.poll.on_command_sent(now);
        });

    ESP_LOGD(TAG, "Subscribed to cover commands for 0x%06x", addr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LIGHT
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::publish_light_discovery_(const Device &dev) {
    uint32_t addr = dev.config.dst_address;
    auto hex = addr_hex(addr);
    auto oid = light_object_id(ctx_, addr);
    auto base = light_base_topic(ctx_, addr);
    bool has_brightness = dev.config.dim_duration_ms > 0;

    // Light entity (JSON schema for HA brightness support)
    std::string payload = json::build_json([&](JsonObject root) {
        root["name"] = dev.config.name;
        root["unique_id"] = ctx_.device_id + "_light_" + hex;
        root["schema"] = "json";
        root["command_topic"] = base + "/set";
        root["state_topic"] = base + "/state";
        if (has_brightness) {
            root["brightness"] = true;
            root["brightness_scale"] = static_cast<int>(PERCENT_SCALE);
        }
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.mqtt->publish(
        (ctx_.discovery_prefix + "/light/" + oid + "/config").c_str(),
        payload.c_str(), true);

    // RSSI sensor entity (grouped under same HA device)
    std::string rssi_payload = json::build_json([&](JsonObject root) {
        root["name"] = std::string(dev.config.name) + " RSSI";
        root["unique_id"] = light_rssi_object_id(ctx_, addr);
        root["state_topic"] = base + "/rssi";
        root["unit_of_measurement"] = "dBm";
        root["device_class"] = "signal_strength";
        root["entity_category"] = "diagnostic";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.mqtt->publish(
        (ctx_.discovery_prefix + "/sensor/" + light_rssi_object_id(ctx_, addr) + "/config").c_str(),
        rssi_payload.c_str(), true);

    ESP_LOGD(TAG, "Published light discovery for 0x%06x", addr);
}

void MqttAdapter::publish_light_state_(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;

    auto base = light_base_topic(ctx_, dev.config.dst_address);
    const auto &light = std::get<LightDevice>(dev.logic);
    auto ctx = light_context(dev.config);
    uint32_t now = millis();

    // JSON schema state (HA expects {"state":"ON"} or {"state":"ON","brightness":80})
    std::string payload = json::build_json([&](JsonObject root) {
        root["state"] = light_sm::is_on(light.state) ? ha_state::ON : ha_state::OFF;
        if (dev.config.dim_duration_ms > 0) {
            root["brightness"] = static_cast<int>(
                light_sm::brightness(light.state, now, ctx) * PERCENT_SCALE);
        }
    });
    ctx_.mqtt->publish((base + "/state").c_str(), payload.c_str(), false);

    // RSSI on separate topic
    char rssi_buf[12];
    snprintf(rssi_buf, sizeof(rssi_buf), "%.0f", round_rssi(dev.rf.last_rssi));
    ctx_.mqtt->publish((base + "/rssi").c_str(), rssi_buf, false);
}

void MqttAdapter::subscribe_light_commands_(const Device &dev) {
    uint32_t addr = dev.config.dst_address;
    auto base = light_base_topic(ctx_, addr);

    ctx_.mqtt->subscribe((base + "/set").c_str(),
        [this, addr](const char *, const char *payload) {
            Device *d = registry_->find(addr, DeviceType::LIGHT);
            if (d == nullptr) return;

            auto &light = std::get<LightDevice>(d->logic);
            auto ctx = light_context(d->config);
            uint32_t now = millis();

            // Try JSON schema first (from HA with brightness support)
            bool handled = json::parse_json(payload, [&](JsonObject root) -> bool {
                if (root["state"].is<const char *>()) {
                    const char *state = root["state"];
                    if (strcmp(state, ha_state::OFF) == 0) {
                        light.state = light_sm::on_turn_off(light.state);
                        d->sender.enqueue(packet::command::DOWN);
                        return true;
                    }
                }
                if (root["brightness"].is<int>()) {
                    // HA sends brightness on scale 0-100 (brightness_scale in discovery)
                    float brightness = static_cast<float>(root["brightness"].as<int>()) / PERCENT_SCALE;
                    light.state = light_sm::on_set_brightness(light.state, brightness, now, ctx);
                    // Determine direction from resulting state variant
                    if (std::holds_alternative<light_sm::DimmingUp>(light.state)) {
                        d->sender.enqueue(packet::command::UP);
                    } else if (std::holds_alternative<light_sm::DimmingDown>(light.state)) {
                        d->sender.enqueue(packet::command::DOWN);
                    } else if (light_sm::is_on(light.state)) {
                        d->sender.enqueue(packet::command::UP);
                    }
                } else if (root["state"].is<const char *>()) {
                    // ON without brightness = full on
                    light.state = light_sm::on_turn_on(light.state, now, ctx);
                    d->sender.enqueue(packet::command::UP);
                }
                return true;
            });

            // Fall back to simple string action
            if (!handled) {
                uint8_t cmd_byte = elero_action_to_command(payload);
                if (cmd_byte != packet::command::INVALID) {
                    if (strcmp(payload, action::OFF) == 0) {
                        light.state = light_sm::on_turn_off(light.state);
                    } else if (strcmp(payload, action::ON) == 0) {
                        light.state = light_sm::on_turn_on(light.state, now, ctx);
                    }
                    d->sender.enqueue(cmd_byte);
                } else {
                    ESP_LOGW(TAG, "Unknown light action: %s", payload);
                }
            }
        });

    ESP_LOGD(TAG, "Subscribed to light commands for 0x%06x", addr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// REMOTE
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::publish_remote_discovery_(const Device &dev) {
    uint32_t addr = dev.config.dst_address;
    auto oid = remote_object_id(ctx_, addr);
    auto st = remote_state_topic(ctx_, addr);

    std::string payload = json::build_json([&](JsonObject root) {
        root["name"] = dev.config.name;
        root["unique_id"] = oid;
        root["state_topic"] = st;
        root["value_template"] = "{{ value_json.rssi }}";
        root["unit_of_measurement"] = "dBm";
        root["device_class"] = "signal_strength";
        root["json_attributes_topic"] = st;
        root["json_attributes_template"] = "{{ value_json | tojson }}";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });

    ctx_.mqtt->publish(
        (ctx_.discovery_prefix + "/sensor/" + oid + "/config").c_str(),
        payload.c_str(), true);

    ESP_LOGD(TAG, "Published remote discovery for 0x%06x", addr);
}

void MqttAdapter::publish_remote_state_(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;

    uint32_t addr = dev.config.dst_address;
    auto topic = remote_state_topic(ctx_, addr);
    const auto &remote = std::get<RemoteDevice>(dev.logic);

    std::string payload = json::build_json([&](JsonObject root) {
        root["rssi"] = dev.rf.last_rssi;
        root["address"] = hex_str(addr);
        root["title"] = dev.config.name;
        root["last_seen"] = dev.rf.last_seen_ms;
        root["last_channel"] = remote.last_channel;
        root["last_command"] = hex_str8(remote.last_command);
        root["last_target"] = hex_str(remote.last_target);
    });

    ctx_.mqtt->publish(topic.c_str(), payload.c_str(), false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// RECONNECT
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::republish_all_() {
    if (registry_ == nullptr) return;

    registry_->for_each_active([this](const Device &dev) {
        if (dev.is_cover()) {
            publish_cover_discovery_(dev);
            subscribe_cover_commands_(dev);
            publish_cover_state_(dev);
        } else if (dev.is_light()) {
            publish_light_discovery_(dev);
            subscribe_light_commands_(dev);
            publish_light_state_(dev);
        } else if (dev.is_remote()) {
            publish_remote_discovery_(dev);
            publish_remote_state_(dev);
        }
    });

    ESP_LOGI(TAG, "Republished all discoveries after reconnect");
}

}  // namespace elero
}  // namespace esphome
