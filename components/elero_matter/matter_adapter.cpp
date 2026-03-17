/// @file matter_adapter.cpp
/// @brief Matter output adapter implementation.
///
/// Creates Matter endpoints for Elero devices and bridges commands/state
/// between the Matter stack and the DeviceRegistry's state machines.
/// Command dispatch uses DeviceRegistry::send_*() — same path as MQTT adapter.

#include "matter_adapter.h"
#include "matter_constants.h"
#include "../elero/device.h"
#include "../elero/device_registry.h"
#include "../elero/cover_sm.h"
#include "../elero/light_sm.h"
#include "esphome/core/log.h"

#include <esp_matter.h>
#include <esp_matter_endpoint.h>

namespace esphome {
namespace elero {

static const char *const TAG = "elero.matter";

// ═══════════════════════════════════════════════════════════════════════════════
// STATIC CALLBACKS (run on Matter/CHIP FreeRTOS task — NOT main loop!)
// ═══════════════════════════════════════════════════════════════════════════════

/// Called by esp-matter when a Matter controller writes an attribute.
/// Runs on the CHIP task — must NOT call registry/state machine directly.
/// Instead, queues a MatterCommand for main loop processing.
static esp_err_t matter_attribute_update_cb(
        esp_matter::attribute::callback_type_t type,
        uint16_t endpoint_id,
        uint32_t cluster_id,
        uint32_t attribute_id,
        esp_matter_attr_val_t *val,
        void *priv_data) {

    if (type != esp_matter::attribute::PRE_UPDATE) {
        return ESP_OK;
    }

    auto *adapter = static_cast<MatterAdapter *>(priv_data);

    MatterCommand cmd{};
    cmd.endpoint_id = endpoint_id;
    cmd.cluster_id = cluster_id;
    cmd.attribute_id = attribute_id;

    if (cluster_id == matter::CLUSTER_WINDOW_COVERING &&
        attribute_id == matter::ATTR_WC_TARGET_POS_LIFT) {
        cmd.value = val->val.u16;
    } else if (cluster_id == matter::CLUSTER_WINDOW_COVERING &&
               attribute_id == matter::ATTR_WC_OPERATIONAL_STATUS) {
        // StopMotion command sets OperationalStatus to 0
        cmd.value = val->val.u8;
    } else if (cluster_id == matter::CLUSTER_ON_OFF &&
               attribute_id == matter::ATTR_ONOFF) {
        cmd.value = val->val.b ? 1 : 0;
    } else if (cluster_id == matter::CLUSTER_LEVEL_CONTROL &&
               attribute_id == matter::ATTR_CURRENT_LEVEL) {
        // Clamp level 255 (null/reserved) to 254
        uint8_t level = val->val.u8;
        cmd.value = std::min(level, matter::LEVEL_MAX);
    } else {
        return ESP_OK;  // Attribute we don't handle
    }

    adapter->queue_command(cmd);
    return ESP_OK;
}

/// Called by esp-matter for Identify cluster effects.
static esp_err_t matter_identify_cb(
        esp_matter::identification::callback_type_t type,
        uint16_t endpoint_id,
        uint8_t effect_id,
        uint8_t effect_variant,
        void *priv_data) {
    ESP_LOGI(TAG, "Identify: endpoint=%u effect=%u", endpoint_id, effect_id);
    return ESP_OK;
}

/// Called by esp-matter for CHIP device events (commissioning, fabric, etc.).
static void matter_event_cb(const ChipDeviceEvent *event, intptr_t arg) {
    if (event == nullptr) return;

    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
            ESP_LOGI(TAG, "Matter commissioning complete");
            break;
        case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
            ESP_LOGI(TAG, "Matter fabric committed");
            break;
        default:
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════════════════════

void MatterAdapter::setup(DeviceRegistry &registry) {
    registry_ = &registry;

    // Create Matter node (root endpoint 0) with default config.
    // VID/PID/discriminator/passcode are set via sdkconfig in __init__.py.
    esp_matter::node::config_t node_config;

    auto *node = esp_matter::node::create(
        &node_config, matter_attribute_update_cb, matter_identify_cb, this);

    if (node == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return;
    }
    node_ = node;

    // Don't call esp_matter::start() here — endpoints from restore_all()
    // are created via on_device_added() after setup returns. Some esp-matter
    // versions require endpoints to exist before start(). We defer start()
    // to the first loop() iteration.

    // Publish commissioning info to registry for web UI display
    if (!qr_code_.empty()) {
        registry_->set_commissioning_info(qr_code_, manual_code_);
        ESP_LOGI(TAG, "QR Code: %s", qr_code_.c_str());
        ESP_LOGI(TAG, "Manual code: %s", manual_code_.c_str());
    }

    ESP_LOGI(TAG, "Matter node created (VID=0x%04X PID=0x%04X disc=%u)",
             vendor_id_, product_id_, discriminator_);
}

void MatterAdapter::loop() {
    // Deferred start: all endpoints from restore_all() are created by now
    if (node_ != nullptr && !started_) {
        esp_err_t err = esp_matter::start(matter_event_cb);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Matter stack: %s",
                     esp_err_to_name(err));
            node_ = nullptr;
            return;
        }
        started_ = true;
        ESP_LOGI(TAG, "Matter stack started");
    }

