#include <algorithm>
#include <cinttypes>
#include "learn_in_manager.h"

namespace esphome::elero {

static const char *const TAG = "elero.learn_in";

const char *learn_in_state_str(LearnInState state) {
  switch (state) {
    case LearnInState::IDLE:
      return "idle";
    case LearnInState::PROGRAMMING:
      return "programming";
    case LearnInState::WAIT_UP:
      return "wait_up";
    case LearnInState::CONFIRMING_UP:
      return "confirming_up";
    case LearnInState::WAIT_DOWN:
      return "wait_down";
    case LearnInState::CONFIRMING_DOWN:
      return "confirming_down";
    case LearnInState::COMPLETE:
      return "complete";
    case LearnInState::FAILED:
      return "failed";
    case LearnInState::CANCELLED:
      return "cancelled";
    case LearnInState::TIMED_OUT:
      return "timed_out";
  }
  return "unknown";
}

LearnInState LearnInManager::state() const {
  if (const auto *step = std::get_if<Sending>(&state_)) return step->phase;
  return std::get<LearnInState>(state_);
}

bool LearnInManager::is_active() const {
  return is_busy() || state() == LearnInState::WAIT_UP || state() == LearnInState::WAIT_DOWN;
}

bool LearnInManager::start(const LearnInStartRequest &request) {
  if (is_active()) {
    ESP_LOGW(TAG, "Cannot start learn-in while state=%s", learn_in_state_str(state()));
    return false;
  }
  if (request.src_addr == 0) {
    ESP_LOGW(TAG, "Learn-in start rejected: src_addr missing");
    return false;
  }
  if (request.channel == 0) {
    ESP_LOGW(TAG, "Learn-in start rejected: channel missing");
    return false;
  }
  if (request.packets == 0) {
    ESP_LOGW(TAG, "Learn-in start rejected: packets must be > 0");
    return false;
  }

  command_.counter = 1;
  command_.src_addr = request.src_addr;
  command_.channel = request.channel;
  packets_per_step_ = request.packets;
  session_start_ms_ = get_time_provider().millis();
  session_timeout_ms_ = request.session_timeout_ms;
  begin_step_(packet::command::PROGRAM, LearnInState::PROGRAMMING, LearnInState::WAIT_UP);
  return true;
}

bool LearnInManager::confirm_up() {
  if (state() != LearnInState::WAIT_UP) {
    ESP_LOGW(TAG, "confirm_up rejected in state=%s", learn_in_state_str(state()));
    return false;
  }
  begin_step_(packet::command::UP, LearnInState::CONFIRMING_UP, LearnInState::WAIT_DOWN);
  return true;
}

bool LearnInManager::confirm_down() {
  if (state() != LearnInState::WAIT_DOWN) {
    ESP_LOGW(TAG, "confirm_down rejected in state=%s", learn_in_state_str(state()));
    return false;
  }
  begin_step_(packet::command::DOWN, LearnInState::CONFIRMING_DOWN, LearnInState::COMPLETE);
  return true;
}

void LearnInManager::begin_step_(uint8_t command, LearnInState phase, LearnInState on_success) {
  command_.payload[4] = command;
  if (phase == LearnInState::PROGRAMMING) {
    command_.type = packet::msg_type::PROGRAM;
    command_.type2 = packet::program::TYPE2;
    command_.hop = packet::program::HOP;
  } else {
    command_.type = packet::msg_type::BUTTON;
    command_.type2 = packet::button::TYPE2;
    command_.hop = packet::button::HOP;
  }
  state_ = Sending{phase, on_success, get_time_provider().millis()};
  ESP_LOGI(TAG, "Learn-in queued state=%s cmd=0x%02x src=0x%06" PRIx32 " ch=%u",
           learn_in_state_str(phase), command, command_.src_addr, command_.channel);
}

void LearnInManager::cancel() {
  if (state() == LearnInState::IDLE) return;
  finish_(LearnInState::CANCELLED);
  ESP_LOGI(TAG, "Learn-in session cancelled");
}

void LearnInManager::finish_(LearnInState state) {
  invalidate_tx_attempt();
  state_ = state;
}

bool LearnInManager::time_reached_(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

void LearnInManager::on_tx_complete(bool success) {
  auto *step = std::get_if<Sending>(&state_);
  if (step == nullptr || step->next_attempt_ms) return;

  const uint32_t now = get_time_provider().millis();
  if (success) {
    step->retries = 0;
    if (++step->sent_packets < packets_per_step_) {
      step->next_attempt_ms = now + packet::button::INTER_PACKET_MS;
      return;
    }

    command_.counter = command_.counter >= packet::limits::COUNTER_MAX ? 1 : command_.counter + 1;
    finish_(step->on_success);
    ESP_LOGI(TAG, "Learn-in step complete cmd=0x%02x -> state=%s",
             command_.payload[4], learn_in_state_str(state()));
    return;
  }

  if (++step->retries > packet::limits::SEND_RETRIES) {
    ESP_LOGE(TAG, "Learn-in failed in state=%s cmd=0x%02x",
             learn_in_state_str(step->phase), command_.payload[4]);
    finish_(LearnInState::FAILED);
    return;
  }
  const auto shift = std::min<uint8_t>(step->retries, 3);
  const auto backoff_ms = std::min<uint32_t>(packet::button::INTER_PACKET_MS << shift,
                                             packet::timing::MAX_BACKOFF_MS);
  step->next_attempt_ms = now + backoff_ms;
  ESP_LOGW(TAG, "Learn-in retry %u/%u for cmd=0x%02x", step->retries,
           packet::limits::SEND_RETRIES, command_.payload[4]);
}

}  // namespace esphome::elero
