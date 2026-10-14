#include "semtech_tx_fsm.h"

#include <cassert>

namespace esphome {
namespace elero {

SemtechTxFsm::SemtechTxFsm(SemtechTxFsmOwner &owner) : owner_(owner) {}

bool SemtechTxFsm::Start(uint32_t now) {
  if (!this->is_idle()) {
    return false;
  }

  this->owner_.tx_set_terminal_result_for_fsm(SemtechTxTerminalResult::None);
  this->TransitionTo(SemtechTxState::Prepare, now);
  return true;
}

void SemtechTxFsm::Poll(uint32_t now) {
  switch (this->state_) {
    case SemtechTxState::Idle:
      return;
    case SemtechTxState::Prepare:
      this->HandlePrepare(now);
      return;
    case SemtechTxState::WaitTxDone:
      this->HandleWaitTxDone(now);
      return;
    case SemtechTxState::ReturnToRx:
      this->HandleReturnToRx(now);
      return;
    case SemtechTxState::WaitRxReady:
      this->HandleWaitRxReady(now);
      return;
    case SemtechTxState::Recover:
      this->HandleRecover(now);
      return;
  }
}

void SemtechTxFsm::Abort(uint32_t now) {
  if (this->is_idle()) {
    this->owner_.tx_recover_for_fsm();
    this->owner_.tx_set_terminal_result_for_fsm(SemtechTxTerminalResult::Failed);
    return;
  }

  this->TransitionTo(SemtechTxState::Recover, now);
  this->HandleRecover(now);
}

void SemtechTxFsm::TransitionTo(SemtechTxState state, uint32_t now) {
#ifndef NDEBUG
  assert(this->IsTransitionAllowed_(this->state_, state));
#endif
  this->state_ = state;
  this->owner_.tx_on_state_enter_for_fsm(state, now);
}

bool SemtechTxFsm::IsTransitionAllowed_(SemtechTxState from,
                                       SemtechTxState to) const {
  switch (from) {
    case SemtechTxState::Idle:
      return to == SemtechTxState::Prepare;
    case SemtechTxState::Prepare:
      return to == SemtechTxState::WaitTxDone || to == SemtechTxState::Recover;
    case SemtechTxState::WaitTxDone:
      return to == SemtechTxState::ReturnToRx || to == SemtechTxState::Recover;
    case SemtechTxState::ReturnToRx:
      return to == SemtechTxState::WaitRxReady || to == SemtechTxState::Recover;
    case SemtechTxState::WaitRxReady:
      return to == SemtechTxState::Idle || to == SemtechTxState::Recover;
    case SemtechTxState::Recover:
      return to == SemtechTxState::Idle;
  }
  return false;
}

void SemtechTxFsm::HandlePrepare(uint32_t now) {
  if (!this->owner_.tx_prepare_for_fsm()) {
    this->TransitionTo(SemtechTxState::Recover, now);
    this->HandleRecover(now);
    return;
  }

  this->TransitionTo(SemtechTxState::WaitTxDone, now);
}

void SemtechTxFsm::HandleWaitTxDone(uint32_t now) {
  switch (this->owner_.tx_wait_done_for_fsm()) {
    case SemtechTxPhaseResult::Pending:
      return;
    case SemtechTxPhaseResult::Succeeded:
      this->TransitionTo(SemtechTxState::ReturnToRx, now);
      this->HandleReturnToRx(now);
      return;
    case SemtechTxPhaseResult::Failed:
      this->TransitionTo(SemtechTxState::Recover, now);
      this->HandleRecover(now);
      return;
  }
}

void SemtechTxFsm::HandleReturnToRx(uint32_t now) {
  if (this->owner_.tx_return_to_rx_for_fsm()) {
    this->TransitionTo(SemtechTxState::WaitRxReady, now);
    return;
  }

  this->TransitionTo(SemtechTxState::Recover, now);
  this->HandleRecover(now);
}

void SemtechTxFsm::HandleWaitRxReady(uint32_t now) {
  switch (this->owner_.tx_wait_rx_ready_for_fsm()) {
    case SemtechTxPhaseResult::Pending:
      return;
    case SemtechTxPhaseResult::Succeeded:
      this->owner_.tx_set_terminal_result_for_fsm(SemtechTxTerminalResult::Success);
      this->TransitionTo(SemtechTxState::Idle, now);
      return;
    case SemtechTxPhaseResult::Failed:
      this->TransitionTo(SemtechTxState::Recover, now);
      this->HandleRecover(now);
      return;
  }
}

void SemtechTxFsm::HandleRecover(uint32_t now) {
  this->owner_.tx_recover_for_fsm();
  this->owner_.tx_set_terminal_result_for_fsm(SemtechTxTerminalResult::Failed);
  this->TransitionTo(SemtechTxState::Idle, now);
}

}  // namespace elero
}  // namespace esphome
