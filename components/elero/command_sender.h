#pragma once

#include <cinttypes>
#include "elero_packet.h"
#include "time_provider.h"
#include "tx_client.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <deque>
#include <span>

namespace esphome {
namespace elero {

/// Non-blocking command queue and transmission logic for Elero cover and light components.
///
/// Movement uses 0x44 button packets; targeted STOP/CHECK uses 0x6a.
/// Each queued request owns its command settings and recipients.
///
/// State machine:
///   IDLE ──enqueue()──▶ WAIT_DELAY ──request_tx()──▶ TX_PENDING
///                           ▲                            │
///                           └────on_tx_complete()────────┘
class CommandSender : public TxClient {
 public:
  enum class State : uint8_t {
    IDLE,
    WAIT_DELAY,
    TX_PENDING,
  };

  CommandSender() = default;

  template<typename Hub>
  void process_queue(uint32_t now, Hub *parent, const char *tag) {
    this->log_tag_ = tag;

    switch (this->state_) {
      case State::IDLE:
        if (this->command_queue_.empty()) {
          return;
        }
        this->state_ = State::WAIT_DELAY;
        [[fallthrough]];

      case State::WAIT_DELAY:
        if (!this->time_reached_(now, this->next_attempt_ms_)) {
          return;
        }

        if (this->command_queue_.empty()) {
          this->state_ = State::IDLE;
          return;
        }

        {
          auto cmd = this->command_queue_.front().command;
          cmd.counter = this->command_.counter;
          if (parent->request_tx(this, cmd)) {
            this->state_ = State::TX_PENDING;
            ESP_LOGV(tag, "TX queued for 0x%06" PRIx32 " cmd=0x%02x, packet %d/%d",
                     cmd.dst_addr, cmd.payload[4],
                     this->send_packets_ + 1, this->command_queue_.front().packets);
          } else {
            ESP_LOGVV(tag, "Radio busy for 0x%06" PRIx32 ", will retry", cmd.dst_addr);
          }
        }
        break;

      case State::TX_PENDING:
        // Queue waiting is not a hardware attempt. Driver FSMs own TX timeouts
        // and the RF task retains completion until Core 1 can receive it.
        break;
    }
  }

  void on_tx_complete(bool success) override {
    if (this->state_ != State::TX_PENDING) {
      ESP_LOGD(this->log_tag_, "Ignoring stale on_tx_complete for 0x%06" PRIx32 " (state=%d, success=%d)",
               this->command_.dst_addr, static_cast<int>(this->state_), success);
      return;
    }

    uint32_t now = get_time_provider().millis();
    this->last_tx_time_ = now;

    if (success) {
      this->send_retries_ = 0;
      ++this->send_packets_;

      uint8_t target_packets = this->command_queue_.empty()
          ? packet::button::PACKETS
          : this->command_queue_.front().packets;
      if (this->send_packets_ >= target_packets) {
        ESP_LOGV(this->log_tag_, "Command 0x%02x to 0x%06" PRIx32 " complete (%d packets)",
                 this->command_queue_.front().command.payload[4], this->command_.dst_addr, this->send_packets_);
        this->advance_queue_();
      } else {
        this->next_attempt_ms_ = now + packet::button::INTER_PACKET_MS;
        this->state_ = State::WAIT_DELAY;
      }
    } else {
      ++this->send_retries_;
      ESP_LOGD(this->log_tag_, "TX retry %d/%d for 0x%06" PRIx32,
               this->send_retries_, packet::limits::SEND_RETRIES, this->command_.dst_addr);

      if (this->send_retries_ > packet::limits::SEND_RETRIES) {
        ESP_LOGE(this->log_tag_, "Max retries for 0x%06" PRIx32 ", dropping command 0x%02x",
                 this->command_.dst_addr, this->command_queue_.front().command.payload[4]);
        this->advance_queue_();
      } else {
        uint32_t backoff_ms = this->calculate_backoff_ms_();
        ESP_LOGD(this->log_tag_, "Backoff %" PRIu32 "ms before retry", backoff_ms);
        this->next_attempt_ms_ = now + backoff_ms;
        this->state_ = State::WAIT_DELAY;
      }
    }
  }

