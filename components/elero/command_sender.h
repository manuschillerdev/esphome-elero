#pragma once

#include "elero_packet.h"
#include "time_provider.h"
#include "tx_client.h"
#include "esphome/core/log.h"
#include <queue>

namespace esphome {
namespace elero {

/// Non-blocking command queue and transmission logic for Elero cover and light components.
///
/// All commands are sent as 0x44 button packets (3x with 10ms gaps), matching
/// how physical Elero remotes transmit. The hub's build_tx_packet_() uses
/// the command template's type field to select the packet builder.
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

  static constexpr uint32_t TX_PENDING_TIMEOUT_MS = packet::timing::TX_PENDING_TIMEOUT;

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
        if ((now - this->last_tx_time_) < packet::button::INTER_PACKET_MS) {
          return;
        }

        if (this->command_queue_.empty()) {
          this->cancelled_ = false;
          this->state_ = State::IDLE;
          return;
        }

        {
          const auto &entry = this->command_queue_.front();
          this->command_.payload[4] = entry.cmd;
          this->command_.type = entry.type;
          if (entry.type == packet::msg_type::BUTTON) {
            this->command_.type2 = packet::button::TYPE2;
            this->command_.hop = packet::button::HOP;
          } else {
            this->command_.type2 = packet::defaults::TYPE2;
            this->command_.hop = packet::defaults::HOP;
          }
        }

        if (parent->request_tx(this, this->command_)) {
          this->state_ = State::TX_PENDING;
          this->tx_start_time_ = now;
          ESP_LOGV(tag, "TX started for 0x%06x cmd=0x%02x, packet %d/%d",
                   this->command_.dst_addr, this->command_.payload[4],
                   this->send_packets_ + 1, this->command_queue_.front().packets);
        } else {
          ESP_LOGVV(tag, "Radio busy for 0x%06x, will retry", this->command_.dst_addr);
        }
        break;

