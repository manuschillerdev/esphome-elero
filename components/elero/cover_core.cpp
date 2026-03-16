/// @file cover_core.cpp
/// @brief Pure C++ cover logic implementation.

#include "cover_core.h"
#include "nvs_config.h"

namespace esphome {
namespace elero {

void CoverCore::sync_from_nvs_config(const NvsDeviceConfig &cfg) {
  config.open_duration_ms = cfg.open_duration_ms;
  config.close_duration_ms = cfg.close_duration_ms;
  config.poll_interval_ms = cfg.poll_interval_ms;
  config.supports_tilt = cfg.supports_tilt != 0;
}

void CoverCore::apply_runtime_settings(uint32_t open_dur_ms, uint32_t close_dur_ms, uint32_t poll_intvl_ms) {
  if (open_dur_ms != 0) config.open_duration_ms = open_dur_ms;
  if (close_dur_ms != 0) config.close_duration_ms = close_dur_ms;
  if (poll_intvl_ms != 0) config.poll_interval_ms = poll_intvl_ms;
}

CoverCore::RxStateResult CoverCore::on_rx_state(uint8_t state, uint32_t now) {
  auto result = packet::map_cover_state(state);

  float new_pos = (result.position >= 0.0f) ? result.position : position;
  float new_tilt = result.tilt;
  auto new_op = static_cast<Operation>(result.operation);

  // RF can start movement UNLESS we explicitly stopped recently.
  // After a STOP command, ignore transient "still moving" RF responses
  // for a short cooldown period. Otherwise, allow RF to start movement
  // (e.g. when a physical remote sends a command we didn't see).
  bool blocked = false;
  if (operation == Operation::IDLE && new_op != Operation::IDLE) {
    if (idle_from_stop && (now - idle_since_ms) < packet::timing::POST_STOP_COOLDOWN_MS) {
      blocked = true;
    }
  }
  Operation effective_op = blocked ? operation : new_op;

  // When RF brings us to IDLE (e.g. TOP/BOTTOM status), clear the stop flag
  if (new_op == Operation::IDLE && idle_from_stop) {
    idle_from_stop = false;
  }

  bool changed = (new_pos != position) || (effective_op != operation) || (new_tilt != tilt);

  // Track movement start when operation changes
  if (effective_op != operation) {
    if (effective_op != Operation::IDLE) {
      movement_start_ms = now;
      movement_start_pos = new_pos;
      last_direction = effective_op;
    }
  }

  position = new_pos;
  tilt = new_tilt;
  operation = effective_op;

  // Any status from the blind is fresh state — defer next poll.
  // This suppresses unnecessary CHECKs while the blind is actively broadcasting.
  last_poll_ms = now;

  // Clear response wait (but not if blocked by post-stop cooldown —
  // the blind's transient "still moving" response doesn't count).
  if (awaiting_response && !blocked) {
    awaiting_response = false;
  }

  return {changed, result.is_warning, result.warning_msg};
}

void CoverCore::recompute_position(uint32_t now) {
  if (operation == Operation::IDLE)
    return;

  float dir;
  float action_dur;
  switch (operation) {
    case Operation::OPENING:
      dir = 1.0f;
      action_dur = static_cast<float>(config.open_duration_ms);
      break;
    case Operation::CLOSING:
      dir = -1.0f;
      action_dur = static_cast<float>(config.close_duration_ms);
      break;
    default:
      return;
  }

  if (action_dur == 0.0f)
    return;

  float elapsed_ratio = static_cast<float>(now - movement_start_ms) / action_dur;
  position = movement_start_pos + dir * elapsed_ratio;
  position = std::clamp(position, 0.0f, 1.0f);
}

bool CoverCore::check_movement_timeout(uint32_t now) {
  if (operation == Operation::IDLE)
    return false;
  if ((now - movement_start_ms) >= packet::timing::TIMEOUT_MOVEMENT) {
    operation = Operation::IDLE;
    return true;
  }
  return false;
}

uint32_t CoverCore::effective_poll_interval() const {
  if (operation != Operation::IDLE) {
    return packet::timing::POLL_INTERVAL_MOVING;
  }
  return config.poll_interval_ms;
}

bool CoverCore::should_poll(uint32_t now) {
  // Response-wait: defer polling after command TX to keep radio in RX
  if (awaiting_response) {
    if ((now - last_command_ms) < packet::timing::RESPONSE_WAIT_MS) {
      return false;  // stay in RX, don't poll yet
    }
    awaiting_response = false;  // timeout, blind didn't respond
    return true;                // NOW send CHECK
  }
  if (immediate_poll)
    return true;
  uint32_t interval = effective_poll_interval();
  if (interval == 0 || interval == UINT32_MAX)
    return false;
  return (now - last_poll_ms) >= interval;
}

void CoverCore::mark_polled(uint32_t now) {
  last_poll_ms = now;
  // Every CHECK gets a response-wait window — stay in RX until blind responds
  // or RESPONSE_WAIT_MS expires. Prevents CHECK flooding while moving.
  awaiting_response = true;
  last_command_ms = now;
  // Note: immediate_poll must be cleared by caller
}

bool CoverCore::is_at_target() const {
  // Don't send stop for fully open/closed — the blind handles those endpoints
  if (target_position >= 1.0f || target_position <= 0.0f)
    return false;

  switch (operation) {
    case Operation::OPENING:
      return position >= target_position;
    case Operation::CLOSING:
      return position <= target_position;
    case Operation::IDLE:
    default:
      return false;
  }
}

void CoverCore::start_movement(Operation op, uint32_t now) {
  if (op == operation)
    return;

  operation = op;
  movement_start_pos = position;
  movement_start_ms = now;
  idle_from_stop = false;
  awaiting_response = true;
  last_command_ms = now;

  if (op == Operation::OPENING || op == Operation::CLOSING) {
    last_direction = op;
    tilt = 0.0f;
  }
}

void CoverCore::stop_movement(uint32_t now) {
  operation = Operation::IDLE;
  idle_from_stop = true;
  idle_since_ms = now;
  awaiting_response = true;
  last_command_ms = now;
}

}  // namespace elero
}  // namespace esphome