  /// Enqueue a command byte for transmission.
  /// Callers are responsible for appending follow-up commands:
  ///   - Covers: enqueue(UP/DOWN) + enqueue(CHECK) to get "moving" status
  ///   - Lights: enqueue(UP/DOWN) + enqueue(RELEASE) to stop dimming
  /// Consecutive identical requests (including recipients) are collapsed.
  /// @param cmd_byte The command byte to send
  /// @param packets Number of RF packets (default: 3 for button protocol)
  /// @param type Packet type: BUTTON (0x44) or COMMAND (0x6a)
  /// @param channels Optional group selectors, copied into the request
  /// @param priority Preempt pending work using the reserved STOP slot
  /// @return true if queued successfully, false if queue is full
  [[nodiscard]] bool enqueue(uint8_t cmd_byte,
                             uint8_t packets = packet::button::PACKETS,
                             uint8_t type = packet::msg_type::BUTTON,
                             std::span<const uint8_t> channels = {},
                             bool priority = false) {
    if (channels.size() > packet::GROUP_MAX_DESTS) return false;
    QueueEntry entry{this->command_, packets};
    auto &cmd = entry.command;
    cmd.counter = 0;  // Assigned at transmission time, shared by all repetitions.
    cmd.payload[4] = cmd_byte;
    cmd.type = type;
    cmd.type2 = type == packet::msg_type::BUTTON ? packet::button::TYPE2 : packet::defaults::TYPE2;
    cmd.hop = type == packet::msg_type::BUTTON ? packet::button::HOP : packet::defaults::HOP;
    cmd.num_dests = static_cast<uint8_t>(channels.size());
    std::fill(std::begin(cmd.dest_channels), std::end(cmd.dest_channels), 0);
    std::copy(channels.begin(), channels.end(), cmd.dest_channels);
    if (!channels.empty()) cmd.channel = channels.front();
    if (!priority && !this->command_queue_.empty() && this->command_queue_.back() == entry) {
      return true;
    }
    // One reserved slot lets STOP preempt a full movement queue. The registry
    // cancels the previous STOP for this device before submitting another.
    if (this->command_queue_.size() >= packet::limits::MAX_COMMAND_QUEUE + (priority ? 1u : 0u)) {
      ESP_LOGW("elero.tx", "Command queue full, dropping cmd 0x%02x", cmd_byte);
      return false;
    }
    if (priority) {
      this->reset_attempt_();
      this->command_queue_.push_front(entry);
    } else {
      this->command_queue_.push_back(entry);
    }

    if (this->state_ == State::IDLE) {
      this->state_ = State::WAIT_DELAY;
    }
    return true;
  }

  void clear_queue() {
    this->command_queue_.clear();
    this->reset_attempt_();
  }

  /// Remove one target from queued work, preserving other group channels.
  /// A frame already handed to the RF task precedes the subsequent STOP;
  /// invalidating its completion prevents any further repetitions for it.
  void cancel_target(uint32_t src, uint8_t channel, uint32_t dst) {
    for (auto it = this->command_queue_.begin(); it != this->command_queue_.end();) {
      auto &cmd = it->command;
      bool changed = false;
      bool remove = false;
      if (cmd.type == packet::msg_type::BUTTON && cmd.src_addr == src) {
        if (cmd.num_dests == 0) {
          changed = remove = cmd.channel == channel;
        } else {
          auto *end = std::remove(cmd.dest_channels, cmd.dest_channels + cmd.num_dests, channel);
          const auto count = static_cast<uint8_t>(end - cmd.dest_channels);
          changed = count != cmd.num_dests;
          cmd.num_dests = count;
          remove = count == 0;
          if (count != 0) cmd.channel = cmd.dest_channels[0];
          std::fill(end, std::end(cmd.dest_channels), 0);
        }
      } else if (cmd.type == packet::msg_type::COMMAND) {
        changed = remove = cmd.dst_addr == dst;
      }
      if (changed && it == this->command_queue_.begin()) this->reset_attempt_();
      if (remove) it = this->command_queue_.erase(it);
      else ++it;
    }
    if (this->state_ == State::IDLE && !this->command_queue_.empty()) this->state_ = State::WAIT_DELAY;
  }

  State state() const { return this->state_; }
  bool is_busy() const { return this->state_ != State::IDLE || !this->command_queue_.empty(); }
  bool has_pending_commands() const { return !this->command_queue_.empty(); }
  size_t queue_size() const { return this->command_queue_.size(); }
  EleroCommand &command() { return this->command_; }
  const EleroCommand &command() const { return this->command_; }

 private:
  void reset_attempt_() {
    if (this->state_ == State::TX_PENDING || this->send_packets_ != 0) this->increase_counter_();
    this->send_packets_ = 0;
    this->send_retries_ = 0;
    this->last_tx_time_ = 0;
    this->next_attempt_ms_ = 0;

    this->invalidate_tx_attempt();
    this->state_ = State::IDLE;
  }

  static bool time_reached_(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
  }

  uint32_t calculate_backoff_ms_() const {
    uint8_t shift = (this->send_retries_ < 4) ? this->send_retries_ : 3;
    uint32_t backoff_ms = packet::button::INTER_PACKET_MS << shift;
    return (backoff_ms > packet::timing::MAX_BACKOFF_MS) ? packet::timing::MAX_BACKOFF_MS : backoff_ms;
  }

  void advance_queue_() {
    if (!this->command_queue_.empty()) {
      this->command_queue_.pop_front();
    }
    this->send_packets_ = 0;
    this->send_retries_ = 0;
    this->increase_counter_();
    this->state_ = this->command_queue_.empty() ? State::IDLE : State::WAIT_DELAY;
    if (this->state_ == State::WAIT_DELAY) {
      this->next_attempt_ms_ = this->last_tx_time_ + packet::button::INTER_PACKET_MS;
    }
  }

  void increase_counter_() {
    if (this->command_.counter >= packet::limits::COUNTER_MAX) {
      this->command_.counter = 1;
    } else {
      ++this->command_.counter;
    }
  }

  struct QueueEntry {
    EleroCommand command;
    uint8_t packets;
    bool operator==(const QueueEntry &) const = default;
  };

  EleroCommand command_{1, 0, 0, 0, 0, 0, 0, {0}};
  std::deque<QueueEntry> command_queue_;

  State state_{State::IDLE};
  uint32_t last_tx_time_{0};
  uint32_t next_attempt_ms_{packet::button::INTER_PACKET_MS};
  uint8_t send_packets_{0};
  uint8_t send_retries_{0};
  const char *log_tag_{"sender"};
};

}  // namespace elero
}  // namespace esphome
