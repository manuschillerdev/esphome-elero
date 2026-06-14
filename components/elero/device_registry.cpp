/// @file device_registry.cpp
/// @brief DeviceRegistry implementation — CRUD, NVS, RF dispatch, loop, observer notification.

#include "device_registry.h"
#include "state_snapshot.h"
#include "elero.h"
#include "overloaded.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include <cstring>

namespace esphome::elero {

static const char *const TAG = "elero.registry";

bool DeviceRegistry::enqueue_or_warn_(Device &dev, uint8_t cmd_byte,
                                      uint8_t packets, uint8_t type,
                                      const char *context) {
    if (dev.sender.enqueue(cmd_byte, packets, type)) {
        return true;
    }

    ESP_LOGW(TAG, "0x%06x: failed to enqueue %s cmd=0x%02x",
             dev.config.dst_address, context, cmd_byte);
    return false;
}

bool DeviceRegistry::enqueue_check_(Device &dev, const char *context) {
    return enqueue_or_warn_(dev, packet::command::CHECK,
                            packet::limits::CHECK_PACKETS,
                            packet::msg_type::COMMAND, context);
}

// ═════════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═════════════════════════════════════════════════════════════════════════════

void DeviceRegistry::init_preferences() {
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        prefs_[i] = global_preferences->make_preference<NvsDeviceConfig>(
            fnv1_hash("elero_device") + i);
    }
    for (size_t i = 0; i < MAX_GROUPS; ++i) {
        group_prefs_[i] = global_preferences->make_preference<NvsGroupConfig>(
            fnv1_hash(nvs_pref_key::GROUP) + i);
    }
    hub_prefs_ = global_preferences->make_preference<NvsHubConfig>(
        fnv1_hash(nvs_pref_key::HUB));
    NvsHubConfig hub_cfg{};
    if (hub_prefs_.load(&hub_cfg) && hub_cfg.is_valid()) {
        hub_name_override_ = hub_cfg.name;
    }
    update_hub_display_name_();
    prefs_initialized_ = true;
}

void DeviceRegistry::set_default_hub_name(const std::string &name) {
    hub_default_name_ = name;
    update_hub_display_name_();
}

bool DeviceRegistry::set_hub_name_override(const std::string &name) {
    std::string trimmed = name;
    // Cap to NVS field length (minus null terminator)
    if (trimmed.size() >= NVS_HUB_NAME_MAX) {
        trimmed.resize(NVS_HUB_NAME_MAX - 1);
    }
    if (trimmed == hub_name_override_) return false;
    hub_name_override_ = trimmed;
    if (prefs_initialized_) {
        NvsHubConfig hub_cfg{};
        hub_cfg.set_name(hub_name_override_.c_str());
        if (!hub_prefs_.save(&hub_cfg)) {
            ESP_LOGW(TAG, "Failed to persist hub name to NVS");
        }
    }
    update_hub_display_name_();
    ESP_LOGI(TAG, "Hub display name set to '%s'", hub_display_name_.c_str());
    for (auto *a : adapters_) {
        a->on_hub_config_changed();
    }
    return true;
}

