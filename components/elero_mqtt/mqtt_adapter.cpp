/// @file mqtt_adapter.cpp
/// @brief MQTT output adapter implementation.
///
/// Publishes HA discovery configs, state updates, and subscribes to command topics.
/// Reads all state from Device structs via the DeviceRegistry — no old entity classes.

#include "mqtt_adapter.h"
#include "../elero/device.h"
#include "../elero/device_registry.h"
#include "../elero/state_snapshot.h"  // state_change:: flags, Published types
#include "../elero/elero_strings.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/json/json_util.h"

namespace esphome {
namespace elero {

static const char *const TAG = "elero.mqtt.adapter";

// Enum options for HA device_class: enum sensors
static const char *const COVER_STATE_OPTIONS[] = {
    "top", "bottom", "intermediate", "tilt", "top_tilt", "bottom_tilt",
    "moving_up", "moving_down", "start_moving_up", "start_moving_down",
    "stopped", "blocking", "overheated", "timeout", "unknown",
};
static const char *const LIGHT_STATE_OPTIONS[] = {
    "on", "top", "bottom", "intermediate",
    "moving_up", "moving_down", "start_moving_up", "start_moving_down",
    "stopped", "blocking", "overheated", "timeout", "unknown",
};

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
        start_stale_collection_();
        mqtt_was_connected_ = true;
    }
}

