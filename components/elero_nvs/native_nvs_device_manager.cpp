/// @file native_nvs_device_manager.cpp
/// @brief Native+NVS device manager: NVS persistence + ESPHome native API.

#include "native_nvs_device_manager.h"
#include "../elero/elero_packet.h"
#include "../elero/elero_strings.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/light/light_state.h"

namespace esphome {
namespace elero {

static const char *const TAG = "elero.nvs_mgr";

// ═══════════════════════════════════════════════════════════════════════════════
// Component Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void NativeNvsDeviceManager::setup() {
  if (hub_ == nullptr) {
    ESP_LOGE(TAG, "Hub not set");
    this->mark_failed();
    return;
  }

  hub_->set_device_manager(this);

  // Warn about YAML-defined covers/lights being ignored in NVS mode
  size_t yaml_covers = hub_->get_configured_covers().size();
  size_t yaml_lights = hub_->get_configured_lights().size();
  if (yaml_covers > 0 || yaml_lights > 0) {
    ESP_LOGW(TAG, "══════════════════════════════════════════════════════════════");
    ESP_LOGW(TAG, "  NVS mode is active. YAML-defined covers (%d) and lights (%d)", yaml_covers, yaml_lights);
    ESP_LOGW(TAG, "  will be IGNORED. Remove cover/light entries from YAML.");
    ESP_LOGW(TAG, "══════════════════════════════════════════════════════════════");
  }

  init_slot_preferences_();

  size_t covers = 0, lights = 0, remotes = 0;

  // Restore cover slots from NVS and register with ESPHome
  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; ++i) {
      if (!cover_slots_[i].restore()) continue;
      if (cover_slots_[i].activate(cover_slots_[i].config(), hub_)) {
        cover_slots_[i].sync_config_to_core();
        cover_slots_[i].apply_name_from_config();
        // Register with ESPHome for native API discovery and lifecycle
        App.register_cover(&cover_slots_[i]);
        App.register_component(&cover_slots_[i]);
        ++covers;
      }
    }
  }

  // Restore light slots from NVS and register with ESPHome
  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; ++i) {
      if (!light_slots_[i].restore()) continue;
      if (light_slots_[i].activate(light_slots_[i].config(), hub_)) {
        light_slots_[i].sync_config_to_core();
        // Create a LightState wrapper — ESPHome native API needs this
        auto *ls = new light::LightState(&light_slots_[i]);
        ls->set_name(light_slots_[i].config().name);
        ls->set_object_id(light_slots_[i].config().name);
        App.register_light(ls);
        App.register_component(ls);
        App.register_component(&light_slots_[i]);
        ++lights;
      }
    }
  }

  // Restore remote slots from NVS
  if (remote_slots_ != nullptr) {
    for (size_t i = 0; i < max_remotes_; ++i) {
      if (!remote_slots_[i].restore()) continue;
      remote_slots_[i].set_state_callback([](EleroRemoteControl *) {});
      remote_slots_[i].activate(remote_slots_[i].config());
      ++remotes;
    }
  }

  setup_done_ = true;
  ESP_LOGI(TAG, "%d covers, %d lights, %d remotes restored", covers, lights, remotes);
}

void NativeNvsDeviceManager::loop() {
  // Covers and lights are registered as Components — ESPHome calls their loop().
  // Nothing to do here for now.
}

void NativeNvsDeviceManager::dump_config() {
  ESP_LOGCONFIG(TAG, "Native+NVS Device Manager:");
  ESP_LOGCONFIG(TAG, "  Mode: NATIVE_NVS");
  ESP_LOGCONFIG(TAG, "  Max covers: %d", max_covers_);
  ESP_LOGCONFIG(TAG, "  Max lights: %d", max_lights_);
  ESP_LOGCONFIG(TAG, "  Max remotes: %d", max_remotes_);
}

// ═══════════════════════════════════════════════════════════════════════════════
// RF Packet Handler
// ═══════════════════════════════════════════════════════════════════════════════

