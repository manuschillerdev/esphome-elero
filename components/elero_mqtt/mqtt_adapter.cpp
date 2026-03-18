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
#include "../elero/state_snapshot.h"
#include "../elero/overloaded.h"
#include "../elero/elero_strings.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/json/json_util.h"

namespace esphome {
namespace elero {

static const char *const TAG = "elero.mqtt.adapter";

// ═══════════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::setup(DeviceRegistry &registry) {
    registry_ = &registry;
    ctx_.mqtt = &mqtt_adapter_;
    ctx_.device_id = App.get_name();

    ESP_LOGD(TAG, "MQTT adapter setup: prefix=%s, discovery=%s, device=%s",
             ctx_.topic_prefix.c_str(), ctx_.discovery_prefix.c_str(),
             ctx_.device_name.c_str());

    if (ctx_.mqtt->is_connected()) {
        ctx_.publish_birth();
        mqtt_was_connected_ = true;
    }
}

void MqttAdapter::loop() {
    bool connected = ctx_.mqtt->is_connected();
    if (connected && !mqtt_was_connected_) {
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
        ctx_.unsubscribe(DeviceType::COVER, addr, mqtt_topic::SET);
        ctx_.unsubscribe(DeviceType::COVER, addr, mqtt_topic::TILT);

        ctx_.remove_discovery(ha_discovery::COVER, ctx_.object_id(DeviceType::COVER, addr));
        ctx_.remove_discovery(ha_discovery::SENSOR, ctx_.object_id(DeviceType::COVER, addr, "rssi"));
        ctx_.remove_discovery(ha_discovery::SENSOR, ctx_.object_id(DeviceType::COVER, addr, "state"));
        ctx_.remove_discovery(ha_discovery::BINARY_SENSOR, ctx_.object_id(DeviceType::COVER, addr, "problem"));
    } else if (dev.is_light()) {
        ctx_.unsubscribe(DeviceType::LIGHT, addr, mqtt_topic::SET);

        ctx_.remove_discovery(ha_discovery::LIGHT, ctx_.object_id(DeviceType::LIGHT, addr));
        ctx_.remove_discovery(ha_discovery::SENSOR, ctx_.object_id(DeviceType::LIGHT, addr, "rssi"));
        ctx_.remove_discovery(ha_discovery::SENSOR, ctx_.object_id(DeviceType::LIGHT, addr, "state"));
        ctx_.remove_discovery(ha_discovery::BINARY_SENSOR, ctx_.object_id(DeviceType::LIGHT, addr, "problem"));
    } else if (dev.is_remote()) {
        ctx_.remove_discovery(ha_discovery::SENSOR, ctx_.object_id(DeviceType::REMOTE, addr));
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
    auto oid = ctx_.object_id(DeviceType::COVER, addr);
    bool tilt = dev.config.supports_tilt != 0;

    // Cover entity
    std::string payload = json::build_json([&](JsonObject root) {
        root["name"] = dev.config.name;
        root["unique_id"] = oid;
        root["command_topic"] = ctx_.topic(DeviceType::COVER, addr, mqtt_topic::SET);
        root["state_topic"] = ctx_.topic(DeviceType::COVER, addr, mqtt_topic::STATE);
        root["position_topic"] = ctx_.topic(DeviceType::COVER, addr, mqtt_topic::POSITION);
        root["payload_open"] = action::OPEN;
        root["payload_close"] = action::CLOSE;
        root["payload_stop"] = action::STOP;
        root["position_open"] = 100;
        root["position_closed"] = 0;
        root["device_class"] = ha_cover_class_str(static_cast<HaCoverClass>(dev.config.ha_device_class));
        root["json_attributes_topic"] = ctx_.topic(DeviceType::COVER, addr, mqtt_topic::ATTRIBUTES);
        if (tilt) {
            root["tilt_command_topic"] = ctx_.topic(DeviceType::COVER, addr, mqtt_topic::TILT);
            root["tilt_status_topic"] = ctx_.topic(DeviceType::COVER, addr, mqtt_topic::TILT_STATE);
        }
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.publish_discovery(ha_discovery::COVER, oid, payload);

    // RSSI sensor
    auto rssi_oid = ctx_.object_id(DeviceType::COVER, addr, "rssi");
    std::string rssi_payload = json::build_json([&](JsonObject root) {
        root["name"] = std::string(dev.config.name) + " RSSI";
        root["unique_id"] = rssi_oid;
        root["state_topic"] = ctx_.topic(DeviceType::COVER, addr, mqtt_topic::RSSI);
        root["unit_of_measurement"] = "dBm";
        root["device_class"] = "signal_strength";
        root["entity_category"] = "diagnostic";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.publish_discovery(ha_discovery::SENSOR, rssi_oid, rssi_payload);

    // Blind state sensor
    auto state_oid = ctx_.object_id(DeviceType::COVER, addr, "state");
    std::string state_payload = json::build_json([&](JsonObject root) {
        root["name"] = std::string(dev.config.name) + " State";
        root["unique_id"] = state_oid;
        root["state_topic"] = ctx_.topic(DeviceType::COVER, addr, mqtt_topic::DEVICE_STATE);
        root["entity_category"] = "diagnostic";
        root["icon"] = "mdi:state-machine";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.publish_discovery(ha_discovery::SENSOR, state_oid, state_payload);

    // Problem binary_sensor
    auto problem_oid = ctx_.object_id(DeviceType::COVER, addr, "problem");
    std::string problem_payload = json::build_json([&](JsonObject root) {
        root["name"] = std::string(dev.config.name) + " Problem";
        root["unique_id"] = problem_oid;
        root["state_topic"] = ctx_.topic(DeviceType::COVER, addr, mqtt_topic::PROBLEM);
        root["device_class"] = "problem";
        root["entity_category"] = "diagnostic";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.publish_discovery(ha_discovery::BINARY_SENSOR, problem_oid, problem_payload);

    ESP_LOGD(TAG, "Published cover discovery for 0x%06x", addr);
}

void MqttAdapter::publish_cover_state_(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;

    uint32_t addr = dev.config.dst_address;
    auto snap = compute_cover_snapshot(dev, millis());

    ctx_.publish(DeviceType::COVER, addr, mqtt_topic::STATE, snap.ha_state, false);

    char pos_buf[8];
    snprintf(pos_buf, sizeof(pos_buf), "%d", static_cast<int>(snap.position * PERCENT_SCALE));
    ctx_.publish(DeviceType::COVER, addr, mqtt_topic::POSITION, pos_buf, false);

    char rssi_buf[12];
    snprintf(rssi_buf, sizeof(rssi_buf), "%.0f", round_rssi(snap.rssi));
    ctx_.publish(DeviceType::COVER, addr, mqtt_topic::RSSI, rssi_buf, false);

    ctx_.publish(DeviceType::COVER, addr, mqtt_topic::DEVICE_STATE, snap.state_string, false);
    ctx_.publish(DeviceType::COVER, addr, mqtt_topic::PROBLEM, snap.is_problem ? ha_state::ON : ha_state::OFF, false);

    std::string attrs = json::build_json([&](JsonObject root) {
        root["last_seen"] = snap.last_seen_ms;
        root["command_source"] = snap.command_source;
        root["tilted"] = snap.tilted;
        root["device_class"] = snap.device_class;
        root["problem_type"] = snap.problem_type;
    });
    ctx_.publish(DeviceType::COVER, addr, mqtt_topic::ATTRIBUTES, attrs, false);

    if (dev.config.supports_tilt != 0) {
        ctx_.publish(DeviceType::COVER, addr, mqtt_topic::TILT_STATE, snap.tilted ? "100" : "0", false);
    }
}

void MqttAdapter::subscribe_cover_commands_(const Device &dev) {
    uint32_t addr = dev.config.dst_address;

    ctx_.subscribe(DeviceType::COVER, addr, mqtt_topic::SET,
        [this, addr](const char *, const char *payload) {
            Device *d = registry_->find(addr, DeviceType::COVER);
            if (d == nullptr) return;

            uint8_t cmd_byte = elero_action_to_command(payload);
            if (cmd_byte == packet::command::INVALID) {
                ESP_LOGW(TAG, "Unknown cover action: %s", payload);
                return;
            }

            auto &cover = std::get<CoverDevice>(d->logic);
            auto ctx = cover_context(d->config);
            uint32_t now = millis();
            cover.last_command_source = CommandSource::HUB;
            cover.state = cover_sm::on_command(cover.state, cmd_byte, now, ctx);

            if (cmd_byte == packet::command::STOP) {
                d->sender.clear_queue();
            }
            (void) d->sender.enqueue(cmd_byte);
            cover.poll.on_command_sent(now);
        });

    ctx_.subscribe(DeviceType::COVER, addr, mqtt_topic::TILT,
        [this, addr](const char *, const char *) {
            Device *d = registry_->find(addr, DeviceType::COVER);
            if (d == nullptr) return;

            uint8_t cmd_byte = elero_action_to_command(action::TILT);
            if (cmd_byte == packet::command::INVALID) return;

            auto &cover = std::get<CoverDevice>(d->logic);
            auto ctx = cover_context(d->config);
            uint32_t now = millis();
            cover.last_command_source = CommandSource::HUB;
            cover.state = cover_sm::on_command(cover.state, cmd_byte, now, ctx);

            (void) d->sender.enqueue(cmd_byte);
            cover.poll.on_command_sent(now);
        });

    ESP_LOGD(TAG, "Subscribed to cover commands for 0x%06x", addr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LIGHT
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::publish_light_discovery_(const Device &dev) {
    uint32_t addr = dev.config.dst_address;
    auto oid = ctx_.object_id(DeviceType::LIGHT, addr);
    bool has_brightness = dev.config.dim_duration_ms > 0;

    // Light entity
    std::string payload = json::build_json([&](JsonObject root) {
        root["name"] = dev.config.name;
        root["unique_id"] = oid;
        root["schema"] = "json";
        root["command_topic"] = ctx_.topic(DeviceType::LIGHT, addr, mqtt_topic::SET);
        root["state_topic"] = ctx_.topic(DeviceType::LIGHT, addr, mqtt_topic::STATE);
        if (has_brightness) {
            root["brightness"] = true;
            root["brightness_scale"] = static_cast<int>(PERCENT_SCALE);
        }
        root["json_attributes_topic"] = ctx_.topic(DeviceType::LIGHT, addr, mqtt_topic::ATTRIBUTES);
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.publish_discovery(ha_discovery::LIGHT, oid, payload);

    // RSSI sensor
    auto rssi_oid = ctx_.object_id(DeviceType::LIGHT, addr, "rssi");
    std::string rssi_payload = json::build_json([&](JsonObject root) {
        root["name"] = std::string(dev.config.name) + " RSSI";
        root["unique_id"] = rssi_oid;
        root["state_topic"] = ctx_.topic(DeviceType::LIGHT, addr, mqtt_topic::RSSI);
        root["unit_of_measurement"] = "dBm";
        root["device_class"] = "signal_strength";
        root["entity_category"] = "diagnostic";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.publish_discovery(ha_discovery::SENSOR, rssi_oid, rssi_payload);

    // Light state sensor (diagnostic)
    auto state_oid = ctx_.object_id(DeviceType::LIGHT, addr, "state");
    std::string state_payload = json::build_json([&](JsonObject root) {
        root["name"] = std::string(dev.config.name) + " State";
        root["unique_id"] = state_oid;
        root["state_topic"] = ctx_.topic(DeviceType::LIGHT, addr, mqtt_topic::DEVICE_STATE);
        root["entity_category"] = "diagnostic";
        root["icon"] = "mdi:state-machine";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.publish_discovery(ha_discovery::SENSOR, state_oid, state_payload);

    // Problem binary_sensor
    auto problem_oid = ctx_.object_id(DeviceType::LIGHT, addr, "problem");
    std::string problem_payload = json::build_json([&](JsonObject root) {
        root["name"] = std::string(dev.config.name) + " Problem";
        root["unique_id"] = problem_oid;
        root["state_topic"] = ctx_.topic(DeviceType::LIGHT, addr, mqtt_topic::PROBLEM);
        root["device_class"] = "problem";
        root["entity_category"] = "diagnostic";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.publish_discovery(ha_discovery::BINARY_SENSOR, problem_oid, problem_payload);

    ESP_LOGD(TAG, "Published light discovery for 0x%06x", addr);
}

void MqttAdapter::publish_light_state_(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;

    uint32_t addr = dev.config.dst_address;
    auto snap = compute_light_snapshot(dev, millis());

    std::string payload = json::build_json([&](JsonObject root) {
        root["state"] = snap.is_on ? ha_state::ON : ha_state::OFF;
        if (dev.config.dim_duration_ms > 0) {
            root["brightness"] = static_cast<int>(snap.brightness * PERCENT_SCALE);
        }
    });
    ctx_.publish(DeviceType::LIGHT, addr, mqtt_topic::STATE, payload, false);

    char rssi_buf[12];
    snprintf(rssi_buf, sizeof(rssi_buf), "%.0f", round_rssi(snap.rssi));
    ctx_.publish(DeviceType::LIGHT, addr, mqtt_topic::RSSI, rssi_buf, false);

    ctx_.publish(DeviceType::LIGHT, addr, mqtt_topic::DEVICE_STATE, snap.state_string, false);

    // Problem state
    ctx_.publish(DeviceType::LIGHT, addr, mqtt_topic::PROBLEM, snap.is_problem ? ha_state::ON : ha_state::OFF, false);

    // JSON attributes
    std::string attrs = json::build_json([&](JsonObject root) {
        root["last_seen"] = snap.last_seen_ms;
        root["command_source"] = snap.command_source;
        root["problem_type"] = snap.problem_type;
    });
    ctx_.publish(DeviceType::LIGHT, addr, mqtt_topic::ATTRIBUTES, attrs, false);
}

void MqttAdapter::subscribe_light_commands_(const Device &dev) {
    uint32_t addr = dev.config.dst_address;

    ctx_.subscribe(DeviceType::LIGHT, addr, mqtt_topic::SET,
        [this, addr](const char *, const char *payload) {
            Device *d = registry_->find(addr, DeviceType::LIGHT);
            if (d == nullptr) return;

            auto &light = std::get<LightDevice>(d->logic);
            auto ctx = light_context(d->config);
            uint32_t now = millis();
            light.last_command_source = CommandSource::HUB;

            bool handled = json::parse_json(payload, [&](JsonObject root) -> bool {
                if (root["state"].is<const char *>()) {
                    const char *state = root["state"];
                    if (strcmp(state, ha_state::OFF) == 0) {
                        light.state = light_sm::on_turn_off(light.state);
                        (void) d->sender.enqueue(packet::command::DOWN);
                        return true;
                    }
                }
                if (root["brightness"].is<int>()) {
                    float brightness = static_cast<float>(root["brightness"].as<int>()) / PERCENT_SCALE;
                    light.state = light_sm::on_set_brightness(light.state, brightness, now, ctx);
                    if (std::holds_alternative<light_sm::DimmingUp>(light.state)) {
                        (void) d->sender.enqueue(packet::command::UP);
                    } else if (std::holds_alternative<light_sm::DimmingDown>(light.state)) {
                        (void) d->sender.enqueue(packet::command::DOWN);
                    } else if (light_sm::is_on(light.state)) {
                        (void) d->sender.enqueue(packet::command::UP);
                    }
                } else if (root["state"].is<const char *>()) {
                    light.state = light_sm::on_turn_on(light.state, now, ctx);
                    (void) d->sender.enqueue(packet::command::UP);
                }
                return true;
            });

            if (!handled) {
                uint8_t cmd_byte = elero_action_to_command(payload);
                if (cmd_byte != packet::command::INVALID) {
                    if (strcmp(payload, action::OFF) == 0) {
                        light.state = light_sm::on_turn_off(light.state);
                    } else if (strcmp(payload, action::ON) == 0) {
                        light.state = light_sm::on_turn_on(light.state, now, ctx);
                    }
                    (void) d->sender.enqueue(cmd_byte);
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
    auto oid = ctx_.object_id(DeviceType::REMOTE, addr);
    auto st = ctx_.topic(DeviceType::REMOTE, addr, mqtt_topic::STATE);

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
    ctx_.publish_discovery(ha_discovery::SENSOR, oid, payload);

    ESP_LOGD(TAG, "Published remote discovery for 0x%06x", addr);
}

void MqttAdapter::publish_remote_state_(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;

    uint32_t addr = dev.config.dst_address;
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

    ctx_.publish(DeviceType::REMOTE, addr, mqtt_topic::STATE, payload, false);
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
