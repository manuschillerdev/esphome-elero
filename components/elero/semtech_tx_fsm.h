#pragma once

#include <cstdint>

namespace esphome {
namespace elero {

enum class SemtechTxState : uint8_t {
  Idle,
  Prepare,
  WaitTxDone,
  ReturnToRx,
  WaitRxReady,
  Recover,
};

enum class SemtechTxPhaseResult : uint8_t {
  Pending,
  Succeeded,
  Failed,
};

enum class SemtechTxTerminalResult : uint8_t {
  None,
  Success,
  Failed,
};

class SemtechTxFsmOwner {
 public:
  virtual ~SemtechTxFsmOwner() = default;

  virtual bool tx_prepare_for_fsm() = 0;
  virtual SemtechTxPhaseResult tx_wait_done_for_fsm() = 0;
  virtual bool tx_return_to_rx_for_fsm() = 0;
  virtual SemtechTxPhaseResult tx_wait_rx_ready_for_fsm() = 0;
  virtual void tx_on_state_enter_for_fsm(SemtechTxState state, uint32_t now) = 0;
  virtual void tx_set_terminal_result_for_fsm(SemtechTxTerminalResult result) = 0;
  virtual void tx_recover_for_fsm() = 0;
};

class SemtechTxFsm {
 public:
  explicit SemtechTxFsm(SemtechTxFsmOwner &owner);

  [[nodiscard]] bool Start(uint32_t now);
  void Poll(uint32_t now);
  void Abort(uint32_t now);

  [[nodiscard]] bool is_idle() const { return state_ == SemtechTxState::Idle; }
  [[nodiscard]] SemtechTxState state() const { return state_; }

 private:
  void TransitionTo(SemtechTxState state, uint32_t now);
  [[nodiscard]] bool IsTransitionAllowed_(SemtechTxState from,
                                          SemtechTxState to) const;
  void HandlePrepare(uint32_t now);
  void HandleWaitTxDone(uint32_t now);
  void HandleReturnToRx(uint32_t now);
  void HandleWaitRxReady(uint32_t now);
  void HandleRecover(uint32_t now);

  SemtechTxFsmOwner &owner_;
  SemtechTxState state_{SemtechTxState::Idle};
};

}  // namespace elero
}  // namespace esphome
