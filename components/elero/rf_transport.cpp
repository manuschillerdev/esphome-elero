#include "rf_transport.h"
#include "radio_rx.h"

namespace esphome::elero {

bool RfTransport::drain_rx_(RfTransportPort &port) {
  if (this->driver_.failed()) return true;
  return drain_radio_rx(this->driver_, this->rx_ready_, this->rx_buffer_,
                        sizeof(this->rx_buffer_), [&](size_t size) {
    port.receive_frame(this->rx_buffer_, size);
  });
}

void RfTransport::start_request_(const RfTaskRequest &request) {
  switch (request.type) {
    case RfTaskRequest::Type::TX: {
      const size_t size = packet::build_command_packet(request.cmd, this->tx_buffer_);
      const TxResult result{request.client, false, request.attempt};
      if (size != 0 && !this->driver_.failed() &&
          this->driver_.load_and_transmit(this->tx_buffer_, size)) {
        this->active_ = result;
      } else {
        this->completion_.put(result);
      }
      break;
    }
    case RfTaskRequest::Type::REINIT_FREQ:
      if (this->driver_.failed()) return;
      this->rx_ready_.store(false, std::memory_order_release);
      this->tx_done_.store(false, std::memory_order_release);
      this->driver_.set_frequency_regs(request.freq.f2, request.freq.f1, request.freq.f0);
      break;
  }
}

bool RfTransport::poll_tx_() {
  if (!this->active_) return false;
  const auto result = this->driver_.failed() ? TxPollResult::FAILED : this->driver_.poll_tx();
  if (result == TxPollResult::PENDING) return false;
  this->active_->success = result == TxPollResult::SUCCESS;
  this->completion_.put(*this->active_);
  this->active_.reset();
  return result == TxPollResult::FAILED;
}

bool RfTransport::step(RfTransportPort &port) {
  this->completion_.flush([&](const TxResult &result) { return port.publish_completion(result); });
  // Never flush an unread reply or partial capture to admit another command.
  const bool rx_idle = this->drain_rx_(port);
  if (!this->active_ && rx_idle && this->completion_.empty()) {
    RfTaskRequest request;
    if (port.take_request(request)) this->start_request_(request);
  }
  const bool tx_failed = this->poll_tx_();
  // A fast reply can arrive while poll_tx restores RX routing.
  const bool rx_idle_after_tx = this->drain_rx_(port);
  if (!this->active_ && rx_idle_after_tx && !this->driver_.failed() &&
      this->driver_.check_health() != RadioHealth::OK) {
    this->driver_.recover();
  }
  return tx_failed;
}

}  // namespace esphome::elero
