/// @file device.h
/// @brief Unified Device struct — one model for all device types and all output modes.
///
/// A Device composes:
///   NvsDeviceConfig (persistence format, used directly as config)
///   + RfMeta (shared RF metadata)
///   + CommandSender (shared TX queue — covers/lights use it, remotes don't)
///   + variant<CoverDevice, LightDevice, RemoteDevice> (type-specific state)
///
/// CommandSender lives OUTSIDE the variant because TxClient (its base) is
/// non-movable (hub holds pointer during TX). The variant must be movable.

#pragma once

#include "device_type.h"
#include "nvs_config.h"
#include "cover_sm.h"
#include "light_sm.h"
#include "poll_timer.h"
#include "command_sender.h"
#include <variant>

namespace esphome::elero {

// ═══════════════════════════════════════════════════════════════════════════════
// RF METADATA (shared by all device types)
// ═══════════════════════════════════════════════════════════════════════════════

struct RfMeta {
    uint32_t last_seen_ms{0};
    float    last_rssi{0.0f};
    uint8_t  last_state_raw{0};
};

// ═══════════════════════════════════════════════════════════════════════════════
// TYPE-SPECIFIC DEVICE LOGIC (variant arms — must be movable!)
// CommandSender is NOT here because TxClient is non-movable.
// ═══════════════════════════════════════════════════════════════════════════════

struct CoverDevice {
    cover_sm::State state{cover_sm::Idle{0.0f}};
    PollTimer       poll;
    float           target_position{-1.0f};       ///< -1 = no target, 0..1 = intermediate target
    cover_sm::Operation last_direction{cover_sm::Operation::OPENING};  ///< For toggle logic
};

struct LightDevice {
    light_sm::State state{light_sm::Off{}};
};

struct RemoteDevice {
    uint8_t  last_command{0};
    uint32_t last_target{0};
    uint8_t  last_channel{0};
};

using DeviceLogic = std::variant<CoverDevice, LightDevice, RemoteDevice>;

// ═══════════════════════════════════════════════════════════════════════════════
// THE DEVICE — one struct, replaces 6+ entity classes
// ═══════════════════════════════════════════════════════════════════════════════

struct Device {
    bool            active{false};       ///< false = empty slot
    NvsDeviceConfig config;              ///< Persistence format, used directly
    RfMeta          rf;                  ///< Shared RF metadata
    DeviceLogic     logic;               ///< Type-specific state (movable)
    CommandSender   sender;              ///< TX queue (non-movable, shared by covers/lights)
    uint32_t        last_notify_ms{0};   ///< Throttle state change notifications

    [[nodiscard]] DeviceType type() const {
        static_assert(std::is_same_v<std::variant_alternative_t<0, DeviceLogic>, CoverDevice>);
        static_assert(std::is_same_v<std::variant_alternative_t<1, DeviceLogic>, LightDevice>);
        static_assert(std::is_same_v<std::variant_alternative_t<2, DeviceLogic>, RemoteDevice>);
        return static_cast<DeviceType>(logic.index());
    }

    [[nodiscard]] bool is_cover() const {
        return std::holds_alternative<CoverDevice>(logic);
    }

    [[nodiscard]] bool is_light() const {
        return std::holds_alternative<LightDevice>(logic);
    }

    [[nodiscard]] bool is_remote() const {
        return std::holds_alternative<RemoteDevice>(logic);
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// CONTEXT BUILDERS
// ═══════════════════════════════════════════════════════════════════════════════

inline cover_sm::Context cover_context(const NvsDeviceConfig &cfg) {
    return {cfg.open_duration_ms, cfg.close_duration_ms,
            packet::timing::TIMEOUT_MOVEMENT,
            packet::timing::POST_STOP_COOLDOWN_MS};
}

inline light_sm::Context light_context(const NvsDeviceConfig &cfg) {
    return {cfg.dim_duration_ms};
}

// ═══════════════════════════════════════════════════════════════════════════════
// DEVICE LIFECYCLE HELPERS
// ═══════════════════════════════════════════════════════════════════════════════

/// Configure a CommandSender's command template from device config.
inline void configure_sender(CommandSender &sender, const NvsDeviceConfig &cfg) {
    auto &cmd = sender.command();
    cmd.dst_addr = cfg.dst_address;
    cmd.src_addr = cfg.src_address;
    cmd.channel = cfg.channel;
    cmd.type = cfg.type_byte;
    cmd.type2 = cfg.type2;
    cmd.hop = cfg.hop;
    cmd.payload[0] = cfg.payload_1;
    cmd.payload[1] = cfg.payload_2;
}

/// Initialize a device slot from config.
inline void init_device(Device &dev, const NvsDeviceConfig &cfg) {
    dev.active = true;
    dev.config = cfg;
    dev.rf = {};
    dev.last_notify_ms = 0;

    switch (cfg.type) {
        case DeviceType::COVER: {
            dev.logic = CoverDevice{};
            auto &cover = std::get<CoverDevice>(dev.logic);
            cover.poll.interval_ms = cfg.poll_interval_ms;
            configure_sender(dev.sender, cfg);
            break;
        }
        case DeviceType::LIGHT:
            dev.logic = LightDevice{};
            configure_sender(dev.sender, cfg);
            break;
        case DeviceType::REMOTE:
            dev.logic = RemoteDevice{};
            break;
    }
}

/// Reset a device slot to empty (without assignment — Device is non-movable).
inline void deactivate_device(Device &dev) {
    dev.active = false;
    dev.config = NvsDeviceConfig{};
    dev.rf = {};
    dev.logic = CoverDevice{};  // Reset variant to default
    dev.sender.clear_queue();
    dev.last_notify_ms = 0;
}

/// Update a device's config without destroying state.
inline void update_device_config(Device &dev, const NvsDeviceConfig &cfg) {
    dev.config = cfg;
    configure_sender(dev.sender, cfg);

    if (auto *cover = std::get_if<CoverDevice>(&dev.logic)) {
        cover->poll.interval_ms = cfg.poll_interval_ms;
    }
}

}  // namespace esphome::elero