void NativeNvsDeviceManager::on_rf_packet(const RfPacketInfo &pkt) {
  if (packet::is_command_packet(pkt.type)) {
    track_remote_(pkt);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Device CRUD
// ═══════════════════════════════════════════════════════════════════════════════

bool NativeNvsDeviceManager::upsert_device(const NvsDeviceConfig &config) {
  if (config.is_cover()) {
    auto *slot = find_active_cover_(config.dst_address);
    if (slot != nullptr) {
      // Update existing — write config to NVS
      slot->update_config(config);
      slot->sync_config_to_core();
      slot->apply_name_from_config();
      if (setup_done_) {
        ESP_LOGW(TAG, "Cover 0x%06x updated in NVS. Reboot to apply name/entity changes.",
                 config.dst_address);
      }
    } else {
      slot = find_free_cover_slot_();
      if (slot == nullptr) {
        ESP_LOGE(TAG, "No free cover slot for 0x%06x", config.dst_address);
        return false;
      }
      if (!slot->activate(config, hub_)) {
        ESP_LOGW(TAG, "Failed to activate cover 0x%06x", config.dst_address);
        return false;
      }
      slot->sync_config_to_core();
      slot->apply_name_from_config();
      (void)slot->save_config();

      if (setup_done_) {
        // Can't register new entities with native API after setup
        ESP_LOGW(TAG, "Cover 0x%06x saved to NVS. Reboot required to register with native API.",
                 config.dst_address);
      } else {
        App.register_cover(slot);
        App.register_component(slot);
      }
    }

    notify_crud_(crud_event::DEVICE_UPSERTED, config.dst_address, device_type_str(DeviceType::COVER));
    return true;
  }

  if (config.is_light()) {
    auto *slot = find_active_light_(config.dst_address);
    if (slot != nullptr) {
      slot->update_config(config);
      slot->sync_config_to_core();
      if (setup_done_) {
        ESP_LOGW(TAG, "Light 0x%06x updated in NVS. Reboot to apply name/entity changes.",
                 config.dst_address);
      }
    } else {
      slot = find_free_light_slot_();
      if (slot == nullptr) {
        ESP_LOGE(TAG, "No free light slot for 0x%06x", config.dst_address);
        return false;
      }
      if (!slot->activate(config, hub_)) {
        ESP_LOGW(TAG, "Failed to activate light 0x%06x", config.dst_address);
        return false;
      }
      slot->sync_config_to_core();
      (void)slot->save_config();

      if (setup_done_) {
        ESP_LOGW(TAG, "Light 0x%06x saved to NVS. Reboot required to register with native API.",
                 config.dst_address);
      } else {
        auto *ls = new light::LightState(slot);
        ls->set_name(slot->config().name);
        ls->set_object_id(slot->config().name);
        App.register_light(ls);
        App.register_component(ls);
        App.register_component(slot);
      }
    }

    notify_crud_(crud_event::DEVICE_UPSERTED, config.dst_address, device_type_str(DeviceType::LIGHT));
    return true;
  }

  if (config.is_remote()) {
    auto *slot = find_active_remote_(config.dst_address);
    if (slot != nullptr) {
      slot->update_config(config);
    } else {
      slot = find_free_remote_slot_();
      if (slot == nullptr) {
        ESP_LOGE(TAG, "No free remote slot for 0x%06x", config.dst_address);
        return false;
      }
      if (!slot->activate(config)) {
        ESP_LOGW(TAG, "Failed to activate remote 0x%06x", config.dst_address);
        return false;
      }
      (void)slot->save_config();
    }

    notify_crud_(crud_event::DEVICE_UPSERTED, config.dst_address, device_type_str(DeviceType::REMOTE));
    return true;
  }

  ESP_LOGW(TAG, "upsert_device: unknown device type %d", static_cast<int>(config.type));
  return false;
}

bool NativeNvsDeviceManager::remove_device(DeviceType type, uint32_t dst_address) {
  bool removed = false;

  switch (type) {
    case DeviceType::COVER: {
      auto *slot = find_active_cover_(dst_address);
      if (slot == nullptr) return false;
      slot->deactivate();
      removed = true;
      if (setup_done_) {
        ESP_LOGW(TAG, "Cover 0x%06x removed from NVS. Reboot to remove from native API.",
                 dst_address);
      }
      break;
    }
    case DeviceType::LIGHT: {
      auto *slot = find_active_light_(dst_address);
      if (slot == nullptr) return false;
      slot->deactivate();
      removed = true;
      if (setup_done_) {
        ESP_LOGW(TAG, "Light 0x%06x removed from NVS. Reboot to remove from native API.",
                 dst_address);
      }
      break;
    }
    case DeviceType::REMOTE: {
      auto *slot = find_active_remote_(dst_address);
      if (slot == nullptr) return false;
      slot->deactivate();
      removed = true;
      break;
    }
  }

  if (removed) {
    notify_crud_(crud_event::DEVICE_REMOVED, dst_address, device_type_str(type));
  }
  return removed;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Remote Control Tracking
// ═══════════════════════════════════════════════════════════════════════════════

void NativeNvsDeviceManager::track_remote_(const RfPacketInfo &pkt) {
  uint32_t remote_addr = pkt.src;
  if (remote_addr == 0) return;

  auto *existing = find_active_remote_(remote_addr);
  if (existing != nullptr) {
    existing->update_from_packet(pkt.timestamp_ms, pkt.rssi, pkt.channel,
                                  pkt.command, pkt.dst);
    return;
  }

  auto *slot = find_free_remote_slot_();
  if (slot == nullptr) {
    ESP_LOGV(TAG, "No free remote slot for new remote 0x%06x", remote_addr);
    return;
  }

  NvsDeviceConfig cfg{};
  cfg.type = DeviceType::REMOTE;
  cfg.dst_address = remote_addr;
  snprintf(cfg.name, sizeof(cfg.name), DEFAULT_REMOTE_NAME_FMT, remote_addr);

  // No-op callback — NativeNvs remotes don't publish state, but consistency with base class
  slot->set_state_callback([](EleroRemoteControl *) {});
  slot->activate(cfg);
  slot->update_from_packet(pkt.timestamp_ms, pkt.rssi, pkt.channel, pkt.command, pkt.dst);
  (void)slot->save_config();

  notify_crud_(crud_event::DEVICE_UPSERTED, remote_addr, device_type_str(DeviceType::REMOTE));
  ESP_LOGI(TAG, "Auto-discovered remote 0x%06x", remote_addr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Slot Management
// ═══════════════════════════════════════════════════════════════════════════════

void NativeNvsDeviceManager::init_slot_preferences_() {
  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; ++i) {
      uint32_t hash = fnv1_hash(nvs_pref_key::NVS_COVER) + i;
      cover_slots_[i].set_preference(global_preferences->make_preference<NvsDeviceConfig>(hash));
    }
  }
  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; ++i) {
      uint32_t hash = fnv1_hash(nvs_pref_key::NVS_LIGHT) + i;
      light_slots_[i].set_preference(global_preferences->make_preference<NvsDeviceConfig>(hash));
    }
  }
  if (remote_slots_ != nullptr) {
    for (size_t i = 0; i < max_remotes_; ++i) {
      uint32_t hash = fnv1_hash(nvs_pref_key::NVS_REMOTE) + i;
      remote_slots_[i].set_preference(global_preferences->make_preference<NvsDeviceConfig>(hash));
    }
  }
}

NativeNvsCover *NativeNvsDeviceManager::find_free_cover_slot_() {
  if (cover_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_covers_; ++i) {
    if (!cover_slots_[i].is_active()) return &cover_slots_[i];
  }
  return nullptr;
}

NativeNvsLight *NativeNvsDeviceManager::find_free_light_slot_() {
  if (light_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_lights_; ++i) {
    if (!light_slots_[i].is_active()) return &light_slots_[i];
  }
  return nullptr;
}

EleroRemoteControl *NativeNvsDeviceManager::find_free_remote_slot_() {
  if (remote_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_remotes_; ++i) {
    if (!remote_slots_[i].is_active()) return &remote_slots_[i];
  }
  return nullptr;
}

NativeNvsCover *NativeNvsDeviceManager::find_active_cover_(uint32_t addr) {
  if (cover_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_covers_; ++i) {
    if (cover_slots_[i].is_active() && cover_slots_[i].get_blind_address() == addr) {
      return &cover_slots_[i];
    }
  }
  return nullptr;
}

NativeNvsLight *NativeNvsDeviceManager::find_active_light_(uint32_t addr) {
  if (light_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_lights_; ++i) {
    if (light_slots_[i].is_active() && light_slots_[i].get_blind_address() == addr) {
      return &light_slots_[i];
    }
  }
  return nullptr;
}

EleroRemoteControl *NativeNvsDeviceManager::find_active_remote_(uint32_t addr) {
  if (remote_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_remotes_; ++i) {
    if (remote_slots_[i].is_active() && remote_slots_[i].get_address() == addr) {
      return &remote_slots_[i];
    }
  }
  return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// CRUD Event Notification
// ═══════════════════════════════════════════════════════════════════════════════

void NativeNvsDeviceManager::notify_crud_(const char *event, const char *json_str) {
  if (crud_callback_) {
    crud_callback_(event, json_str);
  }
}

void NativeNvsDeviceManager::notify_crud_(const char *event, uint32_t addr, const char *device_type) {
  std::string resp = json::build_json([&](JsonObject root) {
    root["address"] = hex_str(addr);
    root["device_type"] = device_type;
  });
  notify_crud_(event, resp.c_str());
}

}  // namespace elero
}  // namespace esphome
