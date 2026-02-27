#pragma once

#include "elero.h"
#include "tx_client.h"
#include "esphome/core/log.h"
#include <queue>

namespace esphome {
namespace elero {

/// Non-blocking command queue and transmission logic for Elero cover and light components.
///
/// This class implements a state machine that coordinates with the Elero hub to send
/// RF commands without blocking the ESPHome main loop. Key features:
///
/// - Queues commands and processes them one at a time
/// - Each command is sent twice (ELERO_SEND_PACKETS) for reliability
/// - 50ms delay between packets (ELERO_DELAY_SEND_PACKETS)
/// - Up to 3 retries on failure (ELERO_SEND_RETRIES)
/// - Cancellation support for STOP commands
///
/// State machine:
///   IDLE ──enqueue()──▶ WAIT_DELAY ──request_tx()──▶ TX_PENDING
///                           ▲                            │
///                           └────on_tx_complete()────────┘
///
/// Ownership model:
/// - When TX_PENDING, this sender "owns" the hub's TX
/// - The hub will call on_tx_complete() exactly once
/// - After callback, ownership is released
class CommandSender : public TxClient {
 public:
  /// Sender state machine states
  enum class State : uint8_t {
    IDLE,        ///< No pending commands, or queue empty
    WAIT_DELAY,  ///< Have command, waiting for inter-packet delay (50ms)
    TX_PENDING,  ///< TX requested and accepted, waiting for hub callback
  };

  CommandSender() = default;

  /// Process the command queue. Call from component's loop().
  ///
  /// This method progresses the state machine:
  /// - IDLE: Check if commands are queued, transition to WAIT_DELAY
  /// - WAIT_DELAY: Check delay, request TX from hub
  /// - TX_PENDING: Nothing to do (waiting for on_tx_complete callback)
  ///
  /// @param now Current millis() timestamp
  /// @param parent Elero hub for TX requests
  /// @param tag Logging tag for debug messages
  void process_queue(uint32_t now, Elero *parent, const char *tag) {
    this->log_tag_ = tag;

    switch (this->state_) {
      case State::IDLE:
        // Nothing pending, check if we have commands
        if (this->command_queue_.empty()) {
          return;
        }
        // Fall through to try sending
        this->state_ = State::WAIT_DELAY;
        [[fallthrough]];

      case State::WAIT_DELAY:
        // Check inter-packet delay
        if ((now - this->last_tx_time_) < ELERO_DELAY_SEND_PACKETS) {
          return;  // Still waiting
        }

        // Ready to transmit - try to acquire the radio
        this->command_.payload[4] = this->command_queue_.front();

        if (parent->request_tx(this, this->command_)) {
          // Successfully started TX
          this->state_ = State::TX_PENDING;
          ESP_LOGV(tag, "TX started for 0x%06x cmd=0x%02x, packet %d/%d",
                   this->command_.blind_addr, this->command_.payload[4],
                   this->send_packets_ + 1, ELERO_SEND_PACKETS);
        } else {
          // Radio busy, will retry next loop iteration
          ESP_LOGVV(tag, "Radio busy for 0x%06x, will retry", this->command_.blind_addr);
        }
        break;

      case State::TX_PENDING:
        // Waiting for on_tx_complete() callback from hub
        // Nothing to do here
        break;
    }
  }

