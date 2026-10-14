#pragma once

#include "radio_driver.h"

#include <cstdint>

namespace esphome {
namespace elero {

enum class Cc1101TxState : uint8_t {
  Idle,
  Prepare,
  WaitTxStarted,
  WaitTxDone,
  ReturnToRx,
  Recover,
};

enum class Cc1101TxPhaseResult : uint8_t {
  Pending,
  Succeeded,
  Failed,
};

// Terminal result exposed by the driver once the FSM returns to Idle.
enum class Cc1101TxTerminalResult : uint8_t {
  None,
  Success,
  Failed,
};

class Cc1101TxFsmOwner {
 public:
  virtual ~Cc1101TxFsmOwner() = default;

  virtual bool tx_prepare_for_fsm() = 0;
  virtual Cc1101TxPhaseResult tx_wait_started_for_fsm() = 0;
  virtual Cc1101TxPhaseResult tx_wait_done_for_fsm() = 0;
  virtual Cc1101TxPhaseResult tx_return_to_rx_for_fsm() = 0;
  virtual void tx_on_state_enter_for_fsm(Cc1101TxState state, uint32_t now) = 0;
  virtual void tx_set_terminal_result_for_fsm(Cc1101TxTerminalResult result) = 0;
  virtual void tx_set_mode_for_fsm(RadioMode mode) = 0;
  virtual void tx_recover_for_fsm() = 0;
};

class Cc1101TxFsm {
 public:
  explicit Cc1101TxFsm(Cc1101TxFsmOwner &owner);

  [[nodiscard]] bool Start(uint32_t now);
  void Poll(uint32_t now);
  void Abort(uint32_t now);

  [[nodiscard]] bool is_idle() const { return state_ == Cc1101TxState::Idle; }
  [[nodiscard]] Cc1101TxState state() const { return state_; }

 private:
  void TransitionTo(Cc1101TxState state, uint32_t now);
  [[nodiscard]] bool IsTransitionAllowed_(Cc1101TxState from,
                                          Cc1101TxState to) const;
  void HandlePrepare(uint32_t now);
  void HandleWaitTxStarted(uint32_t now);
  void HandleWaitTxDone(uint32_t now);
  void HandleReturnToRx(uint32_t now);
  void HandleRecover(uint32_t now);

  Cc1101TxFsmOwner &owner_;
  Cc1101TxState state_{Cc1101TxState::Idle};
};

}  // namespace elero
}  // namespace esphome
