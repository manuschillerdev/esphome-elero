/// @file device_registry.h
/// @brief Single source of truth for all devices — CRUD, NVS persistence, RF dispatch, observers.
///
/// The registry replaces: IDeviceManager, NativeDeviceManager, NvsDeviceManagerBase,
///                         MqttDeviceManager, NativeNvsDeviceManager
///
/// It is mode-agnostic. Output adapters (MQTT, HA native, WebSocket, Matter) observe
/// the registry and react to device events. The registry doesn't know about output formats.

#pragma once

#include "device.h"
#include "nvs_hub_config.h"
#include "output_adapter.h"
#include "overloaded.h"
#include "esphome/core/preferences.h"
#include <array>
#include <concepts>
#include <string>
#include <vector>

namespace esphome::elero {

class Elero;  // Forward declaration (radio core)

class DeviceRegistry {
 public:
    static constexpr size_t MAX_DEVICES = 48;
    static constexpr size_t MAX_GROUPS = 16;

    // ═════════════════════════════════════════════════════════════════════════
    // LIFECYCLE
    // ═════════════════════════════════════════════════════════════════════════

    /// Enable NVS persistence (MQTT/NVS modes). Must be called before setup.
    void set_nvs_enabled(bool en) { nvs_enabled_ = en; }
    [[nodiscard]] bool is_nvs_enabled() const { return nvs_enabled_; }

    /// Set/get hub operating mode (for web UI mode reporting).
    void set_hub_mode(HubMode m) { mode_ = m; }
    [[nodiscard]] HubMode hub_mode() const { return mode_; }

    /// Initialize NVS preference handles.
    void init_preferences();

    /// Restore all devices from NVS. Call during setup().
    void restore_all();

    /// Set the radio hub (needed for CommandSender TX).
    void set_hub(Elero *hub) { hub_ = hub; }

    /// Register an output adapter. Call before restore_all().
    void add_adapter(OutputAdapter *adapter);

    /// Call adapter setup (must be called before restore_all).
    void setup_adapters();

    /// Call from ESPHome loop(). Processes command queues, timers, timeouts, adapters.
    void loop(uint32_t now);

    // ═════════════════════════════════════════════════════════════════════════
    // CRUD
    // ═════════════════════════════════════════════════════════════════════════

    /// Add or update a device with NVS persistence.
    /// If address+type already exists, updates config.
    /// Returns pointer to the device slot, or nullptr if no free slot.
    Device *upsert(const NvsDeviceConfig &config);

    /// Remove a device by address and type. Returns true if found and removed.
    bool remove(uint32_t address, DeviceType type);

    /// Add or update a saved group with NVS persistence. Validates that all
    /// device ids resolve to active non-remote devices of one derived type.
    NvsGroupConfig *upsert_group(const NvsGroupConfig &config, std::string *error = nullptr);

    /// Remove a saved group by id. Returns true if found and removed.
    bool remove_group(const char *id);

    /// Find a saved group by id.
    [[nodiscard]] NvsGroupConfig *find_group(const char *id);
    [[nodiscard]] const NvsGroupConfig *find_group(const char *id) const;

    /// Find a device by address and type.
    [[nodiscard]] Device *find(uint32_t address, DeviceType type);

    /// Find any device by address (first match, any type).
    [[nodiscard]] Device *find(uint32_t address);

    // ═════════════════════════════════════════════════════════════════════════
    // COMMAND DISPATCH
    // ═════════════════════════════════════════════════════════════════════════

    /// Dispatch a command byte to a cover device (open/close/stop + FSM + enqueue + poll).
    void command_cover(Device &dev, uint8_t cmd_byte);

    /// Set a cover's target position (0.0–1.0). Determines direction, sets target, starts movement.
    void set_cover_position(Device &dev, float target);

    /// Dispatch a tilt command to a cover device.
    void command_cover_tilt(Device &dev);

    /// Dispatch a command byte to a light device (on/off + FSM + enqueue).
    void command_light(Device &dev, uint8_t cmd_byte);

    /// Set a light's target brightness (0.0–1.0). Determines dim direction, starts dimming.
    void set_light_brightness(Device &dev, float brightness);

    /// Send one RF group command to multiple same-type devices sharing one remote.
    /// Each device's channel becomes a destination in the packet. All devices must share
    /// the same src_address (same emulated remote). Single-member buckets fall back to
    /// the normal per-device command path at the saved-group orchestration layer.
    /// @param devices Pointer to array of Device pointers (must be active covers or active lights)
    /// @param count Number of devices in the array
    /// @param cmd_byte Command byte (UP/DOWN/STOP/CHECK)
    void command_group(Device *const *devices, size_t count, uint8_t cmd_byte);

    /// Dispatch a saved group command by id. Resolves member ids, derives their
    /// common device type, partitions by src_address, then emits one TX command
    /// per remote bucket. RX/status handling remains per-device and unchanged.
    bool command_saved_group(const char *id, uint8_t cmd_byte, std::string *error = nullptr);

    /// Request an immediate status CHECK for any device (cover or light).
    /// Enqueues a single CHECK packet — blind responds with current state.
    void request_check(Device &dev);

    // ═════════════════════════════════════════════════════════════════════════
    // RF DISPATCH
    // ═════════════════════════════════════════════════════════════════════════

    /// Process a decoded RF packet. Updates device state machines, notifies adapters.
    void on_rf_packet(const RfPacketInfo &pkt, uint32_t now);

    /// Reset all Published caches and re-notify adapters through the normal
    /// snapshot→diff→update pipeline. Use when an adapter's downstream (e.g. MQTT
    /// broker) has lost state and needs a full republish.
    void force_republish_all();

