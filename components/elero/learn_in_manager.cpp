#include <algorithm>
#include <cinttypes>
#include "learn_in_manager.h"

namespace esphome::elero {

static const char *const TAG = "elero.learn_in";

const char *learn_in_state_str(LearnInState state) {
  switch (state) {
    case LearnInState::OPENING_STANDBY: return "opening_standby";
    case LearnInState::STANDBY_SENT: return "standby_sent";
    case LearnInState::TRAIN_P1: return "train_p1";
    case LearnInState::TRAIN_P2: return "train_p2";
    case LearnInState::TRAIN_P3: return "train_p3";
    case LearnInState::WATCH_MOVEMENT: return "watch_movement";
    case LearnInState::TRAIN_UP: return "train_up";
    case LearnInState::TRAIN_DOWN: return "train_down";
    case LearnInState::ACKNOWLEDGING: return "acknowledging";
    case LearnInState::ACK_SENT: return "ack_sent";
    case LearnInState::REMOVING_LINK: return "removing_link";
    case LearnInState::REMOVAL_SENT: return "removal_sent";
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
  return is_busy() || state() == LearnInState::WATCH_MOVEMENT || state() == LearnInState::WAIT_UP || state() == LearnInState::WAIT_DOWN;
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

  if (request.mode != LearnInMode::POWER_CYCLE &&
      (request.receiver == 0 || request.receiver > 0xffffff || request.src_addr > 0xffffff)) return false;

  command_ = {};
  mode_ = request.mode;
  confirmed_directions_ = 0;
  receiver_answered_ = false;
  command_.dst_addr = request.receiver;
  command_.counter = 1;
  command_.src_addr = request.src_addr;
  command_.channel = request.channel;
  packets_per_step_ = request.packets;
  session_start_ms_ = get_time_provider().millis();
  session_timeout_ms_ = request.session_timeout_ms;
  switch (mode_) {
    case LearnInMode::POWER_CYCLE:
      command_.payload[1] = packet::button::SINGLE_PAYLOAD_2;
      begin_step_(packet::command::PROGRAM, LearnInState::PROGRAMMING, LearnInState::WAIT_UP);
      break;
    case LearnInMode::OPEN_STANDBY: begin_training_step_(LearnInState::OPENING_STANDBY); break;
    case LearnInMode::TRAIN: begin_training_step_(LearnInState::TRAIN_P1); break;
    case LearnInMode::REMOVE_LINK: begin_training_step_(LearnInState::REMOVING_LINK); break;
  }
  return true;
}

bool LearnInManager::confirm_up() {
  if (state() == LearnInState::WATCH_MOVEMENT) {
    begin_training_step_(LearnInState::TRAIN_UP);
    return true;
  }
  if (state() != LearnInState::WAIT_UP) {
    ESP_LOGW(TAG, "confirm_up rejected in state=%s", learn_in_state_str(state()));
    return false;
  }
  begin_step_(packet::command::UP, LearnInState::CONFIRMING_UP, LearnInState::WAIT_DOWN);
  return true;
}

bool LearnInManager::confirm_down() {
  if (state() == LearnInState::WATCH_MOVEMENT) {
    begin_training_step_(LearnInState::TRAIN_DOWN);
    return true;
  }
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

void LearnInManager::begin_training_step_(LearnInState phase, uint32_t delay_ms) {
  using enum LearnInState;
  std::fill(std::begin(command_.payload), std::end(command_.payload), 0);
  command_.counter = next_training_counter_;
  next_training_counter_ = next_training_counter_ >= packet::limits::COUNTER_MAX ? 1 : next_training_counter_ + 1;
  command_.type = 0x78;
  command_.type2 = 0x10;
  command_.hop = 0;
  command_.payload[1] = 1;
  command_.programming_layout = ProgrammingLayout::TRAINING;
  LearnInState next = WATCH_MOVEMENT;
  switch (phase) {
    case OPENING_STANDBY:
      command_.type = packet::msg_type::COMMAND;
      command_.hop = 5;
      command_.payload[1] = 4;
      command_.payload[4] = 0xe1;
      next = STANDBY_SENT;
      break;
    case TRAIN_P1:
      command_.type = packet::msg_type::PROGRAM;
      command_.programming_layout = ProgrammingLayout::PADDED_SELECTOR;
      command_.payload[1] = 3;
      command_.payload[4] = 0x80;
      command_.payload[7] = 2;
      next = TRAIN_P2;
      break;
    case TRAIN_P2:
      command_.type = 0xf8;
      command_.programming_layout = ProgrammingLayout::ZERO_BLOCK;
      command_.payload[1] = 0;
      next = TRAIN_P3;
      break;
    case TRAIN_P3:
      command_.payload[0] = 4;
      command_.payload[7] = 2;
      break;
    case TRAIN_UP: command_.payload[4] = packet::command::UP; break;
    case TRAIN_DOWN: command_.payload[4] = packet::command::DOWN; break;
    case ACKNOWLEDGING:
      command_.payload[0] = 8;
      next = ACK_SENT;
      break;
    case REMOVING_LINK:
      command_.type = packet::msg_type::BUTTON_GROUP;
      command_.payload[4] = 0x92;
      command_.payload[5] = 8;
      next = REMOVAL_SENT;
      break;
    default: return;
  }
  state_ = Sending{phase, next, get_time_provider().millis() + delay_ms};
}

void LearnInManager::on_receiver_frame(std::span<const uint8_t> raw) {
  using namespace packet;
  // A D4 can arrive before the final TX repeat completes. Latch it in that step.
  const auto phase = state();
  const bool direction = phase == LearnInState::TRAIN_UP || phase == LearnInState::TRAIN_DOWN;
  if (mode_ != LearnInMode::TRAIN || (!direction && phase != LearnInState::WATCH_MOVEMENT)) return;
  const uint8_t pending = phase == LearnInState::TRAIN_UP ? 1 : phase == LearnInState::TRAIN_DOWN ? 2 : 0;
  const auto *step = std::get_if<Sending>(&state_);
  if (direction && step && step->next_attempt_ms && step->sent_packets == 0) return;
  if ((confirmed_directions_ | pending) != 3 || raw.size() < pkt_offset::FIRST_DEST + ADDR_SIZE + CC1101_APPEND_SIZE ||
      raw.size() != static_cast<size_t>(raw[0]) + PACKET_TOTAL_OVERHEAD ||
      !(raw.back() & cc1101_status::CRC_OK_BIT) || raw[pkt_offset::TYPE] != 0xd4 ||
      raw[pkt_offset::CHANNEL] != channel() || extract_addr(&raw[pkt_offset::SRC_ADDR]) != receiver() ||
      extract_addr(&raw[pkt_offset::FWD_ADDR]) != src_addr() || raw[pkt_offset::NUM_DESTS] != 1 || extract_addr(&raw[pkt_offset::FIRST_DEST]) != 0) return;

  receiver_answered_ = true;
  if (!direction) begin_training_step_(LearnInState::ACKNOWLEDGING);
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

    if (mode_ == LearnInMode::POWER_CYCLE)
      command_.counter = command_.counter >= packet::limits::COUNTER_MAX ? 1 : command_.counter + 1;
    const auto next = step->on_success;
    if (step->phase == LearnInState::TRAIN_UP) confirmed_directions_ |= 1;
    if (step->phase == LearnInState::TRAIN_DOWN) confirmed_directions_ |= 2;
    if (next == LearnInState::TRAIN_P2) begin_training_step_(next, 2100);
    else if (next == LearnInState::TRAIN_P3) begin_training_step_(next, 500);
    else if (next == LearnInState::WATCH_MOVEMENT && receiver_answered_)
      begin_training_step_(LearnInState::ACKNOWLEDGING);
    else finish_(next);
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