    process_commands_();
}

// ═══════════════════════════════════════════════════════════════════════════════
// ADAPTER CALLBACKS
// ═══════════════════════════════════════════════════════════════════════════════

void MatterAdapter::on_device_added(const Device &dev) {
    if (node_ == nullptr) return;

    if (dev.is_cover()) {
        create_cover_endpoint_(dev);
    } else if (dev.is_light()) {
        create_light_endpoint_(dev);
    }
}

void MatterAdapter::on_device_removed(const Device &dev) {
    destroy_endpoint_(dev.config.dst_address, dev.type());
}

void MatterAdapter::on_state_changed(const Device &dev) {
    if (dev.is_cover()) {
        update_cover_attributes_(dev);
    } else if (dev.is_light()) {
        update_light_attributes_(dev);
    }
}

void MatterAdapter::on_config_changed(const Device &dev) {
    if (dev.is_cover()) {
        update_cover_attributes_(dev);
    } else if (dev.is_light()) {
        // Brightness capability may have changed (on/off ↔ dimmable) — recreate
        destroy_endpoint_(dev.config.dst_address, DeviceType::LIGHT);
        create_light_endpoint_(dev);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ENDPOINT CREATION
// ═══════════════════════════════════════════════════════════════════════════════

void MatterAdapter::create_cover_endpoint_(const Device &dev) {
    auto *node = static_cast<esp_matter::node_t *>(node_);
    uint32_t addr = dev.config.dst_address;
    uint64_t key = device_key_(addr, DeviceType::COVER);

    if (device_to_ep_.count(key) > 0) return;

    esp_matter::endpoint::window_covering::config_t wc_config;
    auto *ep = esp_matter::endpoint::window_covering::create(
        node, &wc_config, ENDPOINT_FLAG_NONE, nullptr);

    if (ep == nullptr) {
        ESP_LOGE(TAG, "Failed to create Window Covering endpoint for 0x%06x",
                 addr);
        return;
    }

    uint16_t ep_id = esp_matter::endpoint::get_id(ep);
    ep_to_device_[ep_id] = {addr, DeviceType::COVER};
    device_to_ep_[key] = ep_id;

    update_cover_attributes_(dev);

    ESP_LOGI(TAG, "Created Window Covering endpoint %u for 0x%06x",
             ep_id, addr);
}

void MatterAdapter::create_light_endpoint_(const Device &dev) {
    auto *node = static_cast<esp_matter::node_t *>(node_);
    uint32_t addr = dev.config.dst_address;
    uint64_t key = device_key_(addr, DeviceType::LIGHT);

    if (device_to_ep_.count(key) > 0) return;

    bool has_brightness = dev.config.dim_duration_ms > 0;
    esp_matter::endpoint_t *ep = nullptr;

    if (has_brightness) {
        esp_matter::endpoint::dimmable_light::config_t config;
        ep = esp_matter::endpoint::dimmable_light::create(
            node, &config, ENDPOINT_FLAG_NONE, nullptr);
    } else {
        esp_matter::endpoint::on_off_light::config_t config;
        ep = esp_matter::endpoint::on_off_light::create(
            node, &config, ENDPOINT_FLAG_NONE, nullptr);
    }

    if (ep == nullptr) {
        ESP_LOGE(TAG, "Failed to create light endpoint for 0x%06x", addr);
        return;
    }

    uint16_t ep_id = esp_matter::endpoint::get_id(ep);
    ep_to_device_[ep_id] = {addr, DeviceType::LIGHT};
    device_to_ep_[key] = ep_id;

    update_light_attributes_(dev);

    ESP_LOGI(TAG, "Created %s endpoint %u for 0x%06x",
             has_brightness ? "Dimmable Light" : "On/Off Light",
             ep_id, addr);
}

void MatterAdapter::destroy_endpoint_(uint32_t address, DeviceType type) {
    auto *node = static_cast<esp_matter::node_t *>(node_);
    if (node == nullptr) return;

    uint64_t key = device_key_(address, type);
    auto it = device_to_ep_.find(key);
    if (it == device_to_ep_.end()) return;

    uint16_t ep_id = it->second;

    auto *ep = esp_matter::endpoint::get(node, ep_id);
    if (ep != nullptr) {
        esp_matter::endpoint::destroy(node, ep);
    }

    ep_to_device_.erase(ep_id);
    device_to_ep_.erase(it);

    ESP_LOGI(TAG, "Destroyed endpoint %u for 0x%06x", ep_id, address);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ATTRIBUTE UPDATES (ESPHome → Matter)
// ═══════════════════════════════════════════════════════════════════════════════

void MatterAdapter::update_cover_attributes_(const Device &dev) {
    uint64_t key = device_key_(dev.config.dst_address, DeviceType::COVER);
    auto it = device_to_ep_.find(key);
    if (it == device_to_ep_.end()) return;

    uint16_t ep_id = it->second;
    const auto &cover = std::get<CoverDevice>(dev.logic);
    auto ctx = cover_context(dev.config);
    uint32_t now = millis();

    float pos = cover_sm::position(cover.state, now, ctx);
    uint16_t matter_pos = matter::elero_to_matter_position(pos);
    esp_matter_attr_val_t pos_val = esp_matter_nullable_uint16(matter_pos);
    esp_matter::attribute::update(
        ep_id, matter::CLUSTER_WINDOW_COVERING,
        matter::ATTR_WC_CURRENT_POS_LIFT, &pos_val);

    auto op = cover_sm::operation(cover.state);
    uint8_t op_status = matter::operation_to_matter_status(op);
    esp_matter_attr_val_t op_val = esp_matter_uint8(op_status);
    esp_matter::attribute::update(
        ep_id, matter::CLUSTER_WINDOW_COVERING,
        matter::ATTR_WC_OPERATIONAL_STATUS, &op_val);
}

void MatterAdapter::update_light_attributes_(const Device &dev) {
    uint64_t key = device_key_(dev.config.dst_address, DeviceType::LIGHT);
    auto it = device_to_ep_.find(key);
    if (it == device_to_ep_.end()) return;

    uint16_t ep_id = it->second;
    const auto &light = std::get<LightDevice>(dev.logic);
    auto ctx = light_context(dev.config);
    uint32_t now = millis();

    bool on = light_sm::is_on(light.state);
    esp_matter_attr_val_t on_val = esp_matter_bool(on);
    esp_matter::attribute::update(
        ep_id, matter::CLUSTER_ON_OFF, matter::ATTR_ONOFF, &on_val);

    if (dev.config.dim_duration_ms > 0) {
        float brightness = light_sm::brightness(light.state, now, ctx);
        uint8_t level = matter::elero_to_matter_level(brightness);
        esp_matter_attr_val_t level_val = esp_matter_nullable_uint8(level);
        esp_matter::attribute::update(
            ep_id, matter::CLUSTER_LEVEL_CONTROL,
            matter::ATTR_CURRENT_LEVEL, &level_val);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// COMMAND QUEUE (Matter task → ESPHome main loop)
// ═══════════════════════════════════════════════════════════════════════════════

void MatterAdapter::queue_command(const MatterCommand &cmd) {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    if (cmd_queue_.size() >= MATTER_CMD_QUEUE_MAX) return;
    cmd_queue_.push(cmd);
}

void MatterAdapter::process_commands_() {
    std::queue<MatterCommand> local_queue;
    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        std::swap(local_queue, cmd_queue_);
    }

    while (!local_queue.empty()) {
        auto cmd = local_queue.front();
        local_queue.pop();

        auto ep_it = ep_to_device_.find(cmd.endpoint_id);
        if (ep_it == ep_to_device_.end()) {
            ESP_LOGW(TAG, "Command for unknown endpoint %u", cmd.endpoint_id);
            continue;
        }

        auto [addr, type] = ep_it->second;
        Device *dev = registry_->find(addr, type);
        if (dev == nullptr) {
            ESP_LOGW(TAG, "Device 0x%06x not found in registry", addr);
            continue;
        }

        if (type == DeviceType::COVER) {
            handle_cover_command_(dev, cmd);
        } else if (type == DeviceType::LIGHT) {
            handle_light_command_(dev, cmd);
        }
    }
}

void MatterAdapter::handle_cover_command_(Device *dev,
                                          const MatterCommand &cmd) {
    if (cmd.cluster_id != matter::CLUSTER_WINDOW_COVERING) return;

    // StopMotion: OperationalStatus written to 0 (all stopped)
    if (cmd.attribute_id == matter::ATTR_WC_OPERATIONAL_STATUS) {
        if (cmd.value == 0) {
            registry_->send_cover_command(*dev, packet::command::STOP);
            ESP_LOGD(TAG, "Cover 0x%06x: stop", dev->config.dst_address);
        }
        return;
    }

    if (cmd.attribute_id != matter::ATTR_WC_TARGET_POS_LIFT) return;

    auto ctx = cover_context(dev->config);
    uint32_t now = millis();

    float target = matter::matter_to_elero_position(
        static_cast<uint16_t>(cmd.value));
    float current_pos = cover_sm::position(
        std::get<CoverDevice>(dev->logic).state, now, ctx);

    uint8_t cmd_byte = matter::target_position_to_command(target, current_pos);
    if (cmd_byte == packet::command::INVALID) return;  // Already at target

    registry_->send_cover_command(*dev, cmd_byte);

    ESP_LOGD(TAG, "Cover 0x%06x: target=%.2f cmd=0x%02x",
             dev->config.dst_address, target, cmd_byte);
}

void MatterAdapter::handle_light_command_(Device *dev,
                                          const MatterCommand &cmd) {
    if (cmd.cluster_id == matter::CLUSTER_ON_OFF &&
        cmd.attribute_id == matter::ATTR_ONOFF) {
        if (cmd.value) {
            registry_->send_light_on(*dev);
        } else {
            registry_->send_light_off(*dev);
        }
    } else if (cmd.cluster_id == matter::CLUSTER_LEVEL_CONTROL &&
               cmd.attribute_id == matter::ATTR_CURRENT_LEVEL) {
        float brightness = matter::matter_to_elero_brightness(
            static_cast<uint8_t>(cmd.value));
        registry_->send_light_brightness(*dev, brightness);
    }
}

}  // namespace elero
}  // namespace esphome
