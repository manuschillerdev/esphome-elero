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
#include "output_adapter.h"
#include "overloaded.h"
#include "esphome/core/preferences.h"
#include <array>
#include <concepts>
#include <vector>

namespace esphome::elero {

class Elero;  // Forward declaration (radio core)

class DeviceRegistry {
 public:
    static constexpr size_t MAX_DEVICES = 48;

    // ═════════════════════════════════════════════════════════════════════════
    // LIFECYCLE
    // ═════════════════════════════════════════════════════════════════════════

    /// Enable NVS persistence (MQTT/NVS/Matter modes). Must be called before setup.
    void set_nvs_enabled(bool en) { nvs_enabled_ = en; }
    [[nodiscard]] bool is_nvs_enabled() const { return nvs_enabled_; }

    /// Set the operating mode (called by component codegen).
    void set_hub_mode(HubMode mode) { hub_mode_ = mode; }
    [[nodiscard]] HubMode hub_mode() const { return hub_mode_; }

    /// Matter commissioning info (set by MatterAdapter, read by web server).
    void set_commissioning_info(const std::string &qr, const std::string &manual) {
        commissioning_qr_ = qr;
        commissioning_manual_ = manual;
    }
    [[nodiscard]] const std::string &commissioning_qr() const { return commissioning_qr_; }
    [[nodiscard]] const std::string &commissioning_manual() const { return commissioning_manual_; }

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

    /// Add a device without NVS persistence (for YAML-defined devices).
    /// Source of truth is YAML, not NVS.
    Device *register_device(const NvsDeviceConfig &config);

    /// Remove a device by address and type. Returns true if found and removed.
    bool remove(uint32_t address, DeviceType type);

    /// Find a device by address and type.
    [[nodiscard]] Device *find(uint32_t address, DeviceType type);

    /// Find any device by address (first match, any type).
    [[nodiscard]] Device *find(uint32_t address);

    // ═════════════════════════════════════════════════════════════════════════
    // USER COMMAND DISPATCH
    // ═════════════════════════════════════════════════════════════════════════

    /// Send a cover command: transitions cover_sm, clears queue on STOP,
    /// enqueues TX, marks poll sent. Called by adapters (MQTT, Matter, web).
    void send_cover_command(Device &dev, uint8_t cmd_byte);

    /// Turn on a light (full brightness or last brightness).
    void send_light_on(Device &dev);

    /// Turn off a light.
    void send_light_off(Device &dev);

    /// Set light brightness (0.0–1.0). Determines dim direction and enqueues TX.
    void send_light_brightness(Device &dev, float target);

    // ═════════════════════════════════════════════════════════════════════════
    // RF DISPATCH
    // ═════════════════════════════════════════════════════════════════════════

    /// Process a decoded RF packet. Updates device state machines, notifies adapters.
    void on_rf_packet(const RfPacketInfo &pkt, uint32_t now);

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

    [[nodiscard]] size_t count_active() const;
    [[nodiscard]] size_t count_active(DeviceType type) const;

    // ═════════════════════════════════════════════════════════════════════════
    // SLOT ACCESS (for ESPHome adapter shell binding)
    // ═════════════════════════════════════════════════════════════════════════

    Device *slot(size_t idx) { return (idx < MAX_DEVICES) ? &slots_[idx] : nullptr; }
    static constexpr size_t max_devices() { return MAX_DEVICES; }

    // ═════════════════════════════════════════════════════════════════════════
    // PERSISTENCE
    // ═════════════════════════════════════════════════════════════════════════

    /// Persist a single device to NVS.
    void persist(Device &dev, size_t slot_idx);

    /// Persist a device (finds slot index automatically).
    void persist(Device &dev);

 private:
    std::array<Device, MAX_DEVICES> slots_{};
    std::vector<OutputAdapter *> adapters_;
    Elero *hub_{nullptr};
    bool nvs_enabled_{false};
    HubMode hub_mode_{HubMode::NATIVE};
    std::string commissioning_qr_;
    std::string commissioning_manual_;

    // NVS preference handles (one per slot)
    ESPPreferenceObject prefs_[MAX_DEVICES]{};
    bool prefs_initialized_{false};

    // ── Internal helpers ──
    Device *find_free_slot_();
    size_t slot_index_(const Device &dev) const;
    void notify_added_(const Device &dev);
    void notify_removed_(const Device &dev);
    void notify_state_changed_(const Device &dev);
    void notify_config_changed_(const Device &dev);
    void notify_rf_packet_(const RfPacketInfo &pkt);

    /// Process cover device loop (polling, timeouts, position, command queue).
    void loop_cover_(Device &dev, CoverDevice &cover, uint32_t now);

    /// Process light device loop (dimming, command queue).
    void loop_light_(Device &dev, LightDevice &light, uint32_t now);

    /// Handle an RF status packet for a specific device.
    void dispatch_status_(Device &dev, uint8_t state_byte, uint32_t now);

    /// Handle an RF command packet (from a remote) targeting a device.
    void dispatch_remote_command_(Device &dev, uint8_t cmd_byte, uint32_t now);

    /// Track a remote control from an observed RF command packet.
    void track_remote_(const RfPacketInfo &pkt, uint32_t now);

    /// Assign staggered poll offsets to all active covers.
    void assign_poll_stagger_();
};

}  // namespace esphome::elero