void MqttAdapter::loop() {
    bool connected = ctx_.mqtt->is_connected();

    if (connected && !mqtt_was_connected_) {
        ESP_LOGI(TAG, "MQTT connected, starting stale discovery cleanup");
        start_stale_collection_();
    }

    if (!connected) {
        mqtt_was_connected_ = false;
        cleanup_state_ = CleanupState::IDLE;
        collected_topics_.clear();
        return;
    }

    mqtt_was_connected_ = true;

    if (cleanup_state_ == CleanupState::COLLECTING &&
        millis() - collect_start_ms_ > STALE_COLLECT_DELAY_MS) {
        finish_stale_cleanup_();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ADAPTER CALLBACKS
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::on_device_added(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;
    if (!dev.config.is_enabled()) return;
    // Only publish persisted devices (updated_at set by persist())
    if (dev.config.updated_at == 0) return;

    if (dev.is_cover()) {
        publish_cover_discovery_(dev);
        subscribe_cover_commands_(dev);
    } else if (dev.is_light()) {
        publish_light_discovery_(dev);
        subscribe_light_commands_(dev);
    } else if (dev.is_remote()) {
        publish_remote_discovery_(dev);
    }
    // Initial state publish happens via notify_state_changed_ (called by registry after add)
    publish_gateway_state_();
}

void MqttAdapter::on_device_removed(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;
    remove_all_discovery_(dev);
    publish_gateway_state_();
}

void MqttAdapter::remove_all_discovery_(const Device &dev) {
    uint32_t addr = dev.config.dst_address;

    if (dev.is_cover()) {
        ctx_.unsubscribe(DeviceType::COVER, addr, mqtt_topic::SET);
        ctx_.unsubscribe(DeviceType::COVER, addr, mqtt_topic::SET_POSITION);
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

void MqttAdapter::on_state_changed(const Device &dev, uint16_t changes) {
    if (!ctx_.mqtt->is_connected()) return;
    if (!dev.config.is_enabled()) return;
    if (dev.config.updated_at == 0) return;

    if (dev.is_cover()) {
        publish_cover_state_(dev, changes);
    } else if (dev.is_light()) {
        publish_light_state_(dev, changes);
    } else if (dev.is_remote()) {
        publish_remote_state_(dev);
    }
}

void MqttAdapter::on_config_changed(const Device &dev) {
    if (!ctx_.mqtt->is_connected()) return;

    if (!dev.config.is_enabled()) {
        // Device was disabled — unpublish its discovery
        remove_all_discovery_(dev);
        return;
    }

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
        if (dev.config.open_duration_ms > 0 && dev.config.close_duration_ms > 0) {
            root["set_position_topic"] = ctx_.topic(DeviceType::COVER, addr, mqtt_topic::SET_POSITION);
        }
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
        root["state_class"] = "measurement";
        root["entity_category"] = "diagnostic";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.publish_discovery(ha_discovery::SENSOR, rssi_oid, rssi_payload);

    // Blind state sensor (enum)
    auto state_oid = ctx_.object_id(DeviceType::COVER, addr, "state");
    std::string state_payload = json::build_json([&](JsonObject root) {
        root["name"] = std::string(dev.config.name) + " State";
        root["unique_id"] = state_oid;
        root["state_topic"] = ctx_.topic(DeviceType::COVER, addr, mqtt_topic::DEVICE_STATE);
        root["device_class"] = "enum";
        root["entity_category"] = "diagnostic";
        root["icon"] = "mdi:state-machine";
        auto opts = root["options"].to<JsonArray>();
        for (auto *s : COVER_STATE_OPTIONS) opts.add(s);
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

void MqttAdapter::publish_cover_state_(const Device &dev, uint16_t changes) {
    if (!ctx_.mqtt->is_connected()) return;

    uint32_t addr = dev.config.dst_address;
    const auto &pub = std::get<CoverDevice>(dev.logic).published;

    if (changes & state_change::HA_STATE) {
        ctx_.publish(DeviceType::COVER, addr, mqtt_topic::STATE, pub.ha_state, false);
    }

    if (changes & state_change::POSITION) {
        char pos_buf[8];
        snprintf(pos_buf, sizeof(pos_buf), "%d", pub.position_pct);
        ctx_.publish(DeviceType::COVER, addr, mqtt_topic::POSITION, pos_buf, false);
    }

    if (changes & state_change::RSSI) {
        char rssi_buf[12];
        snprintf(rssi_buf, sizeof(rssi_buf), "%d", pub.rssi_rounded);
        ctx_.publish(DeviceType::COVER, addr, mqtt_topic::RSSI, rssi_buf, false);
    }

    if (changes & state_change::STATE_STRING) {
        ctx_.publish(DeviceType::COVER, addr, mqtt_topic::DEVICE_STATE, pub.state_string, false);
    }

    if (changes & state_change::PROBLEM) {
        ctx_.publish(DeviceType::COVER, addr, mqtt_topic::PROBLEM, pub.is_problem ? ha_state::ON : ha_state::OFF, false);
    }

    if (changes & (state_change::COMMAND_SOURCE | state_change::PROBLEM | state_change::TILT)) {
        std::string attrs = json::build_json([&](JsonObject root) {
            root["command_source"] = pub.command_source;
            root["tilted"] = pub.tilted;
            root["device_class"] = ha_cover_class_str(static_cast<HaCoverClass>(dev.config.ha_device_class));
            root["problem_type"] = pub.problem_type;
        });
        ctx_.publish(DeviceType::COVER, addr, mqtt_topic::ATTRIBUTES, attrs, false);
    }

    if ((changes & state_change::TILT) && dev.config.supports_tilt != 0) {
        ctx_.publish(DeviceType::COVER, addr, mqtt_topic::TILT_STATE, pub.tilted ? "100" : "0", false);
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

            registry_->command_cover(*d, cmd_byte);
        });

    // Position commands (only meaningful when durations are set)
    ctx_.subscribe(DeviceType::COVER, addr, mqtt_topic::SET_POSITION,
        [this, addr](const char *, const char *payload) {
            Device *d = registry_->find(addr, DeviceType::COVER);
            if (d == nullptr) return;

            float target = static_cast<float>(atoi(payload)) / PERCENT_SCALE;
            registry_->set_cover_position(*d, target);
        });

    ctx_.subscribe(DeviceType::COVER, addr, mqtt_topic::TILT,
        [this, addr](const char *, const char *) {
            Device *d = registry_->find(addr, DeviceType::COVER);
            if (d == nullptr) return;

            registry_->command_cover_tilt(*d);
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
        root["state_class"] = "measurement";
        root["entity_category"] = "diagnostic";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });
    ctx_.publish_discovery(ha_discovery::SENSOR, rssi_oid, rssi_payload);

    // Light state sensor (enum)
    auto state_oid = ctx_.object_id(DeviceType::LIGHT, addr, "state");
    std::string state_payload = json::build_json([&](JsonObject root) {
        root["name"] = std::string(dev.config.name) + " State";
        root["unique_id"] = state_oid;
        root["state_topic"] = ctx_.topic(DeviceType::LIGHT, addr, mqtt_topic::DEVICE_STATE);
        root["device_class"] = "enum";
        root["entity_category"] = "diagnostic";
        root["icon"] = "mdi:state-machine";
        auto opts = root["options"].to<JsonArray>();
        for (auto *s : LIGHT_STATE_OPTIONS) opts.add(s);
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

void MqttAdapter::publish_light_state_(const Device &dev, uint16_t changes) {
    if (!ctx_.mqtt->is_connected()) return;

    uint32_t addr = dev.config.dst_address;
    const auto &pub = std::get<LightDevice>(dev.logic).published;

    if (changes & state_change::BRIGHTNESS) {
        std::string payload = json::build_json([&](JsonObject root) {
            root["state"] = pub.is_on ? ha_state::ON : ha_state::OFF;
            if (dev.config.dim_duration_ms > 0) {
                root["brightness"] = pub.brightness_pct;
            }
        });
        ctx_.publish(DeviceType::LIGHT, addr, mqtt_topic::STATE, payload, false);
    }

    if (changes & state_change::RSSI) {
        char rssi_buf[12];
        snprintf(rssi_buf, sizeof(rssi_buf), "%d", pub.rssi_rounded);
        ctx_.publish(DeviceType::LIGHT, addr, mqtt_topic::RSSI, rssi_buf, false);
    }

    if (changes & state_change::STATE_STRING) {
        ctx_.publish(DeviceType::LIGHT, addr, mqtt_topic::DEVICE_STATE, pub.state_string, false);
    }

    if (changes & state_change::PROBLEM) {
        ctx_.publish(DeviceType::LIGHT, addr, mqtt_topic::PROBLEM, pub.is_problem ? ha_state::ON : ha_state::OFF, false);
    }

    if (changes & (state_change::COMMAND_SOURCE | state_change::PROBLEM)) {
        std::string attrs = json::build_json([&](JsonObject root) {
            root["command_source"] = pub.command_source;
            root["problem_type"] = pub.problem_type;
        });
        ctx_.publish(DeviceType::LIGHT, addr, mqtt_topic::ATTRIBUTES, attrs, false);
    }
}

void MqttAdapter::subscribe_light_commands_(const Device &dev) {
    uint32_t addr = dev.config.dst_address;

    ctx_.subscribe(DeviceType::LIGHT, addr, mqtt_topic::SET,
        [this, addr](const char *, const char *payload) {
            Device *d = registry_->find(addr, DeviceType::LIGHT);
            if (d == nullptr) return;

            // Try JSON payload first (HA light schema: {"state":"ON/OFF","brightness":N})
            bool handled = json::parse_json(payload, [&](JsonObject root) -> bool {
                if (root["state"].is<const char *>()) {
                    const char *state = root["state"];
                    if (strcmp(state, ha_state::OFF) == 0) {
                        registry_->command_light(*d, packet::command::DOWN);
                        return true;
                    }
                }
                if (root["brightness"].is<int>()) {
                    float brightness = static_cast<float>(root["brightness"].as<int>()) / PERCENT_SCALE;
                    registry_->set_light_brightness(*d, brightness);
                } else if (root["state"].is<const char *>()) {
                    registry_->command_light(*d, packet::command::UP);
                }
                return true;
            });

            // Fallback: plain string actions ("on", "off", "up", "down", etc.)
            if (!handled) {
                uint8_t cmd_byte = elero_action_to_command(payload);
                if (cmd_byte != packet::command::INVALID) {
                    registry_->command_light(*d, cmd_byte);
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
        root["last_channel"] = remote.last_channel;
        root["last_command"] = hex_str8(remote.last_command);
        root["last_target"] = hex_str(remote.last_target);
    });

    ctx_.publish(DeviceType::REMOTE, addr, mqtt_topic::STATE, payload, false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SHARED HELPERS
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// GATEWAY SENSOR — always published, keeps HA device alive with 0 child entities
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::publish_gateway_discovery_() {
    auto oid = ctx_.device_id + "_gateway";
    auto st = ctx_.topic_prefix + "/gateway/state";

    std::string payload = json::build_json([&](JsonObject root) {
        root["name"] = "Gateway";
        root["unique_id"] = oid;
        root["object_id"] = oid;
        root["state_topic"] = st;
        root["value_template"] = "{{ value_json.active_devices }}";
        root["unit_of_measurement"] = "devices";
        root["icon"] = "mdi:radio-tower";
        root["entity_category"] = "diagnostic";
        root["json_attributes_topic"] = st;
        root["json_attributes_template"] = "{{ value_json | tojson }}";
        ctx_.add_availability(root);
        JsonObject device = root["device"].to<JsonObject>();
        ctx_.add_device_block(device);
    });

    ctx_.mqtt->publish(
        ctx_.discovery_topic(ha_discovery::SENSOR, oid).c_str(),
        payload.c_str(), true);
}

void MqttAdapter::publish_gateway_state_() {
    if (!ctx_.mqtt->is_connected() || registry_ == nullptr) return;

    size_t covers = registry_->count_active(DeviceType::COVER);
    size_t lights = registry_->count_active(DeviceType::LIGHT);
    size_t remotes = registry_->count_active(DeviceType::REMOTE);

    std::string payload = json::build_json([&](JsonObject root) {
        root["active_devices"] = static_cast<int>(covers + lights + remotes);
        root["covers"] = static_cast<int>(covers);
        root["lights"] = static_cast<int>(lights);
        root["remotes"] = static_cast<int>(remotes);
    });

    ctx_.mqtt->publish(
        (ctx_.topic_prefix + "/gateway/state").c_str(),
        payload.c_str(), false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// STALE DISCOVERY CLEANUP
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::start_stale_collection_() {
    collected_topics_.clear();

    // Subscribe to wildcard discovery topics to collect all retained messages
    for (size_t i = 0; i < DISCOVERY_DOMAIN_COUNT; ++i) {
        std::string topic = ctx_.discovery_prefix + "/" + DISCOVERY_DOMAINS[i] + "/+/config";
        ctx_.mqtt->subscribe(topic.c_str(),
            [this](const char *topic, const char *payload) {
                if (cleanup_state_ != CleanupState::COLLECTING) return;
                if (payload == nullptr || payload[0] == '\0') return;

                // Only collect topics that belong to OUR device
                std::string device_id = ctx_.device_id;
                bool is_ours = json::parse_json(payload, [&](JsonObject root) -> bool {
                    JsonObject dev = root["device"];
                    if (dev.isNull()) return false;
                    JsonArray ids = dev["identifiers"];
                    if (ids.isNull() || ids.size() == 0) return false;
                    const char *id = ids[0];
                    return id != nullptr && device_id == id;
                });
                if (is_ours) {
                    collected_topics_.emplace_back(topic);
                }
            });
    }

    cleanup_state_ = CleanupState::COLLECTING;
    collect_start_ms_ = millis();
}

void MqttAdapter::finish_stale_cleanup_() {
    // Unsubscribe from wildcard discovery topics
    for (size_t i = 0; i < DISCOVERY_DOMAIN_COUNT; ++i) {
        std::string topic = ctx_.discovery_prefix + "/" + DISCOVERY_DOMAINS[i] + "/+/config";
        ctx_.mqtt->unsubscribe(topic.c_str());
    }

    // Build set of discovery topics we expect to have
    std::vector<std::string> expected;
    collect_expected_topics_(expected);

    // Remove any collected topic not in the expected set
    size_t removed = 0;
    for (const auto &topic : collected_topics_) {
        bool is_expected = false;
        for (const auto &e : expected) {
            if (topic == e) { is_expected = true; break; }
        }
        if (!is_expected) {
            ESP_LOGI(TAG, "Removing stale discovery: %s", topic.c_str());
            ctx_.mqtt->publish(topic.c_str(), "", true);
            ++removed;
        }
    }

    collected_topics_.clear();
    collected_topics_.shrink_to_fit();

    // Now publish birth + gateway + all active discoveries
    ctx_.publish_birth();
    publish_gateway_discovery_();
    publish_gateway_state_();
    republish_all_();

    cleanup_state_ = CleanupState::DONE;
    ESP_LOGI(TAG, "Stale cleanup complete: removed %zu, published %zu active",
             removed, expected.size());
}

void MqttAdapter::collect_expected_topics_(std::vector<std::string> &out) const {
    // Gateway sensor is always published
    out.push_back(ctx_.discovery_topic(ha_discovery::SENSOR, ctx_.device_id + "_gateway"));

    if (registry_ == nullptr) return;

    registry_->for_each_active([&](const Device &dev) {
        if (!dev.config.is_enabled()) return;
        uint32_t addr = dev.config.dst_address;

        if (dev.is_cover()) {
            out.push_back(ctx_.discovery_topic(ha_discovery::COVER, ctx_.object_id(DeviceType::COVER, addr)));
            out.push_back(ctx_.discovery_topic(ha_discovery::SENSOR, ctx_.object_id(DeviceType::COVER, addr, "rssi")));
            out.push_back(ctx_.discovery_topic(ha_discovery::SENSOR, ctx_.object_id(DeviceType::COVER, addr, "state")));
            out.push_back(ctx_.discovery_topic(ha_discovery::BINARY_SENSOR, ctx_.object_id(DeviceType::COVER, addr, "problem")));
        } else if (dev.is_light()) {
            out.push_back(ctx_.discovery_topic(ha_discovery::LIGHT, ctx_.object_id(DeviceType::LIGHT, addr)));
            out.push_back(ctx_.discovery_topic(ha_discovery::SENSOR, ctx_.object_id(DeviceType::LIGHT, addr, "rssi")));
            out.push_back(ctx_.discovery_topic(ha_discovery::SENSOR, ctx_.object_id(DeviceType::LIGHT, addr, "state")));
            out.push_back(ctx_.discovery_topic(ha_discovery::BINARY_SENSOR, ctx_.object_id(DeviceType::LIGHT, addr, "problem")));
        } else if (dev.is_remote()) {
            out.push_back(ctx_.discovery_topic(ha_discovery::SENSOR, ctx_.object_id(DeviceType::REMOTE, addr)));
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// RECONNECT
// ═══════════════════════════════════════════════════════════════════════════════

void MqttAdapter::republish_all_() {
    if (registry_ == nullptr) return;

    registry_->for_each_active([this](Device &dev) {
        if (!dev.config.is_enabled()) return;

        // Reset Published cache — broker has no state after reconnect
        if (dev.is_cover()) {
            std::get<CoverDevice>(dev.logic).published = {};
            publish_cover_discovery_(dev);
            subscribe_cover_commands_(dev);
        } else if (dev.is_light()) {
            std::get<LightDevice>(dev.logic).published = {};
            publish_light_discovery_(dev);
            subscribe_light_commands_(dev);
        } else if (dev.is_remote()) {
            publish_remote_discovery_(dev);
        }

        // Force-publish all state (ALL flag bypasses diff)
        on_state_changed(dev, state_change::ALL);
    });

    ESP_LOGI(TAG, "Republished all discoveries after reconnect");
}

}  // namespace elero
}  // namespace esphome