    // ═════════════════════════════════════════════════════════════════════════
    // ITERATION
    // ═════════════════════════════════════════════════════════════════════════

    template<std::invocable<Device &> F>
    void for_each_active(F &&fn) {
        for (auto &dev : slots_) {
            if (dev.active) fn(dev);
        }
    }

    template<std::invocable<const Device &> F>
    void for_each_active(F &&fn) const {
        for (const auto &dev : slots_) {
            if (dev.active) fn(dev);
        }
    }

    template<std::invocable<const Device &> F>
    void for_each_active(DeviceType type, F &&fn) const {
        for (const auto &dev : slots_) {
            if (dev.active && dev.config.type == type) fn(dev);
        }
    }

    template<std::invocable<const NvsGroupConfig &> F>
    void for_each_group(F &&fn) const {
        for (const auto &group : groups_) {
            if (group.is_valid()) fn(group);
        }
    }

    [[nodiscard]] size_t count_active() const;
    [[nodiscard]] size_t count_groups() const;
    [[nodiscard]] size_t count_active(DeviceType type) const;

    // ═════════════════════════════════════════════════════════════════════════
    // SLOT ACCESS (for ESPHome adapter shell binding)
    // ═════════════════════════════════════════════════════════════════════════

    Device *slot(size_t idx) { return (idx < MAX_DEVICES) ? &slots_[idx] : nullptr; }
    [[nodiscard]] size_t slot_index(const Device &dev) const;
    static constexpr size_t max_devices() { return MAX_DEVICES; }

    // ═════════════════════════════════════════════════════════════════════════
    // PERSISTENCE
    // ═════════════════════════════════════════════════════════════════════════

    /// Persist a single device to NVS.
    void persist(Device &dev, size_t slot_idx);

    /// Persist a device (finds slot index automatically).
    void persist(Device &dev);

    // ═════════════════════════════════════════════════════════════════════════
    // HUB-LEVEL CONFIG (user-overridable hub display name)
    // ═════════════════════════════════════════════════════════════════════════

    /// Hub display name: user override from NVS if set, otherwise default from YAML.
    /// Used by the MQTT adapter for the HA gateway device block and by the web
    /// UI for display.
    [[nodiscard]] const std::string &hub_display_name() const { return hub_display_name_; }

    /// YAML-configured default hub name (no NVS override applied).
    [[nodiscard]] const std::string &hub_default_name() const { return hub_default_name_; }

    /// True when the user has set a non-empty hub name override (i.e. the
    /// effective display name differs from the YAML default).
    [[nodiscard]] bool has_hub_name_override() const { return !hub_name_override_.empty(); }

    /// Default hub name (YAML-configured, fallback when no NVS override).
    void set_default_hub_name(const std::string &name);

    /// Override the hub display name (persists to NVS).
    /// Empty string clears the override and falls back to the default name.
    /// Returns true if value actually changed (so callers can republish discovery).
    bool set_hub_name_override(const std::string &name);

 private:
    std::array<Device, MAX_DEVICES> slots_{};
    std::array<NvsGroupConfig, MAX_GROUPS> groups_{};
    std::vector<OutputAdapter *> adapters_;
    Elero *hub_{nullptr};
    bool nvs_enabled_{false};
    HubMode mode_{HubMode::NATIVE};

    // NVS preference handles (one per slot)
    ESPPreferenceObject prefs_[MAX_DEVICES]{};
    ESPPreferenceObject group_prefs_[MAX_GROUPS]{};
    bool prefs_initialized_{false};

    // Hub config (display name override)
    ESPPreferenceObject hub_prefs_{};
    std::string hub_default_name_;       ///< YAML-configured default
    std::string hub_name_override_;      ///< NVS override (empty = no override)
    std::string hub_display_name_;       ///< Effective name (override or default)

    void update_hub_display_name_();

    // ── Internal helpers ──
    Device *find_free_slot_();
    NvsGroupConfig *find_free_group_slot_();
    [[nodiscard]] bool validate_group_(const NvsGroupConfig &config, std::string *error) const;
    void persist_group_(const NvsGroupConfig &group, size_t slot_idx);
    void clear_group_slot_(NvsGroupConfig &group);
    void prune_device_from_groups_(uint32_t address);
    void notify_added_(const Device &dev);
    void notify_removed_(const Device &dev);
    void notify_state_changed_(Device &dev, uint32_t now);
    void notify_config_changed_(const Device &dev);
    void notify_rf_packet_(const RfPacketInfo &pkt);
    void notify_group_upserted_(const NvsGroupConfig &group);
    void notify_group_removed_(const char *id);

    [[nodiscard]] bool enqueue_or_warn_(Device &dev, uint8_t cmd_byte,
                                        uint8_t packets, uint8_t type,
                                        const char *context);
    [[nodiscard]] bool enqueue_check_(Device &dev, const char *context);

    /// Process cover device loop (polling, timeouts, position, command queue).
    void loop_cover_(Device &dev, CoverDevice &cover, uint32_t now);

    /// Process light device loop (dimming, command queue).
    void loop_light_(Device &dev, LightDevice &light, uint32_t now);

    /// Handle an RF status packet for a specific device.
    /// Always runs through snapshot→diff→publish; the diff handles dedup.
    void dispatch_status_(Device &dev, uint8_t state_byte, uint32_t now);

    /// Track a remote control from an observed RF command packet.
    void track_remote_(const RfPacketInfo &pkt, uint32_t now);

    /// Assign staggered poll offsets to all active covers.
    void assign_poll_stagger_();
};

}  // namespace esphome::elero