      case State::TX_PENDING:
        if ((now - this->tx_start_time_) > TX_PENDING_TIMEOUT_MS) {
          ESP_LOGW(tag, "TX_PENDING timeout for 0x%06x after %ums, treating as failure",
                   this->command_.dst_addr, TX_PENDING_TIMEOUT_MS);
          ++this->send_retries_;
          if (this->send_retries_ > packet::limits::SEND_RETRIES) {
            ESP_LOGE(tag, "Max retries for 0x%06x after timeout, dropping command 0x%02x",
                     this->command_.dst_addr, this->command_.payload[4]);
            this->advance_queue_();
          } else {
            uint32_t backoff_ms = this->calculate_backoff_ms_();
            this->last_tx_time_ = now + backoff_ms - packet::button::INTER_PACKET_MS;
            this->state_ = State::WAIT_DELAY;
          }
        }
        break;
    }
  }

  void on_tx_complete(bool success) override {
    if (this->state_ != State::TX_PENDING) {
      ESP_LOGD(this->log_tag_, "Ignoring stale on_tx_complete for 0x%06x (state=%d, success=%d)",
               this->command_.dst_addr, static_cast<int>(this->state_), success);
      return;
    }

    if (this->cancelled_) {
      ESP_LOGD(this->log_tag_, "TX for 0x%06x completed but was cancelled, ignoring",
               this->command_.dst_addr);
      this->cancelled_ = false;
      this->send_packets_ = 0;
      this->send_retries_ = 0;
      this->state_ = State::IDLE;
      return;
    }

    this->last_tx_time_ = get_time_provider().millis();

    if (success) {
      this->send_retries_ = 0;
      ++this->send_packets_;

      uint8_t target_packets = this->command_queue_.empty()
          ? packet::button::PACKETS
          : this->command_queue_.front().packets;
      if (this->send_packets_ >= target_packets) {
        ESP_LOGV(this->log_tag_, "Command 0x%02x to 0x%06x complete (%d packets)",
                 this->command_.payload[4], this->command_.dst_addr, this->send_packets_);
        this->advance_queue_();
      } else {
        this->state_ = State::WAIT_DELAY;
      }
    } else {
      ++this->send_retries_;
      ESP_LOGD(this->log_tag_, "TX retry %d/%d for 0x%06x",
               this->send_retries_, packet::limits::SEND_RETRIES, this->command_.dst_addr);

      if (this->send_retries_ > packet::limits::SEND_RETRIES) {
        ESP_LOGE(this->log_tag_, "Max retries for 0x%06x, dropping command 0x%02x",
                 this->command_.dst_addr, this->command_.payload[4]);
        this->advance_queue_();
      } else {
        uint32_t backoff_ms = this->calculate_backoff_ms_();
        ESP_LOGD(this->log_tag_, "Backoff %ums before retry", backoff_ms);
        this->last_tx_time_ = get_time_provider().millis() + backoff_ms - packet::button::INTER_PACKET_MS;
        this->state_ = State::WAIT_DELAY;
      }
    }
  }

  /// Enqueue a command byte for transmission.
  /// Callers are responsible for appending follow-up commands:
  ///   - Covers: enqueue(UP/DOWN) + enqueue(CHECK) to get "moving" status
  ///   - Lights: enqueue(UP/DOWN) + enqueue(RELEASE) to stop dimming
  /// Consecutive duplicate commands (same cmd + type) are collapsed.
  /// @param cmd_byte The command byte to send
  /// @param packets Number of RF packets (default: 3 for button protocol)
  /// @param type Packet type: BUTTON (0x44) or COMMAND (0x6a)
  /// @return true if queued successfully, false if queue is full
  [[nodiscard]] bool enqueue(uint8_t cmd_byte,
                             uint8_t packets = packet::button::PACKETS,
                             uint8_t type = packet::msg_type::BUTTON) {
    // Collapse consecutive duplicates (same cmd AND type)
    if (!this->command_queue_.empty() &&
        this->command_queue_.back().cmd == cmd_byte &&
        this->command_queue_.back().type == type) {
      return true;
    }
    if (this->command_queue_.size() >= packet::limits::MAX_COMMAND_QUEUE) {
      ESP_LOGW("elero.tx", "Command queue full, dropping cmd 0x%02x", cmd_byte);
      return false;
    }
    this->command_queue_.push({cmd_byte, packets, type});

    if (this->state_ == State::IDLE) {
      this->state_ = State::WAIT_DELAY;
    }
    return true;
  }

  void clear_queue() {
    this->command_queue_ = std::queue<QueueEntry>{};
    this->send_packets_ = 0;
    this->send_retries_ = 0;
    this->last_tx_time_ = 0;

    if (this->state_ == State::TX_PENDING) {
      // Cancelled TX won't call advance_queue_, so bump the counter now
      // to avoid the next command reusing the in-flight counter.
      this->increase_counter_();
      this->cancelled_ = true;
    } else {
      this->state_ = State::IDLE;
    }
  }

  State state() const { return this->state_; }
  bool is_busy() const { return this->state_ != State::IDLE || !this->command_queue_.empty(); }
  bool has_pending_commands() const { return !this->command_queue_.empty(); }
  size_t queue_size() const { return this->command_queue_.size(); }
  EleroCommand &command() { return this->command_; }
  const EleroCommand &command() const { return this->command_; }

 private:
  uint32_t calculate_backoff_ms_() const {
    uint8_t shift = (this->send_retries_ < 4) ? this->send_retries_ : 3;
    uint32_t backoff_ms = packet::button::INTER_PACKET_MS << shift;
    return (backoff_ms > packet::timing::MAX_BACKOFF_MS) ? packet::timing::MAX_BACKOFF_MS : backoff_ms;
  }

  void advance_queue_() {
    if (!this->command_queue_.empty()) {
      this->command_queue_.pop();
    }
    this->send_packets_ = 0;
    this->send_retries_ = 0;
    this->command_.num_dests = 0;  // Clear group fields after each command drains
    this->increase_counter_();
    this->state_ = this->command_queue_.empty() ? State::IDLE : State::WAIT_DELAY;
  }

  void increase_counter_() {
    if (this->command_.counter >= packet::limits::COUNTER_MAX) {
      this->command_.counter = 1;
    } else {
      ++this->command_.counter;
    }
  }

  struct QueueEntry { uint8_t cmd; uint8_t packets; uint8_t type; };

  EleroCommand command_{1, 0, 0, 0, 0, 0, 0, {0}};
  std::queue<QueueEntry> command_queue_;

  State state_{State::IDLE};
  uint32_t last_tx_time_{0};
  uint32_t tx_start_time_{0};
  uint8_t send_packets_{0};
  uint8_t send_retries_{0};
  bool cancelled_{false};
  const char *log_tag_{"sender"};
};

}  // namespace elero
}  // namespace esphome
