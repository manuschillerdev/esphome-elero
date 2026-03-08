#include "dynamic_entity_base.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elero {

// ─── Persistence ───

bool DynamicEntityBase::restore() {
  NvsDeviceConfig cfg{};
  if (!pref_.load(&cfg) || !cfg.is_valid()) {
    return false;
  }
  config_ = cfg;
  return true;
}

bool DynamicEntityBase::save_config() {
  config_.updated_at = millis();
  if (!pref_.save(&config_)) {
    ESP_LOGE(entity_tag_(), "NVS save failed for 0x%06x", config_.dst_address);
    return false;
  }
  return true;
}

void DynamicEntityBase::clear_config() {
  NvsDeviceConfig empty{};
  pref_.save(&empty);
}

// ─── CommandSender configuration ───

void DynamicEntityBase::configure_sender_() {
  auto &cmd = sender_.command();
  cmd.dst_addr = config_.dst_address;
  cmd.src_addr = config_.src_address;
  cmd.channel = config_.channel;
  cmd.type = config_.type_byte;
  cmd.type2 = config_.type2;
  cmd.hop = config_.hop;
  cmd.payload[0] = config_.payload_1;
  cmd.payload[1] = config_.payload_2;
}

// ─── Activation ───

bool DynamicEntityBase::activate(const NvsDeviceConfig &config, Elero *parent) {
  if (active_) {
    ESP_LOGW(entity_tag_(), "Slot already active for 0x%06x", config_.dst_address);
    return false;
  }
  if (!config.is_valid() || !is_matching_type_(config)) {
    ESP_LOGE(entity_tag_(), "Invalid config for activation");
    return false;
  }

  config_ = config;
  parent_ = parent;
  configure_sender_();

  if (config.is_enabled() && parent != nullptr) {
    register_with_hub();
  }

  active_ = true;
  ESP_LOGI(entity_tag_(), "Activated %s '%s' at 0x%06x (enabled=%d)",
           entity_type_str_(), config_.name, config_.dst_address, config_.is_enabled());
  return true;
}

void DynamicEntityBase::deactivate() {
  if (!active_) return;
  ESP_LOGI(entity_tag_(), "Deactivating %s 0x%06x", entity_type_str_(), config_.dst_address);

  unregister_from_hub();
  clear_config();

  active_ = false;
  parent_ = nullptr;
  config_ = NvsDeviceConfig{};
  sender_.clear_queue();
  last_state_raw_ = 0;
  last_seen_ms_ = 0;
  last_rssi_ = 0.0f;
  reset_entity_state_();
}

void DynamicEntityBase::update_config(const NvsDeviceConfig &config) {
  if (!active_) return;

  if (registered_) {
    unregister_from_hub();
  }

  config_ = config;
  configure_sender_();
  (void)save_config();  // Best-effort; save_config logs on failure

  if (config.is_enabled() && parent_ != nullptr) {
    register_with_hub();
  }

  ESP_LOGI(entity_tag_(), "Updated %s '%s' at 0x%06x (enabled=%d)",
           entity_type_str_(), config_.name, config_.dst_address, config_.is_enabled());
}

void DynamicEntityBase::register_with_hub() {
  if (registered_ || parent_ == nullptr) return;
  do_register_();
  registered_ = true;
}

void DynamicEntityBase::unregister_from_hub() {
  if (!registered_ || parent_ == nullptr) return;
  do_unregister_();
  registered_ = false;
}

}  // namespace elero
}  // namespace esphome
