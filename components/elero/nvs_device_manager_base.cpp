/// @file nvs_device_manager_base.cpp
/// @brief Common NVS slot management, CRUD, and remote tracking.

#include "nvs_device_manager_base.h"
#include "elero_packet.h"
#include "elero_strings.h"
#include "esphome/core/log.h"
#include "esphome/components/json/json_util.h"

namespace esphome {
namespace elero {

// ═══════════════════════════════════════════════════════════════════════════════
// Component Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void NvsDeviceManagerBase::setup() {
  if (hub_ == nullptr) {
    ESP_LOGE(manager_tag_(), "Hub not set");
    this->mark_failed();
    return;
  }

  hub_->set_device_manager(this);

  // Warn about YAML-defined covers/lights being ignored in NVS mode
  size_t yaml_covers = hub_->get_configured_covers().size();
  size_t yaml_lights = hub_->get_configured_lights().size();
  if (yaml_covers > 0 || yaml_lights > 0) {
    ESP_LOGW(manager_tag_(), "══════════════════════════════════════════════════════════════");
    ESP_LOGW(manager_tag_(), "  NVS mode is active. YAML-defined covers (%d) and lights (%d)", yaml_covers, yaml_lights);
    ESP_LOGW(manager_tag_(), "  will be IGNORED. Remove cover/light entries from YAML.");
    ESP_LOGW(manager_tag_(), "══════════════════════════════════════════════════════════════");
  }

  init_slot_preferences_();

  // Restore slots from preferences
  size_t covers = 0, lights = 0, remotes = 0;

  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; ++i) {
      if (!cover_slots_[i].restore()) continue;
      cover_slots_[i].set_state_callback(make_cover_state_callback_());
      if (cover_slots_[i].activate(cover_slots_[i].config(), hub_)) {
        cover_slots_[i].sync_config_to_core();
        on_cover_activated_(&cover_slots_[i]);
        ++covers;
      }
    }
  }

  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; ++i) {
      if (!light_slots_[i].restore()) continue;
      light_slots_[i].set_state_callback(make_light_state_callback_());
      if (light_slots_[i].activate(light_slots_[i].config(), hub_)) {
        light_slots_[i].sync_config_to_core();
        on_light_activated_(&light_slots_[i]);
        ++lights;
      }
    }
  }

  if (remote_slots_ != nullptr) {
    for (size_t i = 0; i < max_remotes_; ++i) {
      if (!remote_slots_[i].restore()) continue;
      remote_slots_[i].set_state_callback(make_remote_state_callback_());
      if (remote_slots_[i].activate(remote_slots_[i].config())) {
        on_remote_activated_(&remote_slots_[i]);
        ++remotes;
      }
    }
  }

  ESP_LOGI(manager_tag_(), "%d covers, %d lights, %d remotes restored", covers, lights, remotes);
}

void NvsDeviceManagerBase::init_slot_preferences_() {
  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; ++i) {
      uint32_t hash = fnv1_hash(nvs_pref_key::COVER) + i;
      cover_slots_[i].set_preference(global_preferences->make_preference<NvsDeviceConfig>(hash));
    }
  }
  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; ++i) {
      uint32_t hash = fnv1_hash(nvs_pref_key::LIGHT) + i;
      light_slots_[i].set_preference(global_preferences->make_preference<NvsDeviceConfig>(hash));
    }
  }
  if (remote_slots_ != nullptr) {
    for (size_t i = 0; i < max_remotes_; ++i) {
      uint32_t hash = fnv1_hash(nvs_pref_key::REMOTE) + i;
      remote_slots_[i].set_preference(global_preferences->make_preference<NvsDeviceConfig>(hash));
    }
  }
}

void NvsDeviceManagerBase::loop() {
  uint32_t now = millis();

  loop_hook_();

  if (cover_slots_ != nullptr) {
    for (size_t i = 0; i < max_covers_; ++i) {
      if (cover_slots_[i].is_active()) {
        cover_slots_[i].loop(now);
      }
    }
  }

  if (light_slots_ != nullptr) {
    for (size_t i = 0; i < max_lights_; ++i) {
      if (light_slots_[i].is_active()) {
        light_slots_[i].loop(now);
      }
    }
  }
}