  /// TxClient interface: called by hub when TX completes.
  ///
  /// This callback is guaranteed to be called exactly once for each successful
  /// request_tx() call. It handles:
  /// - Cancellation (if clear_queue was called during TX)
  /// - Success: increment packet count, dequeue if done
  /// - Failure: retry or give up after max retries
  void on_tx_complete(bool success) override {
    this->last_tx_time_ = millis();

    // Check cancellation FIRST
    if (this->cancelled_) {
      ESP_LOGD(this->log_tag_, "TX for 0x%06x completed but was cancelled, ignoring",
               this->command_.blind_addr);
      this->cancelled_ = false;
      this->send_packets_ = 0;
      this->send_retries_ = 0;
      this->state_ = State::IDLE;
      return;  // Don't process result, don't advance queue (already empty)
    }

    if (success) {
      this->send_retries_ = 0;
      ++this->send_packets_;

      if (this->send_packets_ >= ELERO_SEND_PACKETS) {
        // Command fully sent, move to next
        ESP_LOGV(this->log_tag_, "Command 0x%02x to 0x%06x complete (%d packets)",
                 this->command_.payload[4], this->command_.blind_addr, this->send_packets_);
        this->advance_queue_();
      } else {
        // Need to send more packets for this command
        this->state_ = State::WAIT_DELAY;
      }
    } else {
      // TX failed
      ++this->send_retries_;
      ESP_LOGD(this->log_tag_, "TX retry %d/%d for 0x%06x",
               this->send_retries_, ELERO_SEND_RETRIES, this->command_.blind_addr);

      if (this->send_retries_ > ELERO_SEND_RETRIES) {
        // Give up on this command
        ESP_LOGE(this->log_tag_, "Max retries for 0x%06x, dropping command 0x%02x",
                 this->command_.blind_addr, this->command_.payload[4]);
        this->advance_queue_();
      } else {
        // Will retry after delay
        this->state_ = State::WAIT_DELAY;
      }
    }
  }

  /// Enqueue a command byte for transmission.
  /// @param cmd_byte The command byte to send
  /// @return true if queued successfully, false if queue is full
  [[nodiscard]] bool enqueue(uint8_t cmd_byte) {
    if (this->command_queue_.size() >= ELERO_MAX_COMMAND_QUEUE) {
      return false;
    }
    this->command_queue_.push(cmd_byte);

    // Kick state machine if idle
    if (this->state_ == State::IDLE) {
      this->state_ = State::WAIT_DELAY;
    }
    return true;
  }

  /// Clear all pending commands from the queue.
  ///
  /// If TX is in progress, it will complete but the result will be ignored.
  /// This is used for STOP commands to ensure immediate response.
  void clear_queue() {
    this->command_queue_ = std::queue<uint8_t>{};
    this->send_packets_ = 0;
    this->send_retries_ = 0;

    if (this->state_ == State::TX_PENDING) {
      // TX in flight — can't abort mid-transmission, mark as cancelled
      // The on_tx_complete callback will see this flag and go directly to IDLE
      this->cancelled_ = true;
    } else {
      this->state_ = State::IDLE;
    }
  }

  /// Check current state.
  State state() const { return this->state_; }

  /// Check if sender is busy (has pending work).
  bool is_busy() const {
    return this->state_ != State::IDLE || !this->command_queue_.empty();
  }

  /// Check if there are pending commands in queue.
  bool has_pending_commands() const { return !this->command_queue_.empty(); }

  /// Get current queue size.
  size_t queue_size() const { return this->command_queue_.size(); }

  /// Get mutable reference to the command structure for configuration.
  EleroCommand &command() { return this->command_; }

  /// Get const reference to the command structure.
  const EleroCommand &command() const { return this->command_; }

 private:
  /// Advance to next command in queue (called after command completes or fails).
  void advance_queue_() {
    // Guard: only pop if queue has items
    // (defensive — cancelled_ logic should prevent this, but be safe)
    if (!this->command_queue_.empty()) {
      this->command_queue_.pop();
    }

    this->send_packets_ = 0;
    this->send_retries_ = 0;
    this->increase_counter_();

    // Transition based on queue state
    if (this->command_queue_.empty()) {
      this->state_ = State::IDLE;
    } else {
      this->state_ = State::WAIT_DELAY;
    }
  }

  /// Increment rolling command counter (wraps 255 → 1).
  void increase_counter_() {
    if (this->command_.counter == 0xff) {
      this->command_.counter = 1;
    } else {
      ++this->command_.counter;
    }
  }

  EleroCommand command_{1, 0, 0, 0, {0, 0}, 0, {0}};
  std::queue<uint8_t> command_queue_;

  State state_{State::IDLE};
  uint32_t last_tx_time_{0};
  uint8_t send_packets_{0};
  uint8_t send_retries_{0};
  bool cancelled_{false};
  const char *log_tag_{"sender"};
};

}  // namespace elero
}  // namespace esphome