void DeviceRegistry::update_hub_display_name_() {
    hub_display_name_ = hub_name_override_.empty() ? hub_default_name_
                                                   : hub_name_override_;
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

    size_t restored_groups = 0;
    for (size_t i = 0; i < MAX_GROUPS; ++i) {
        NvsGroupConfig group{};
        if (group_prefs_[i].load(&group) && group.is_valid()) {
            std::string error;
            if (!validate_group_(group, &error)) {
                ESP_LOGW(TAG, "Skipping invalid group '%s': %s", group.id, error.c_str());
                continue;
            }
            groups_[i] = group;
            ++restored_groups;
            ESP_LOGI(TAG, "Restored group '%s' (%s, %u members)",
                     group.name, group.id, group.member_count);
        }
    }

    // Notify adapters (discovery configs, subscriptions) and queue one CHECK
    // per cover to get real state from each blind. STATUS responses drive
    // state through the normal dispatch_status_ → notify_state_changed_ pipeline.
    uint32_t now = millis();
    uint32_t cover_idx = 0;
    for (auto &dev : slots_) {
        if (dev.active) {
            notify_added_(dev);
            if (dev.is_cover()) {
                bool queued = enqueue_check_(dev, "restore_all");
                auto &cover = std::get<CoverDevice>(dev.logic);
                cover.poll.offset_ms = cover_idx * packet::timing::POLL_OFFSET_SPACING;
                if (queued) {
                    cover.poll.on_poll_sent(now);
                }
                ++cover_idx;
            }
        }
    }

    ESP_LOGI(TAG, "Restored %zu devices and %zu groups from NVS (%zu covers, %zu lights, %zu remotes)",
             restored, restored_groups,
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
    notify_state_changed_(*slot, millis());
    ESP_LOGI(TAG, "Added %s '%s' at 0x%06x (slot %zu)",
             device_type_str(config.type), config.name,
             config.dst_address, slot_index(*slot));
    return slot;
}

bool DeviceRegistry::remove(uint32_t address, DeviceType type) {
    Device *dev = find(address, type);
    if (!dev) return false;

    ESP_LOGI(TAG, "Removing %s at 0x%06x", device_type_str(type), address);
    notify_removed_(*dev);

    // Clear NVS (only when persistence is enabled)
    if (nvs_enabled_ && prefs_initialized_) {
        size_t idx = slot_index(*dev);
        NvsDeviceConfig empty{};
        empty.version = 0;  // Mark as invalid
        prefs_[idx].save(&empty);
    }

    deactivate_device(*dev);
    prune_device_from_groups_(address);
    return true;
}

NvsGroupConfig *DeviceRegistry::upsert_group(const NvsGroupConfig &config, std::string *error) {
    if (!validate_group_(config, error)) return nullptr;

    NvsGroupConfig *existing = find_group(config.id);
    if (existing != nullptr) {
        *existing = config;
        persist_group_(*existing, static_cast<size_t>(existing - groups_.data()));
        notify_group_upserted_(*existing);
        ESP_LOGI(TAG, "Updated group '%s' (%s, %u members)",
                 existing->name, existing->id, existing->member_count);
        return existing;
    }

    NvsGroupConfig *slot = find_free_group_slot_();
    if (slot == nullptr) {
        if (error != nullptr) *error = "No free group slot";
        ESP_LOGE(TAG, "No free slot for group '%s'", config.id);
        return nullptr;
    }

    *slot = config;
    persist_group_(*slot, static_cast<size_t>(slot - groups_.data()));
    notify_group_upserted_(*slot);
    ESP_LOGI(TAG, "Added group '%s' (%s, %u members)",
             slot->name, slot->id, slot->member_count);
    return slot;
}

bool DeviceRegistry::remove_group(const char *id) {
    NvsGroupConfig *group = find_group(id);
    if (group == nullptr) return false;

    char removed_id[NVS_GROUP_ID_MAX]{};
    strncpy(removed_id, group->id, NVS_GROUP_ID_MAX - 1);
    ESP_LOGI(TAG, "Removing group '%s'", removed_id);
    notify_group_removed_(removed_id);
    clear_group_slot_(*group);
    return true;
}

NvsGroupConfig *DeviceRegistry::find_group(const char *id) {
    if (id == nullptr || id[0] == '\0') return nullptr;
    for (auto &group : groups_) {
        if (group.is_valid() && strncmp(group.id, id, NVS_GROUP_ID_MAX) == 0) {
            return &group;
        }
    }
    return nullptr;
}

const NvsGroupConfig *DeviceRegistry::find_group(const char *id) const {
    if (id == nullptr || id[0] == '\0') return nullptr;
    for (const auto &group : groups_) {
        if (group.is_valid() && strncmp(group.id, id, NVS_GROUP_ID_MAX) == 0) {
            return &group;
        }
    }
    return nullptr;
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
// COMMAND DISPATCH
// ═════════════════════════════════════════════════════════════════════════════

void DeviceRegistry::command_cover(Device &dev, uint8_t cmd_byte) {
    if (!dev.is_cover()) return;

    auto &cover = std::get<CoverDevice>(dev.logic);
    auto ctx = cover_context(dev.config);
    uint32_t now = millis();

    if (cmd_byte == packet::command::CHECK) {
        request_check(dev);
        return;
    }

    if (cmd_byte == packet::command::STOP) {
        dev.sender.clear_queue();
        (void) enqueue_or_warn_(dev, cmd_byte, packet::button::PACKETS,
                                packet::msg_type::COMMAND,
                                "command_cover(stop)");
        (void) enqueue_check_(dev, "command_cover(stop)");
        cover.state = cover_sm::on_command(cover.state, cmd_byte, now, ctx);
        cover.target_position = cover_sm::NO_TARGET;
    } else {
        if (cmd_byte == packet::command::UP) cover.last_direction = cover_sm::Operation::OPENING;
        if (cmd_byte == packet::command::DOWN) cover.last_direction = cover_sm::Operation::CLOSING;
        bool move_queued = enqueue_or_warn_(dev, cmd_byte, packet::button::PACKETS,
                                            packet::msg_type::BUTTON,
                                            "command_cover(move)");
        (void) enqueue_check_(dev, "command_cover(move)");
        cover.state = cover_sm::on_command(cover.state, cmd_byte, now, ctx);
        if (move_queued) {
            cover.poll.on_command_sent(now);
        }
    }

    notify_state_changed_(dev, now);
}

void DeviceRegistry::set_cover_position(Device &dev, float target) {
    if (!dev.is_cover()) return;

    auto &cover = std::get<CoverDevice>(dev.logic);
    auto ctx = cover_context(dev.config);
    if (!cover_sm::has_position_tracking(ctx)) return;

    uint32_t now = millis();

    uint8_t cmd;
    if (target >= cover_sm::POSITION_OPEN) {
        cmd = packet::command::UP;
        cover.target_position = cover_sm::NO_TARGET;  // Blind handles endpoint
    } else if (target <= cover_sm::POSITION_CLOSED) {
        cmd = packet::command::DOWN;
        cover.target_position = cover_sm::NO_TARGET;  // Blind handles endpoint
    } else {
        float current = cover_sm::position(cover.state, now, ctx);
        cmd = (target > current) ? packet::command::UP : packet::command::DOWN;
        cover.target_position = target;
    }
    bool move_queued = enqueue_or_warn_(dev, cmd, packet::button::PACKETS,
                                        packet::msg_type::BUTTON,
                                        "set_cover_position");
    (void) enqueue_check_(dev, "set_cover_position");
    cover.state = cover_sm::on_command(cover.state, cmd, now, ctx);
    if (cmd == packet::command::UP) cover.last_direction = cover_sm::Operation::OPENING;
    if (cmd == packet::command::DOWN) cover.last_direction = cover_sm::Operation::CLOSING;
    if (move_queued) {
        cover.poll.on_command_sent(now);
    }

    notify_state_changed_(dev, now);
}

void DeviceRegistry::command_cover_tilt(Device &dev) {
    if (!dev.is_cover()) return;

    auto &cover = std::get<CoverDevice>(dev.logic);
    auto ctx = cover_context(dev.config);
    uint32_t now = millis();

    bool tilt_queued = enqueue_or_warn_(dev, packet::command::TILT,
                                        packet::button::PACKETS,
                                        packet::msg_type::BUTTON,
                                        "command_cover_tilt");
    (void) enqueue_check_(dev, "command_cover_tilt");
    cover.state = cover_sm::on_command(cover.state, packet::command::TILT, now, ctx);
    if (tilt_queued) {
        cover.poll.on_command_sent(now);
    }

    notify_state_changed_(dev, now);
}

void DeviceRegistry::command_light(Device &dev, uint8_t cmd_byte) {
    if (!dev.is_light()) return;

    auto &light = std::get<LightDevice>(dev.logic);
    auto ctx = light_context(dev.config);
    uint32_t now = millis();

    if (cmd_byte == packet::command::CHECK) {
        request_check(dev);
        return;
    }

    if (cmd_byte == packet::command::DOWN) {
        light.state = light_sm::on_turn_off(light.state);
        dev.sender.clear_queue();
        (void) enqueue_or_warn_(dev, cmd_byte, packet::button::PACKETS,
                                packet::msg_type::BUTTON,
                                "command_light(off)");
    } else if (cmd_byte == packet::command::UP) {
        light.state = light_sm::on_turn_on(light.state, now, ctx);
        (void) enqueue_or_warn_(dev, cmd_byte, packet::button::PACKETS,
                                packet::msg_type::BUTTON,
                                "command_light(on)");
    } else {
        (void) enqueue_or_warn_(dev, cmd_byte, packet::button::PACKETS,
                                packet::msg_type::BUTTON,
                                "command_light(other)");
    }

    notify_state_changed_(dev, now);
}

void DeviceRegistry::set_light_brightness(Device &dev, float brightness) {
    if (!dev.is_light()) return;

    auto &light = std::get<LightDevice>(dev.logic);
    auto ctx = light_context(dev.config);
    uint32_t now = millis();

    if (brightness <= 0.0f) {
        light.state = light_sm::on_turn_off(light.state);
        dev.sender.clear_queue();
        (void) enqueue_or_warn_(dev, packet::command::DOWN,
                                packet::button::PACKETS,
                                packet::msg_type::BUTTON,
                                "set_light_brightness(off)");
    } else if (!light_sm::supports_brightness(ctx)) {
        light.state = light_sm::on_turn_on(light.state, now, ctx);
        (void) enqueue_or_warn_(dev, packet::command::UP,
                                packet::button::PACKETS,
                                packet::msg_type::BUTTON,
                                "set_light_brightness(on)");
    } else {
        light.state = light_sm::on_set_brightness(light.state, brightness, now, ctx);
        if (std::holds_alternative<light_sm::DimmingUp>(light.state)) {
            (void) enqueue_or_warn_(dev, packet::command::UP,
                                    packet::button::PACKETS,
                                    packet::msg_type::BUTTON,
                                    "set_light_brightness(dim_up)");
        } else if (std::holds_alternative<light_sm::DimmingDown>(light.state)) {
            (void) enqueue_or_warn_(dev, packet::command::DOWN,
                                    packet::button::PACKETS,
                                    packet::msg_type::BUTTON,
                                    "set_light_brightness(dim_down)");
        } else if (light_sm::is_on(light.state)) {
            (void) enqueue_or_warn_(dev, packet::command::UP,
                                    packet::button::PACKETS,
                                    packet::msg_type::BUTTON,
                                    "set_light_brightness(on_state)");
        }
    }

    notify_state_changed_(dev, now);
}

void DeviceRegistry::command_group(Device *const *devices, size_t count, uint8_t cmd_byte) {
    if (count < 2 || count > packet::GROUP_MAX_DESTS || devices == nullptr) {
        ESP_LOGW(TAG, "command_group: invalid count %zu (need 2..%d)", count, packet::GROUP_MAX_DESTS);
        return;
    }

    DeviceType group_type = devices[0] != nullptr ? devices[0]->config.type : DeviceType::COVER;
    uint32_t src_addr = devices[0] != nullptr ? devices[0]->config.src_address : 0;
    for (size_t i = 0; i < count; ++i) {
        if (devices[i] == nullptr || !devices[i]->active || devices[i]->is_remote()) {
            ESP_LOGW(TAG, "command_group: device[%zu] is not an active controllable device", i);
            return;
        }
        if (devices[i]->config.type != group_type) {
            ESP_LOGW(TAG, "command_group: device[%zu] has mixed type", i);
            return;
        }
        if (devices[i]->config.src_address != src_addr) {
            ESP_LOGW(TAG, "command_group: device[%zu] has different src_address (0x%06x vs 0x%06x)",
                     i, devices[i]->config.src_address, src_addr);
            return;
        }
    }

    if (cmd_byte == packet::command::CHECK) {
        for (size_t i = 0; i < count; ++i) {
            request_check(*devices[i]);
        }
        return;
    }

    // Build the group command on the first device's sender.
    // Set multi-dest fields so build_tx_packet_ dispatches to build_group_button_packet.
    Device &lead = *devices[0];
    auto &cmd = lead.sender.command();
    uint8_t prev_num_dests = cmd.num_dests;
    uint8_t prev_dest_channels[packet::GROUP_MAX_DESTS]{};
    for (size_t i = 0; i < packet::GROUP_MAX_DESTS; ++i) {
        prev_dest_channels[i] = cmd.dest_channels[i];
    }

    cmd.num_dests = static_cast<uint8_t>(count);
    for (size_t i = 0; i < count; ++i) {
        cmd.dest_channels[i] = devices[i]->config.channel;
    }

    // Enqueue the command (3x press) via the lead device's sender.
    // RX/status handling remains per-device; for covers we follow up with
    // individual CHECK commands so each blind reports its own state.
    if (!enqueue_or_warn_(lead, cmd_byte, packet::button::PACKETS,
                          packet::msg_type::BUTTON, "command_group(lead)")) {
        cmd.num_dests = prev_num_dests;
        for (size_t i = 0; i < packet::GROUP_MAX_DESTS; ++i) {
            cmd.dest_channels[i] = prev_dest_channels[i];
        }
        return;
    }
    if (group_type == DeviceType::COVER) {
        for (size_t i = 0; i < count; ++i) {
            (void) enqueue_check_(*devices[i], "command_group(check)");
        }
    }

    uint32_t now = millis();
    for (size_t i = 0; i < count; ++i) {
        if (devices[i]->is_cover()) {
            auto &cover = std::get<CoverDevice>(devices[i]->logic);
            auto ctx = cover_context(devices[i]->config);

            if (cmd_byte == packet::command::STOP) {
                cover.state = cover_sm::on_command(cover.state, cmd_byte, now, ctx);
                cover.target_position = cover_sm::NO_TARGET;
            } else {
                if (cmd_byte == packet::command::UP) cover.last_direction = cover_sm::Operation::OPENING;
                if (cmd_byte == packet::command::DOWN) cover.last_direction = cover_sm::Operation::CLOSING;
                cover.state = cover_sm::on_command(cover.state, cmd_byte, now, ctx);
                cover.poll.on_command_sent(now);
            }
        } else if (devices[i]->is_light()) {
            auto &light = std::get<LightDevice>(devices[i]->logic);
            auto ctx = light_context(devices[i]->config);
            if (cmd_byte == packet::command::DOWN) {
                light.state = light_sm::on_turn_off(light.state);
            } else if (cmd_byte == packet::command::UP) {
                light.state = light_sm::on_turn_on(light.state, now, ctx);
            }
        }
        notify_state_changed_(*devices[i], now);
    }

    // num_dests is auto-cleared by CommandSender::advance_queue_() after the
    // group button entry drains. The CHECK commands queued above use
    // type=COMMAND (0x6a) — build_tx_packet_ only checks num_dests for
    // BUTTON type, so CHECKs are safe regardless of timing.

    ESP_LOGI(TAG, "Group command 0x%02x via remote 0x%06x to %zu %s devices (channels: %s)",
             cmd_byte, src_addr, count, device_type_str(group_type),
             [&]() {
                 static char buf[128];
                 size_t pos = 0;
                 for (size_t i = 0; i < count && pos < sizeof(buf) - 4; ++i) {
                     if (i > 0) buf[pos++] = ',';
                     pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", devices[i]->config.channel);
                 }
                 return buf;
             }());
}

bool DeviceRegistry::command_saved_group(const char *id, uint8_t cmd_byte, std::string *error) {
    const NvsGroupConfig *group = find_group(id);
    if (group == nullptr) {
        if (error != nullptr) *error = "Unknown group";
        return false;
    }
    if (cmd_byte == packet::command::INVALID) {
        if (error != nullptr) *error = "Invalid command";
        return false;
    }

    Device *members[NVS_GROUP_MAX_MEMBERS]{};
    uint32_t bucket_src_addrs[NVS_GROUP_MAX_MEMBERS]{};
    uint8_t bucket_counts[NVS_GROUP_MAX_MEMBERS]{};
    size_t bucket_count = 0;

    for (uint8_t i = 0; i < group->member_count; ++i) {
        Device *dev = find(group->device_ids[i]);
        if (dev == nullptr || dev->is_remote()) {
            if (error != nullptr) *error = "Group contains unknown or non-controllable device";
            return false;
        }
        members[i] = dev;

        size_t bucket_idx = 0;
        for (; bucket_idx < bucket_count; ++bucket_idx) {
            if (bucket_src_addrs[bucket_idx] == dev->config.src_address) break;
        }
        if (bucket_idx == bucket_count) {
            bucket_src_addrs[bucket_count] = dev->config.src_address;
            bucket_counts[bucket_count] = 0;
            ++bucket_count;
        }
        ++bucket_counts[bucket_idx];
        if (bucket_counts[bucket_idx] > packet::GROUP_MAX_DESTS) {
            if (error != nullptr) *error = "Too many group members for one remote";
            return false;
        }
    }

    // Re-validate at command time so deleted/changed devices cannot create an
    // invalid protocol transaction.
    if (!validate_group_(*group, error)) return false;

    for (size_t b = 0; b < bucket_count; ++b) {
        Device *bucket_devices[packet::GROUP_MAX_DESTS]{};
        size_t count = 0;
        for (uint8_t i = 0; i < group->member_count; ++i) {
            Device *dev = members[i];
            if (dev->config.src_address == bucket_src_addrs[b]) {
                bucket_devices[count++] = dev;
            }
        }

        if (count == 1) {
            Device &dev = *bucket_devices[0];
            if (dev.is_cover()) {
                command_cover(dev, cmd_byte);
            } else if (dev.is_light()) {
                command_light(dev, cmd_byte);
            }
        } else {
            command_group(bucket_devices, count, cmd_byte);
        }
    }

    ESP_LOGI(TAG, "Saved group '%s' command 0x%02x dispatched in %zu remote bucket(s)",
             group->id, cmd_byte, bucket_count);
    return true;
}

void DeviceRegistry::request_check(Device &dev) {
    if (!dev.active) return;
    bool queued = enqueue_check_(dev, "request_check");
    if (queued && dev.is_cover()) {
        auto &cover = std::get<CoverDevice>(dev.logic);
        cover.poll.on_poll_sent(millis());
    }
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
    } else if (packet::is_command_packet(pkt.type)) {
        // Remote commands are passive — we only auto-discover the remote.
        // The blind's status response (via dispatch_status_) handles state.
        track_remote_(pkt, now);
    }
}

void DeviceRegistry::dispatch_status_(Device &dev, uint8_t state_byte, uint32_t now) {
    std::visit(overloaded{
        [&](CoverDevice &cover) {
            auto ctx = cover_context(dev.config);
            cover.state = cover_sm::on_rf_status(cover.state, state_byte, now, ctx);
            cover.poll.on_rf_received(now);

            // Track tilt state from RF
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
        },
        [&](LightDevice &light) {
            auto ctx = light_context(dev.config);
            light.state = light_sm::on_rf_status(light.state, state_byte, now, ctx);
        },
        [](RemoteDevice &) {},
    }, dev.logic);

    // Always run through the snapshot→diff→publish pipeline on STATUS.
    // The diff handles dedup: if nothing changed (same RSSI, same state),
    // changes=0 and adapters aren't notified. No premature gating here.
    notify_state_changed_(dev, now);
}


void DeviceRegistry::track_remote_(const RfPacketInfo &pkt, uint32_t now) {
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
            notify_state_changed_(*existing, now);
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
    // Don't persist — auto-discovered remotes are ephemeral until user saves.
    // updated_at remains 0, so adapters know not to publish to MQTT.
    notify_added_(*slot);
    // Prime the Published cache so subsequent identical packets dedupe to zero.
    // Without this, every echo/retransmit of the first observed packet would fire
    // a full publish — see ELERO_GROUP_INVESTIGATION.md §8.1.
    notify_state_changed_(*slot, now);
    ESP_LOGI(TAG, "Discovered remote 0x%06x (slot %zu)", pkt.src, slot_index(*slot));
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
    bool was_stopping = std::holds_alternative<cover_sm::Stopping>(cover.state);
    auto old_idx = cover.state.index();
    cover.state = cover_sm::on_tick(cover.state, now, ctx);
    bool state_type_changed = (cover.state.index() != old_idx);

    // 2. Poll if due — single packet suffices (blind is mains-powered, always
    //    listening). If missed, retry via normal poll interval.
    bool moving = cover_sm::is_moving(cover.state);
    if (cover.poll.should_poll(now, moving)) {
        if (enqueue_check_(dev, "loop_cover(poll)")) {
            cover.poll.on_poll_sent(now);
        }
    }

    // 3. Post-stop verification — after Stopping cooldown expires, verify the
    //    blind's actual resting position (frozen estimate may drift).
    if (state_type_changed && was_stopping && std::holds_alternative<cover_sm::Idle>(cover.state)) {
        if (enqueue_check_(dev, "loop_cover(post_stop_check)")) {
            cover.poll.on_poll_sent(now);
        }
    }

    // 4. Intermediate position stop — if cover has position tracking and
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
            (void) enqueue_or_warn_(dev, packet::command::STOP,
                                    packet::button::PACKETS,
                                    packet::msg_type::COMMAND,
                                    "loop_cover(target_stop)");
            (void) enqueue_check_(dev, "loop_cover(target_stop)");
            cover.state = cover_sm::on_command(cover.state, packet::command::STOP, now, ctx);
            state_type_changed = true;
            cover.target_position = cover_sm::NO_TARGET;  // Clear target
        }
    }

    // 5. Process command queue
    if (hub_) {
        dev.sender.process_queue(now, hub_, "elero.cover");
    }

    // 6. Notify state changes
    if (state_type_changed) {
        notify_state_changed_(dev, now);
    } else if (moving &&
               (now - dev.last_published_ms) >= packet::timing::PUBLISH_THROTTLE_MS) {
        // Throttled position updates during movement
        notify_state_changed_(dev, now);
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
        (void) enqueue_or_warn_(dev, packet::button::RELEASE,
                                packet::button::PACKETS,
                                packet::msg_type::BUTTON,
                                "loop_light(release)");
    }

    // 3. Process command queue
    if (hub_) {
        dev.sender.process_queue(now, hub_, "elero.light");
    }

    // 4. Notify state changes
    if (state_type_changed) {
        notify_state_changed_(dev, now);
    } else if (light_sm::is_dimming(light.state) &&
               (now - dev.last_published_ms) >= packet::timing::PUBLISH_THROTTLE_MS) {
        notify_state_changed_(dev, now);
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

size_t DeviceRegistry::count_groups() const {
    size_t n = 0;
    for (const auto &group : groups_) {
        if (group.is_valid()) ++n;
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
    persist(dev, slot_index(dev));
}

void DeviceRegistry::persist_group_(const NvsGroupConfig &group, size_t slot_idx) {
    if (!prefs_initialized_ || slot_idx >= MAX_GROUPS) return;
    if (!group_prefs_[slot_idx].save(&group)) {
        ESP_LOGW(TAG, "Failed to persist group '%s'", group.id);
    }
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

NvsGroupConfig *DeviceRegistry::find_free_group_slot_() {
    for (auto &group : groups_) {
        if (!group.is_valid()) return &group;
    }
    return nullptr;
}

bool DeviceRegistry::validate_group_(const NvsGroupConfig &config, std::string *error) const {
    auto fail = [error](const char *msg) {
        if (error != nullptr) *error = msg;
        return false;
    };

    if (config.id[0] == '\0') return fail("Missing group id");
    if (config.name[0] == '\0') return fail("Missing group name");
    if (config.member_count < 2) return fail("Group requires at least 2 devices");
    if (config.member_count > NVS_GROUP_MAX_MEMBERS) return fail("Too many group members");

    DeviceType derived_type = DeviceType::REMOTE;
    uint32_t seen[NVS_GROUP_MAX_MEMBERS]{};
    size_t seen_count = 0;
    uint32_t src_addrs[NVS_GROUP_MAX_MEMBERS]{};
    uint8_t src_counts[NVS_GROUP_MAX_MEMBERS]{};
    size_t src_count = 0;

    for (uint8_t i = 0; i < config.member_count; ++i) {
        uint32_t address = config.device_ids[i];
        if (address == 0) return fail("Group contains empty device id");
        for (size_t j = 0; j < seen_count; ++j) {
            if (seen[j] == address) return fail("Group contains duplicate device id");
        }
        seen[seen_count++] = address;

        const Device *dev = nullptr;
        for (const auto &slot : slots_) {
            if (slot.active && slot.config.dst_address == address) {
                dev = &slot;
                break;
            }
        }
        if (dev == nullptr) return fail("Group contains unknown device id");
        if (dev->is_remote()) return fail("Groups cannot contain remotes");
        if (i == 0) {
            derived_type = dev->config.type;
        } else if (dev->config.type != derived_type) {
            return fail("Group cannot mix cover and light devices");
        }

        size_t src_idx = 0;
        for (; src_idx < src_count; ++src_idx) {
            if (src_addrs[src_idx] == dev->config.src_address) break;
        }
        if (src_idx == src_count) {
            src_addrs[src_count] = dev->config.src_address;
            src_counts[src_count] = 0;
            ++src_count;
        }
        ++src_counts[src_idx];
        if (src_counts[src_idx] > packet::GROUP_MAX_DESTS) {
            return fail("Too many group members for one remote");
        }
    }
    return true;
}

void DeviceRegistry::clear_group_slot_(NvsGroupConfig &group) {
    size_t idx = static_cast<size_t>(&group - groups_.data());
    NvsGroupConfig empty{};
    empty.version = 0;
    if (prefs_initialized_ && idx < MAX_GROUPS) {
        group_prefs_[idx].save(&empty);
    }
    group = empty;
}

void DeviceRegistry::prune_device_from_groups_(uint32_t address) {
    for (auto &group : groups_) {
        if (!group.is_valid()) continue;

        bool changed = false;
        uint8_t write = 0;
        for (uint8_t read = 0; read < group.member_count; ++read) {
            if (group.device_ids[read] == address) {
                changed = true;
                continue;
            }
            group.device_ids[write++] = group.device_ids[read];
        }
        if (!changed) continue;

        for (uint8_t i = write; i < group.member_count; ++i) {
            group.device_ids[i] = 0;
        }
        group.member_count = write;

        if (group.member_count < 2) {
            char removed_id[NVS_GROUP_ID_MAX]{};
            strncpy(removed_id, group.id, NVS_GROUP_ID_MAX - 1);
            notify_group_removed_(removed_id);
            clear_group_slot_(group);
        } else {
            persist_group_(group, static_cast<size_t>(&group - groups_.data()));
            notify_group_upserted_(group);
        }
    }
}

size_t DeviceRegistry::slot_index(const Device &dev) const {
    return static_cast<size_t>(&dev - slots_.data());
}

void DeviceRegistry::notify_added_(const Device &dev) {
    for (auto *a : adapters_) a->on_device_added(dev);
}

void DeviceRegistry::notify_removed_(const Device &dev) {
    for (auto *a : adapters_) a->on_device_removed(dev);
}

void DeviceRegistry::notify_state_changed_(Device &dev, uint32_t now) {
    uint16_t changes = 0;

    if (dev.is_cover()) {
        auto snap = compute_cover_snapshot(dev, now);
        changes = diff_and_update_cover(snap, std::get<CoverDevice>(dev.logic).published);
    } else if (dev.is_light()) {
        auto snap = compute_light_snapshot(dev, now);
        changes = diff_and_update_light(snap, std::get<LightDevice>(dev.logic).published);
    } else if (dev.is_remote()) {
        auto snap = compute_remote_snapshot(dev);
        changes = diff_and_update_remote(snap, std::get<RemoteDevice>(dev.logic).published);
    }

    if (changes == 0) {
        ESP_LOGVV(TAG, "0x%06x: notify suppressed (no changes)", dev.config.dst_address);
        return;
    }

    ESP_LOGD(TAG, "0x%06x: publish [%s] (0x%04x)",
             dev.config.dst_address, state_change_str(changes), changes);

    dev.last_published_ms = now;
    for (auto *a : adapters_) a->on_state_changed(dev, changes);
}

void DeviceRegistry::notify_config_changed_(const Device &dev) {
    for (auto *a : adapters_) a->on_config_changed(dev);
}

void DeviceRegistry::notify_rf_packet_(const RfPacketInfo &pkt) {
    for (auto *a : adapters_) a->on_rf_packet(pkt);
}

void DeviceRegistry::notify_group_upserted_(const NvsGroupConfig &group) {
    for (auto *a : adapters_) a->on_group_upserted(group);
}

void DeviceRegistry::notify_group_removed_(const char *id) {
    for (auto *a : adapters_) a->on_group_removed(id);
}

void DeviceRegistry::force_republish_all() {
    uint32_t now = millis();
    for (auto &dev : slots_) {
        if (!dev.active) continue;
        // Skip devices we've never heard from via RF — their FSM state is
        // just the default (Idle{POSITION_CLOSED}), not reality. The boot
        // CHECK poll will query them and real STATUS responses will publish.
        if (dev.rf.last_seen_ms == 0) continue;

        // Reset Published to sentinel defaults — the next diff will see everything
        // as changed, recompute valid values, and notify adapters with ALL fields.
        if (dev.is_cover()) {
            std::get<CoverDevice>(dev.logic).published = {};
        } else if (dev.is_light()) {
            std::get<LightDevice>(dev.logic).published = {};
        } else if (dev.is_remote()) {
            std::get<RemoteDevice>(dev.logic).published = {};
        }
        notify_state_changed_(dev, now);
    }
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
