/// @file device_registry.cpp
/// @brief DeviceRegistry implementation — CRUD, NVS, RF dispatch, loop, observer notification.

#include "device_registry.h"
#include "elero.h"
#include "overloaded.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"

namespace esphome::elero {

static const char *const TAG = "elero.registry";

// ═════════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═════════════════════════════════════════════════════════════════════════════

void DeviceRegistry::init_preferences() {
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        prefs_[i] = global_preferences->make_preference<NvsDeviceConfig>(
            fnv1_hash("elero_device") + i);
    }
    prefs_initialized_ = true;
}

void DeviceRegistry::restore_all() {
    if (!prefs_initialized_) {
        init_preferences();
    }

    size_t restored = 0;
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        NvsDeviceConfig cfg{};
        if (prefs_[i].load(&cfg) && cfg.is_valid()) {
            init_device(slots_[i], cfg);
            ++restored;
            ESP_LOGI(TAG, "Restored %s '%s' at 0x%06x (slot %zu)",
                     device_type_str(cfg.type), cfg.name,
                     cfg.dst_address, i);
        }
    }

    // Notify adapters of all restored devices
    for (auto &dev : slots_) {
        if (dev.active) {
            notify_added_(dev);
        }
    }

    assign_poll_stagger_();

    ESP_LOGI(TAG, "Restored %zu devices from NVS (%zu covers, %zu lights, %zu remotes)",
             restored,
             count_active(DeviceType::COVER),
             count_active(DeviceType::LIGHT),
             count_active(DeviceType::REMOTE));
}

void DeviceRegistry::add_adapter(OutputAdapter *adapter) {
    adapters_.push_back(adapter);
}

