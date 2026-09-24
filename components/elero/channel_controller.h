#pragma once

#include "command_sender.h"

namespace esphome::elero {

enum class ChannelCommandState : uint8_t { IDLE, PENDING, SENT, FAILED };

// A single channel operation, owned and driven by the hub on Core 1.
// STOP replaces pending repeats; stale RF completions are invalidated by clear_queue.
class ChannelController : public CommandSender {
 public:
  [[nodiscard]] bool send(uint32_t remote, uint8_t channel, uint8_t command) {
    if (remote == 0 || remote > 0xFFFFFF || channel == 0 ||
        (command != packet::command::UP && command != packet::command::STOP && command != packet::command::DOWN))
      return false;
    if (is_busy()) {
      if (command != packet::command::STOP)
        return false;
      clear_queue();
    }
    this->command().src_addr = remote;
    this->command().channel = channel;
    this->command().dst_addr = 0;
    if (!enqueue(command))
      return false;
    result_ = ChannelCommandState::PENDING;
    return true;
  }

  void on_tx_complete(bool success) override {
    CommandSender::on_tx_complete(success);
    if (result_ == ChannelCommandState::PENDING && !is_busy())
      result_ = success ? ChannelCommandState::SENT : ChannelCommandState::FAILED;
  }

  ChannelCommandState result() const { return result_; }

 private:
  ChannelCommandState result_{ChannelCommandState::IDLE};
};

}  // namespace esphome::elero