void NvsDeviceManagerBase::dump_config() {
  ESP_LOGCONFIG(manager_tag_(), "NVS Device Manager:");
  ESP_LOGCONFIG(manager_tag_(), "  Max covers: %d", max_covers_);
  ESP_LOGCONFIG(manager_tag_(), "  Max lights: %d", max_lights_);
  ESP_LOGCONFIG(manager_tag_(), "  Max remotes: %d", max_remotes_);
}

// ═══════════════════════════════════════════════════════════════════════════════
// RF Packet Handler
// ═══════════════════════════════════════════════════════════════════════════════

void NvsDeviceManagerBase::on_rf_packet(const RfPacketInfo &pkt) {
  if (packet::is_command_packet(pkt.type)) {
    track_remote_(pkt);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Device CRUD
// ═══════════════════════════════════════════════════════════════════════════════

bool NvsDeviceManagerBase::upsert_device(const NvsDeviceConfig &config) {
  if (config.is_cover()) {
    auto *slot = find_active_cover_(config.dst_address);
    if (slot != nullptr) {
      slot->update_config(config);
      on_cover_updated_(slot);
      slot->sync_config_to_core();
    } else {
      slot = find_free_cover_slot_();
      if (slot == nullptr) {
        ESP_LOGE(manager_tag_(), "No free cover slot for 0x%06x", config.dst_address);
        return false;
      }
      slot->set_state_callback(make_cover_state_callback_());
      if (!slot->activate(config, hub_)) {
        ESP_LOGW(manager_tag_(), "Failed to activate cover 0x%06x", config.dst_address);
        return false;
      }
      slot->sync_config_to_core();
      (void)slot->save_config();
      on_cover_activated_(slot);
    }

    notify_crud_upserted_(slot->config());
    return true;
  }

  if (config.is_light()) {
    auto *slot = find_active_light_(config.dst_address);
    if (slot != nullptr) {
      slot->update_config(config);
      on_light_updated_(slot);
      slot->sync_config_to_core();
    } else {
      slot = find_free_light_slot_();
      if (slot == nullptr) {
        ESP_LOGE(manager_tag_(), "No free light slot for 0x%06x", config.dst_address);
        return false;
      }
      slot->set_state_callback(make_light_state_callback_());
      if (!slot->activate(config, hub_)) {
        ESP_LOGW(manager_tag_(), "Failed to activate light 0x%06x", config.dst_address);
        return false;
      }
      slot->sync_config_to_core();
      (void)slot->save_config();
      on_light_activated_(slot);
    }

    notify_crud_upserted_(slot->config());
    return true;
  }

  if (config.is_remote()) {
    auto *slot = find_active_remote_(config.dst_address);
    if (slot != nullptr) {
      slot->update_config(config);
      on_remote_updated_(slot);
    } else {
      slot = find_free_remote_slot_();
      if (slot == nullptr) {
        ESP_LOGE(manager_tag_(), "No free remote slot for 0x%06x", config.dst_address);
        return false;
      }
      slot->set_state_callback(make_remote_state_callback_());
      if (!slot->activate(config)) {
        ESP_LOGW(manager_tag_(), "Failed to activate remote 0x%06x", config.dst_address);
        return false;
      }
      (void)slot->save_config();
      on_remote_activated_(slot);
    }

    notify_crud_upserted_(slot->config());
    return true;
  }

  ESP_LOGW(manager_tag_(), "upsert_device: unknown device type %d", static_cast<int>(config.type));
  return false;
}

bool NvsDeviceManagerBase::remove_device(DeviceType type, uint32_t dst_address) {
  bool removed = false;

  switch (type) {
    case DeviceType::COVER: {
      auto *slot = find_active_cover_(dst_address);
      if (slot == nullptr) return false;
      on_cover_deactivating_(slot);
      slot->deactivate();
      removed = true;
      break;
    }
    case DeviceType::LIGHT: {
      auto *slot = find_active_light_(dst_address);
      if (slot == nullptr) return false;
      on_light_deactivating_(slot);
      slot->deactivate();
      removed = true;
      break;
    }
    case DeviceType::REMOTE: {
      auto *slot = find_active_remote_(dst_address);
      if (slot == nullptr) return false;
      on_remote_deactivating_(slot);
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

void NvsDeviceManagerBase::track_remote_(const RfPacketInfo &pkt) {
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
    ESP_LOGV(manager_tag_(), "No free remote slot for new remote 0x%06x", remote_addr);
    return;
  }

  NvsDeviceConfig cfg{};
  cfg.type = DeviceType::REMOTE;
  cfg.dst_address = remote_addr;
  snprintf(cfg.name, sizeof(cfg.name), DEFAULT_REMOTE_NAME_FMT, remote_addr);

  slot->set_state_callback(make_remote_state_callback_());
  slot->activate(cfg);
  slot->update_from_packet(pkt.timestamp_ms, pkt.rssi, pkt.channel, pkt.command, pkt.dst);

  (void)slot->save_config();
  on_remote_activated_(slot);

  ESP_LOGI(manager_tag_(), "Auto-discovered remote 0x%06x", remote_addr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Slot Management
// ═══════════════════════════════════════════════════════════════════════════════

EleroDynamicCover *NvsDeviceManagerBase::find_free_cover_slot_() {
  if (cover_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_covers_; ++i) {
    if (!cover_slots_[i].is_active()) return &cover_slots_[i];
  }
  return nullptr;
}

EleroDynamicLight *NvsDeviceManagerBase::find_free_light_slot_() {
  if (light_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_lights_; ++i) {
    if (!light_slots_[i].is_active()) return &light_slots_[i];
  }
  return nullptr;
}

EleroRemoteControl *NvsDeviceManagerBase::find_free_remote_slot_() {
  if (remote_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_remotes_; ++i) {
    if (!remote_slots_[i].is_active()) return &remote_slots_[i];
  }
  return nullptr;
}

EleroDynamicCover *NvsDeviceManagerBase::find_active_cover_(uint32_t addr) {
  if (cover_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_covers_; ++i) {
    if (cover_slots_[i].is_active() && cover_slots_[i].get_blind_address() == addr) {
      return &cover_slots_[i];
    }
  }
  return nullptr;
}

EleroDynamicLight *NvsDeviceManagerBase::find_active_light_(uint32_t addr) {
  if (light_slots_ == nullptr) return nullptr;
  for (size_t i = 0; i < max_lights_; ++i) {
    if (light_slots_[i].is_active() && light_slots_[i].get_blind_address() == addr) {
      return &light_slots_[i];
    }
  }
  return nullptr;
}

EleroRemoteControl *NvsDeviceManagerBase::find_active_remote_(uint32_t addr) {
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

void NvsDeviceManagerBase::notify_crud_(const char *event, const char *json_str) {
  if (crud_callback_) {
    crud_callback_(event, json_str);
  }
}

void NvsDeviceManagerBase::notify_crud_(const char *event, uint32_t addr, const char *device_type) {
  std::string resp = json::build_json([&](JsonObject root) {
    root["address"] = hex_str(addr);
    root["device_type"] = device_type;
  });
  notify_crud_(event, resp.c_str());
}

void NvsDeviceManagerBase::notify_crud_upserted_(const NvsDeviceConfig &config) {
  std::string resp = json::build_json([&](JsonObject root) {
    root["address"] = hex_str(config.dst_address);
    root["device_type"] = device_type_str(config.type);
    root["name"] = config.name;
    root["enabled"] = config.is_enabled();
    root["updated_at"] = config.updated_at;
    if (!config.is_remote()) {
      root["channel"] = config.channel;
      root["remote"] = hex_str(config.src_address);
    }
    if (config.is_cover()) {
      root["open_ms"] = config.open_duration_ms;
      root["close_ms"] = config.close_duration_ms;
      root["poll_ms"] = config.poll_interval_ms;
      root["supports_tilt"] = config.supports_tilt != 0;
    }
    if (config.is_light()) {
      root["dim_ms"] = config.dim_duration_ms;
    }
  });
  notify_crud_(crud_event::DEVICE_UPSERTED, resp.c_str());
}

}  // namespace elero
}  // namespace esphome
