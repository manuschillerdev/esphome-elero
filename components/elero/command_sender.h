#pragma once

#include "elero.h"
#include "esphome/core/log.h"
#include <queue>

namespace esphome {
namespace elero {

/// Shared command queue and transmission logic for Elero cover and light components.
/// Handles retry logic, packet repetition, and counter management.
class CommandSender {
 public:
  CommandSender() = default;

  /// Process pending commands from the queue.
  /// Should be called from the component's loop() method.
  /// @param now Current millis() timestamp
  /// @param parent Elero hub for sending commands
  /// @param tag Logging tag for debug messages
  void process_queue(uint32_t now, Elero *parent, const char *tag) {
    if ((now - last_command_) > ELERO_DELAY_SEND_PACKETS) {
      if (!command_queue_.empty()) {
        command_.payload[4] = command_queue_.front();
        if (parent->send_command(&command_)) {
          ++send_packets_;
          send_retries_ = 0;
          if (send_packets_ >= ELERO_SEND_PACKETS) {
            command_queue_.pop();
            send_packets_ = 0;
            increase_counter();
          }
        } else {
          ESP_LOGD(tag, "Retry #%d for device 0x%06x", send_retries_, command_.blind_addr);
          ++send_retries_;
          if (send_retries_ > ELERO_SEND_RETRIES) {
            ESP_LOGE(tag, "Hit maximum retries for device 0x%06x, giving up.", command_.blind_addr);
            send_retries_ = 0;
            command_queue_.pop();
          }
        }
        last_command_ = now;
      }
    }
  }

  /// Enqueue a command byte for transmission.
  /// @param cmd_byte The command byte to send
  /// @return true if queued successfully, false if queue is full
  [[nodiscard]] bool enqueue(uint8_t cmd_byte) {
    if (command_queue_.size() < ELERO_MAX_COMMAND_QUEUE) {
      command_queue_.push(cmd_byte);
      return true;
    }
    return false;
  }

  /// Clear all pending commands from the queue.
  void clear_queue() {
    command_queue_ = std::queue<uint8_t>{};
  }

  /// Check if there are pending commands.
  bool has_pending_commands() const { return !command_queue_.empty(); }

  /// Get current queue size.
  size_t queue_size() const { return command_queue_.size(); }

  /// Get mutable reference to the command structure for configuration.
  EleroCommand &command() { return command_; }

  /// Get const reference to the command structure.
  const EleroCommand &command() const { return command_; }

 private:
  void increase_counter() {
    if (command_.counter == 0xff) {
      command_.counter = 1;
    } else {
      ++command_.counter;
    }
  }

  EleroCommand command_{1, 0, 0, 0, {0, 0}, 0, {0}};
  std::queue<uint8_t> command_queue_;
  uint32_t last_command_{0};
  uint8_t send_retries_{0};
  uint8_t send_packets_{0};
};

}  // namespace elero
}  // namespace esphome
