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

bool LearnInManager::start(const LearnInStartRequest &request) {
  if (is_active() || is_busy()) {
    ESP_LOGW(TAG, "Cannot start learn-in while state=%s", learn_in_state_str(state_));
    return false;
  }
  if (!prime_session_(request)) {
    return false;
  }
  return queue_step_(packet::command::PROGRAM, LearnInState::PROGRAMMING);
}

bool LearnInManager::confirm_up() {
  if (state_ != LearnInState::WAIT_UP || is_busy()) {
    ESP_LOGW(TAG, "confirm_up rejected in state=%s", learn_in_state_str(state_));
    return false;
  }
  return queue_step_(packet::command::UP, LearnInState::CONFIRMING_UP);
}

bool LearnInManager::confirm_down() {
  if (state_ != LearnInState::WAIT_DOWN || is_busy()) {
    ESP_LOGW(TAG, "confirm_down rejected in state=%s", learn_in_state_str(state_));
    return false;
  }
  return queue_step_(packet::command::DOWN, LearnInState::CONFIRMING_DOWN);
}

void LearnInManager::cancel() {
  if (state_ == LearnInState::IDLE && !is_busy()) {
    return;
  }
  reset_transport_();
  state_ = LearnInState::CANCELLED;
  ESP_LOGI(TAG, "Learn-in session cancelled");
}

void LearnInManager::on_tx_complete(bool success) {
  if (!tx_pending_) {
    return;
  }

  tx_pending_ = false;
  uint32_t now = get_time_provider().millis();

  if (success) {
    retries_ = 0;
    ++sent_packets_;

    if (sent_packets_ < packets_per_step_) {
      next_attempt_ms_ = now + packet::button::INTER_PACKET_MS;
      return;
    }

    uint8_t completed_step = queued_step_;
    reset_transport_();
    increase_counter_();

    switch (state_) {
      case LearnInState::PROGRAMMING:
        state_ = LearnInState::WAIT_UP;
        break;
      case LearnInState::CONFIRMING_UP:
        state_ = LearnInState::WAIT_DOWN;
        break;
      case LearnInState::CONFIRMING_DOWN:
        state_ = LearnInState::COMPLETE;
        break;
      default:
        state_ = LearnInState::FAILED;
        break;
    }

    ESP_LOGI(TAG, "Learn-in step complete cmd=0x%02x -> state=%s", completed_step,
             learn_in_state_str(state_));
    return;
  }

  ++retries_;
  if (retries_ > packet::limits::SEND_RETRIES) {
    fail_session_();
    return;
  }

  next_attempt_ms_ = now + calculate_backoff_ms_();
  ESP_LOGW(TAG, "Learn-in retry %u/%u for cmd=0x%02x", retries_,
           packet::limits::SEND_RETRIES, queued_step_);
}

bool LearnInManager::is_active() const {
  return !is_terminal_() && state_ != LearnInState::IDLE;
}

bool LearnInManager::time_reached_(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

uint32_t LearnInManager::calculate_backoff_ms_() const {
  uint8_t shift = (retries_ < 4) ? retries_ : 3;
  uint32_t backoff_ms = packet::button::INTER_PACKET_MS << shift;
  return (backoff_ms > packet::timing::MAX_BACKOFF_MS) ? packet::timing::MAX_BACKOFF_MS
                                                        : backoff_ms;
}

bool LearnInManager::is_terminal_() const {
  switch (state_) {
    case LearnInState::COMPLETE:
    case LearnInState::FAILED:
    case LearnInState::CANCELLED:
    case LearnInState::TIMED_OUT:
      return true;
    case LearnInState::IDLE:
    case LearnInState::PROGRAMMING:
    case LearnInState::WAIT_UP:
    case LearnInState::CONFIRMING_UP:
    case LearnInState::WAIT_DOWN:
    case LearnInState::CONFIRMING_DOWN:
      return false;
  }
  return true;
}

bool LearnInManager::prime_session_(const LearnInStartRequest &request) {
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

  reset_all_();
  command_.counter = 1;
  command_.dst_addr = 0;
  command_.src_addr = request.src_addr;
  command_.channel = request.channel;
  command_.type = packet::msg_type::PROGRAM;
  command_.type2 = packet::program::TYPE2;
  command_.hop = packet::program::HOP;
  command_.payload[0] = 0;
  command_.payload[1] = 0;
  packets_per_step_ = request.packets;
  session_deadline_ms_ = get_time_provider().millis() + request.session_timeout_ms;
  return true;
}

bool LearnInManager::queue_step_(uint8_t cmd_byte, LearnInState pending_state) {
  if (cmd_byte == packet::command::INVALID) {
    return false;
  }
  queued_step_ = cmd_byte;
  sent_packets_ = 0;
  retries_ = 0;
  next_attempt_ms_ = 0;
  if (pending_state == LearnInState::PROGRAMMING) {
    command_.type = packet::msg_type::PROGRAM;
    command_.type2 = packet::program::TYPE2;
    command_.hop = packet::program::HOP;
  } else {
    command_.type = packet::msg_type::BUTTON;
    command_.type2 = packet::button::TYPE2;
    command_.hop = packet::button::HOP;
  }
  state_ = pending_state;
  ESP_LOGI(TAG, "Learn-in queued state=%s cmd=0x%02x src=0x%06x ch=%u",
           learn_in_state_str(state_), cmd_byte, command_.src_addr, command_.channel);
  return true;
}

void LearnInManager::fail_session_() {
  ESP_LOGE(TAG, "Learn-in failed in state=%s cmd=0x%02x", learn_in_state_str(state_),
           queued_step_);
  reset_transport_();
  state_ = LearnInState::FAILED;
}

void LearnInManager::increase_counter_() {
  if (command_.counter >= packet::limits::COUNTER_MAX) {
    command_.counter = 1;
  } else {
    ++command_.counter;
  }
}

void LearnInManager::reset_transport_() {
  this->invalidate_tx_attempt();
  queued_step_ = 0;
  sent_packets_ = 0;
  retries_ = 0;
  next_attempt_ms_ = 0;
  tx_pending_ = false;
}

void LearnInManager::reset_all_() {
  reset_transport_();
  packets_per_step_ = packet::program::PACKETS;
  session_deadline_ms_ = 0;
  state_ = LearnInState::IDLE;
}

}  // namespace esphome::elero
