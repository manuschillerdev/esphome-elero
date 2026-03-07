/// @file light_core.cpp
/// @brief Pure C++ light logic implementation.

#include "light_core.h"
#include "nvs_config.h"

namespace esphome {
namespace elero {

void LightCore::sync_from_nvs_config(const NvsDeviceConfig &cfg) {
  config.dim_duration_ms = cfg.dim_duration_ms;
}

bool LightCore::on_rx_state(uint8_t state) {
  if (state == packet::state::LIGHT_ON) {
    if (!is_on) {
      is_on = true;
      brightness = 1.0f;
      dim_direction = DimDirection::NONE;
      return true;
    }
  } else if (state == packet::state::LIGHT_OFF || state == packet::state::BOTTOM_TILT) {
    if (is_on) {
      is_on = false;
      brightness = 0.0f;
      dim_direction = DimDirection::NONE;
      return true;
    }
  }
  return false;
}

void LightCore::recompute_brightness(uint32_t now) {
  if (dim_direction == DimDirection::NONE || config.dim_duration_ms == 0)
    return;

  float dir = (dim_direction == DimDirection::UP) ? 1.0f : -1.0f;
  brightness += dir * static_cast<float>(now - last_recompute_time) /
                static_cast<float>(config.dim_duration_ms);
  brightness = std::clamp(brightness, 0.0f, 1.0f);
  last_recompute_time = now;
}

bool LightCore::is_at_target() const {
  if (dim_direction == DimDirection::NONE)
    return false;
  if (dim_direction == DimDirection::UP) {
    return brightness >= target_brightness;
  }
  return brightness <= target_brightness;
}

LightCore::WriteAction LightCore::compute_write_action(bool target_on, float new_brightness) const {
  WriteAction result{};

  if (!target_on) {
    result.command = command_off;
    return result;
  }

  // Light should be on
  if (config.dim_duration_ms == 0) {
    // No brightness support: just toggle on
    result.command = command_on;
    return result;
  }

  // Full brightness shortcut
  if (new_brightness >= 1.0f) {
    result.command = command_on;
    return result;
  }

  // If currently off, need to turn on first (caller will chain dim after)
  if (brightness < BRIGHTNESS_EPSILON) {
    result.command = command_on;
    // After turning on, brightness becomes 1.0 — caller needs to re-evaluate for dimming
    return result;
  }

  // Already on with some brightness — compute dim direction
  if (new_brightness > brightness + BRIGHTNESS_EPSILON) {
    result.command = command_dim_up;
    result.start_dimming = true;
    result.dim_dir = DimDirection::UP;
  } else if (new_brightness < brightness - BRIGHTNESS_EPSILON) {
    result.command = command_dim_down;
    result.start_dimming = true;
    result.dim_dir = DimDirection::DOWN;
  }
  // Within tolerance: no action needed

  return result;
}

void LightCore::apply_write_action(bool target_on, float new_brightness, const WriteAction &action, uint32_t now) {
  if (!target_on) {
    turn_off();
    return;
  }

  is_on = true;

  if (config.dim_duration_ms == 0) {
    brightness = 1.0f;
    return;
  }

  target_brightness = new_brightness;

  if (new_brightness >= 1.0f) {
    brightness = 1.0f;
    dim_direction = DimDirection::NONE;
    return;
  }

  // If was off, turn on sets brightness to 1.0
  if (brightness < BRIGHTNESS_EPSILON && !action.start_dimming) {
    brightness = 1.0f;
    // Don't start dimming here — caller will re-evaluate
    return;
  }

  if (action.start_dimming) {
    dim_direction = action.dim_dir;
    dimming_start = now;
    last_recompute_time = now;
  }
}

void LightCore::turn_off() {
  is_on = false;
  brightness = 0.0f;
  dim_direction = DimDirection::NONE;
}

}  // namespace elero
}  // namespace esphome