void DeviceRegistry::setup_adapters() {
    for (auto *a : adapters_) {
        a->setup(*this);
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// CRUD
// ═════════════════════════════════════════════════════════════════════════════

Device *DeviceRegistry::register_device(const NvsDeviceConfig &config) {
    Device *existing = find(config.dst_address, config.type);
    if (existing) {
        update_device_config(*existing, config);
        notify_config_changed_(*existing);
        return existing;
    }

    Device *slot = find_free_slot_();
    if (!slot) {
        ESP_LOGE(TAG, "No free slot for %s at 0x%06x",
                 device_type_str(config.type), config.dst_address);
        return nullptr;
    }

    init_device(*slot, config);
    if (config.type == DeviceType::COVER) assign_poll_stagger_();
    notify_added_(*slot);
    ESP_LOGI(TAG, "Registered %s '%s' at 0x%06x (slot %zu)",
             device_type_str(config.type), config.name,
             config.dst_address, slot_index_(*slot));
    return slot;
}

Device *DeviceRegistry::upsert(const NvsDeviceConfig &config) {
    // Try to find existing device with same address+type
    Device *existing = find(config.dst_address, config.type);
    if (existing) {
        update_device_config(*existing, config);
        persist(*existing);
        notify_config_changed_(*existing);
        ESP_LOGI(TAG, "Updated %s '%s' at 0x%06x",
                 device_type_str(config.type), config.name, config.dst_address);
        return existing;
    }

    // Find a free slot
    Device *slot = find_free_slot_();
    if (!slot) {
        ESP_LOGE(TAG, "No free slot for %s at 0x%06x",
                 device_type_str(config.type), config.dst_address);
        return nullptr;
    }

    init_device(*slot, config);
    if (config.type == DeviceType::COVER) assign_poll_stagger_();
    persist(*slot);
    notify_added_(*slot);
    ESP_LOGI(TAG, "Added %s '%s' at 0x%06x (slot %zu)",
             device_type_str(config.type), config.name,
             config.dst_address, slot_index_(*slot));
    return slot;
}

bool DeviceRegistry::remove(uint32_t address, DeviceType type) {
    Device *dev = find(address, type);
    if (!dev) return false;

    ESP_LOGI(TAG, "Removing %s at 0x%06x", device_type_str(type), address);
    notify_removed_(*dev);

    // Clear NVS (only when persistence is enabled)
    if (nvs_enabled_ && prefs_initialized_) {
        size_t idx = slot_index_(*dev);
        NvsDeviceConfig empty{};
        empty.version = 0;  // Mark as invalid
        prefs_[idx].save(&empty);
    }

    deactivate_device(*dev);
    return true;
}

Device *DeviceRegistry::find(uint32_t address, DeviceType type) {
    for (auto &dev : slots_) {
        if (dev.active && dev.config.dst_address == address && dev.config.type == type) {
            return &dev;
        }
    }
    return nullptr;
}

Device *DeviceRegistry::find(uint32_t address) {
    for (auto &dev : slots_) {
        if (dev.active && dev.config.dst_address == address) {
            return &dev;
        }
    }
    return nullptr;
}

// ═════════════════════════════════════════════════════════════════════════════
// RF DISPATCH
// ═════════════════════════════════════════════════════════════════════════════

void DeviceRegistry::on_rf_packet(const RfPacketInfo &pkt, uint32_t now) {
    // Notify all adapters of raw RF packet (web UI needs this)
    notify_rf_packet_(pkt);

    if (packet::is_status_packet(pkt.type)) {
        // Status packets: src is the blind/light reporting status
        Device *dev = find(pkt.src);
        if (dev && dev->active) {
            dev->rf.last_seen_ms = now;
            dev->rf.last_rssi = pkt.rssi;
            dev->rf.last_state_raw = pkt.state;
            dispatch_status_(*dev, pkt.state, now);
        }
    } else if (packet::is_command_packet(pkt.type) && !pkt.echo) {
        // Remote commands are passive — we only auto-discover the remote.
        // The blind's status response (via dispatch_status_) handles state.
        track_remote_(pkt, now);
    }
}

void DeviceRegistry::dispatch_status_(Device &dev, uint8_t state_byte, uint32_t now) {
    bool changed = false;

    std::visit(overloaded{
        [&](CoverDevice &cover) {
            auto ctx = cover_context(dev.config);
            auto old_idx = cover.state.index();
            cover.state = cover_sm::on_rf_status(cover.state, state_byte, now, ctx);
            cover.poll.on_rf_received(now);

            // Track tilt state from RF
            bool was_tilted = cover.tilted;
            if (state_byte == packet::state::TILT ||
                state_byte == packet::state::TOP_TILT ||
                state_byte == packet::state::BOTTOM_TILT) {
                cover.tilted = true;
            } else if (state_byte == packet::state::TOP ||
                       state_byte == packet::state::BOTTOM ||
                       state_byte == packet::state::MOVING_UP ||
                       state_byte == packet::state::MOVING_DOWN ||
                       state_byte == packet::state::START_MOVING_UP ||
                       state_byte == packet::state::START_MOVING_DOWN) {
                cover.tilted = false;
            }

            changed = (cover.state.index() != old_idx) || (cover.tilted != was_tilted);
        },
        [&](LightDevice &light) {
            auto ctx = light_context(dev.config);
            auto old_idx = light.state.index();
            light.state = light_sm::on_rf_status(light.state, state_byte, now, ctx);
            changed = (light.state.index() != old_idx);
        },
        [](RemoteDevice &) {},
    }, dev.logic);

    if (changed) {
        notify_state_changed_(dev);
        dev.last_notify_ms = now;  // Prevent duplicate publish from position throttle
    }
}


void DeviceRegistry::track_remote_(const RfPacketInfo &pkt, uint32_t now) {
    // Native mode doesn't auto-discover remotes — devices are YAML-defined
    if (!nvs_enabled_) return;

    // Check if we already track this remote (active, any enabled state)
    Device *existing = find(pkt.src, DeviceType::REMOTE);
    if (existing) {
        existing->rf.last_seen_ms = now;
        existing->rf.last_rssi = pkt.rssi;
        auto &remote = std::get<RemoteDevice>(existing->logic);
        remote.last_command = pkt.command;
        remote.last_target = pkt.dst;
        remote.last_channel = pkt.channel;
        // Only broadcast state for enabled remotes (disabled = unpublished)
        if (existing->config.is_enabled()) {
            notify_state_changed_(*existing);
        }
        return;
    }

    // Auto-discover new remote
    NvsDeviceConfig cfg{};
    cfg.type = DeviceType::REMOTE;
    cfg.dst_address = pkt.src;
    cfg.channel = pkt.channel;
    snprintf(cfg.name, NVS_NAME_MAX, DEFAULT_REMOTE_NAME_FMT, pkt.src);

    Device *slot = find_free_slot_();
    if (!slot) {
        ESP_LOGW(TAG, "No free slot for remote 0x%06x", pkt.src);
        return;
    }

    init_device(*slot, cfg);
    auto &remote = std::get<RemoteDevice>(slot->logic);
    remote.last_command = pkt.command;
    remote.last_target = pkt.dst;
    remote.last_channel = pkt.channel;
    slot->rf.last_seen_ms = now;
    slot->rf.last_rssi = pkt.rssi;
    persist(*slot);
    notify_added_(*slot);
    ESP_LOGI(TAG, "Discovered remote 0x%06x (slot %zu)", pkt.src, slot_index_(*slot));
}

// ═════════════════════════════════════════════════════════════════════════════
// LOOP
// ═════════════════════════════════════════════════════════════════════════════

void DeviceRegistry::loop(uint32_t now) {
    for (auto &dev : slots_) {
        if (!dev.active || !dev.config.is_enabled()) continue;

        std::visit(overloaded{
            [&](CoverDevice &cover) { loop_cover_(dev, cover, now); },
            [&](LightDevice &light) { loop_light_(dev, light, now); },
            [](RemoteDevice &) {},
        }, dev.logic);
    }

    // Drive adapter loops (MQTT reconnect, etc.)
    for (auto *a : adapters_) {
        a->loop();
    }
}

void DeviceRegistry::loop_cover_(Device &dev, CoverDevice &cover, uint32_t now) {
    auto ctx = cover_context(dev.config);

    // 1. Tick — check movement timeout and post-stop cooldown
    auto old_idx = cover.state.index();
    cover.state = cover_sm::on_tick(cover.state, now, ctx);
    bool state_type_changed = (cover.state.index() != old_idx);

    // 2. Poll if due
    bool moving = cover_sm::is_moving(cover.state);
    if (cover.poll.should_poll(now, moving)) {
        // Single packet for movement polls (blind responds to each packet,
        // if missed we retry in 2s). Full 3 packets for idle polls (less frequent).
        uint8_t packets = moving ? 1 : packet::button::PACKETS;
        (void) dev.sender.enqueue(packet::command::CHECK, packets, packet::msg_type::COMMAND);
        cover.poll.on_poll_sent(now);
    }

    // 3. Intermediate position stop — if cover has position tracking and
    //    has reached its target position, stop it.
    if (moving && cover_sm::has_position_tracking(ctx) && cover.target_position >= cover_sm::POSITION_CLOSED) {
        float pos = cover_sm::position(cover.state, now, ctx);
        bool at_target = false;
        if (std::holds_alternative<cover_sm::Opening>(cover.state)) {
            at_target = pos >= cover.target_position;
        } else if (std::holds_alternative<cover_sm::Closing>(cover.state)) {
            at_target = pos <= cover.target_position;
        }
        // Don't send stop for fully open/closed — the blind handles those endpoints
        if (at_target && cover.target_position > cover_sm::POSITION_CLOSED && cover.target_position < cover_sm::POSITION_OPEN) {
            dev.sender.clear_queue();
            (void) dev.sender.enqueue(packet::command::STOP, packet::button::PACKETS, packet::msg_type::COMMAND);
            (void) dev.sender.enqueue(packet::command::CHECK, packet::button::PACKETS, packet::msg_type::COMMAND);
            cover.state = cover_sm::on_command(cover.state, packet::command::STOP, now, ctx);
            state_type_changed = true;
            cover.target_position = cover_sm::NO_TARGET;  // Clear target
        }
    }

    // 4. Process command queue
    if (hub_) {
        dev.sender.process_queue(now, hub_, "elero.cover");
    }

    // 5. Notify state changes
    if (state_type_changed) {
        notify_state_changed_(dev);
        dev.last_notify_ms = now;
    } else if (moving &&
               (now - dev.last_notify_ms) >= packet::timing::PUBLISH_THROTTLE_MS) {
        // Throttled position updates during movement
        notify_state_changed_(dev);
        dev.last_notify_ms = now;
    }
}

void DeviceRegistry::loop_light_(Device &dev, LightDevice &light, uint32_t now) {
    auto ctx = light_context(dev.config);

    // 1. Tick — check dimming completion
    auto old_idx = light.state.index();
    light.state = light_sm::on_tick(light.state, now, ctx);
    bool state_type_changed = (light.state.index() != old_idx);

    // 2. Send RELEASE when dimming completes (freeze brightness on the receiver).
    //    Button packets use RELEASE (0x00) instead of STOP (0x10).
    if (state_type_changed && !light_sm::is_dimming(light.state) &&
        light_sm::is_on(light.state)) {
        (void) dev.sender.enqueue(packet::button::RELEASE, packet::button::PACKETS);
    }

    // 3. Process command queue
    if (hub_) {
        dev.sender.process_queue(now, hub_, "elero.light");
    }

    // 4. Notify state changes
    if (state_type_changed) {
        notify_state_changed_(dev);
        dev.last_notify_ms = now;
    } else if (light_sm::is_dimming(light.state) &&
               (now - dev.last_notify_ms) >= packet::timing::PUBLISH_THROTTLE_MS) {
        notify_state_changed_(dev);
        dev.last_notify_ms = now;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ITERATION
// ═════════════════════════════════════════════════════════════════════════════

size_t DeviceRegistry::count_active() const {
    size_t n = 0;
    for (const auto &dev : slots_) {
        if (dev.active) ++n;
    }
    return n;
}

size_t DeviceRegistry::count_active(DeviceType type) const {
    size_t n = 0;
    for (const auto &dev : slots_) {
        if (dev.active && dev.config.type == type) ++n;
    }
    return n;
}

// ═════════════════════════════════════════════════════════════════════════════
// PERSISTENCE
// ═════════════════════════════════════════════════════════════════════════════

void DeviceRegistry::persist(Device &dev, size_t slot_idx) {
    if (!prefs_initialized_ || slot_idx >= MAX_DEVICES) return;
    dev.config.updated_at = millis();
    prefs_[slot_idx].save(&dev.config);
}

void DeviceRegistry::persist(Device &dev) {
    persist(dev, slot_index_(dev));
}

// ═════════════════════════════════════════════════════════════════════════════
// INTERNAL HELPERS
// ═════════════════════════════════════════════════════════════════════════════

Device *DeviceRegistry::find_free_slot_() {
    for (auto &dev : slots_) {
        if (!dev.active) return &dev;
    }
    return nullptr;
}

size_t DeviceRegistry::slot_index_(const Device &dev) const {
    return static_cast<size_t>(&dev - slots_.data());
}

void DeviceRegistry::notify_added_(const Device &dev) {
    for (auto *a : adapters_) a->on_device_added(dev);
}

void DeviceRegistry::notify_removed_(const Device &dev) {
    for (auto *a : adapters_) a->on_device_removed(dev);
}

void DeviceRegistry::notify_state_changed_(const Device &dev) {
    for (auto *a : adapters_) a->on_state_changed(dev);
}

void DeviceRegistry::notify_config_changed_(const Device &dev) {
    for (auto *a : adapters_) a->on_config_changed(dev);
}

void DeviceRegistry::notify_rf_packet_(const RfPacketInfo &pkt) {
    for (auto *a : adapters_) a->on_rf_packet(pkt);
}

void DeviceRegistry::assign_poll_stagger_() {
    uint32_t cover_idx = 0;
    for (auto &dev : slots_) {
        if (!dev.active || !dev.is_cover()) continue;
        auto &cover = std::get<CoverDevice>(dev.logic);
        cover.poll.offset_ms = cover_idx * packet::timing::POLL_OFFSET_SPACING;
        ++cover_idx;
    }
}

}  // namespace esphome::elero
