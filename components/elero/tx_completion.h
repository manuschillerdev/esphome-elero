#pragma once

#include "tx_client.h"
#include <cassert>
#include <optional>

namespace esphome::elero {

struct TxResult {
  TxClient *client{nullptr};  ///< nullptr for fire-and-forget (raw TX)
  bool success{false};
  uint32_t attempt{0};
};

/// RF-task-owned completion slot. A full Core 1 queue pauses new TX admission,
/// while RX and watchdog processing continue. No accepted result is overwritten.
class PendingTxCompletion {
 public:
  bool empty() const { return !this->result_.has_value(); }
  void put(const TxResult &result) {
    assert(this->empty());
    this->result_ = result;
  }
  template<typename Publish>
  void flush(Publish publish) {
    if (this->result_ && publish(*this->result_)) this->result_.reset();
  }
 private:
  std::optional<TxResult> result_;
};

}  // namespace esphome::elero
