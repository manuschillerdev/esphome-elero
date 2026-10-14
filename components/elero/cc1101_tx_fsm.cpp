#include "cc1101_tx_fsm.h"

#include <cassert>

namespace esphome {
namespace elero {

Cc1101TxFsm::Cc1101TxFsm(Cc1101TxFsmOwner &owner) : owner_(owner) {}

bool Cc1101TxFsm::Start(uint32_t now) {
  if (!this->is_idle()) {
    return false;
  }

  this->owner_.tx_set_mode_for_fsm(RadioMode::TX);
  this->owner_.tx_set_terminal_result_for_fsm(Cc1101TxTerminalResult::None);
  this->TransitionTo(Cc1101TxState::Prepare, now);
  return true;
}

void Cc1101TxFsm::Poll(uint32_t now) {
  switch (this->state_) {
    case Cc1101TxState::Idle:
      return;
    case Cc1101TxState::Prepare:
      this->HandlePrepare(now);
      return;
    case Cc1101TxState::WaitTxStarted:
      this->HandleWaitTxStarted(now);
      return;
    case Cc1101TxState::WaitTxDone:
      this->HandleWaitTxDone(now);
      return;
    case Cc1101TxState::ReturnToRx:
      this->HandleReturnToRx(now);
      return;
    case Cc1101TxState::Recover:
      this->HandleRecover(now);
      return;
  }
}

void Cc1101TxFsm::Abort(uint32_t now) {
  if (this->is_idle()) {
    this->owner_.tx_recover_for_fsm();
    this->owner_.tx_set_terminal_result_for_fsm(Cc1101TxTerminalResult::Failed);
    return;
  }

  this->TransitionTo(Cc1101TxState::Recover, now);
  this->HandleRecover(now);
}

void Cc1101TxFsm::TransitionTo(Cc1101TxState state, uint32_t now) {
#ifndef NDEBUG
  assert(this->IsTransitionAllowed_(this->state_, state));
#endif
  this->state_ = state;
  this->owner_.tx_on_state_enter_for_fsm(state, now);
}

bool Cc1101TxFsm::IsTransitionAllowed_(Cc1101TxState from,
                                       Cc1101TxState to) const {
  switch (from) {
    case Cc1101TxState::Idle:
      return to == Cc1101TxState::Prepare;
    case Cc1101TxState::Prepare:
      return to == Cc1101TxState::WaitTxStarted || to == Cc1101TxState::Recover;
    case Cc1101TxState::WaitTxStarted:
      return to == Cc1101TxState::WaitTxDone || to == Cc1101TxState::Recover;
    case Cc1101TxState::WaitTxDone:
      return to == Cc1101TxState::ReturnToRx || to == Cc1101TxState::Recover;
    case Cc1101TxState::ReturnToRx:
      return to == Cc1101TxState::Idle || to == Cc1101TxState::Recover;
    case Cc1101TxState::Recover:
      return to == Cc1101TxState::Idle;
  }
  return false;
}

void Cc1101TxFsm::HandlePrepare(uint32_t now) {
  if (!this->owner_.tx_prepare_for_fsm()) {
    this->TransitionTo(Cc1101TxState::Recover, now);
    this->HandleRecover(now);
    return;
  }

  this->TransitionTo(Cc1101TxState::WaitTxStarted, now);
  this->HandleWaitTxStarted(now);
}

void Cc1101TxFsm::HandleWaitTxStarted(uint32_t now) {
  switch (this->owner_.tx_wait_started_for_fsm()) {
    case Cc1101TxPhaseResult::Pending:
      return;
    case Cc1101TxPhaseResult::Succeeded:
      this->TransitionTo(Cc1101TxState::WaitTxDone, now);
      return;
    case Cc1101TxPhaseResult::Failed:
      this->TransitionTo(Cc1101TxState::Recover, now);
      this->HandleRecover(now);
      return;
  }
}

void Cc1101TxFsm::HandleWaitTxDone(uint32_t now) {
  switch (this->owner_.tx_wait_done_for_fsm()) {
    case Cc1101TxPhaseResult::Pending:
      return;
    case Cc1101TxPhaseResult::Succeeded:
      this->owner_.tx_set_mode_for_fsm(RadioMode::RX);
      this->TransitionTo(Cc1101TxState::ReturnToRx, now);
      this->HandleReturnToRx(now);
      return;
    case Cc1101TxPhaseResult::Failed:
      this->TransitionTo(Cc1101TxState::Recover, now);
      this->HandleRecover(now);
      return;
  }
}

void Cc1101TxFsm::HandleReturnToRx(uint32_t now) {
  switch (this->owner_.tx_return_to_rx_for_fsm()) {
    case Cc1101TxPhaseResult::Pending:
      return;
    case Cc1101TxPhaseResult::Succeeded:
      this->owner_.tx_set_terminal_result_for_fsm(Cc1101TxTerminalResult::Success);
      this->TransitionTo(Cc1101TxState::Idle, now);
      return;
    case Cc1101TxPhaseResult::Failed:
      this->TransitionTo(Cc1101TxState::Recover, now);
      this->HandleRecover(now);
      return;
  }
}

void Cc1101TxFsm::HandleRecover(uint32_t now) {
  this->owner_.tx_recover_for_fsm();
  this->owner_.tx_set_terminal_result_for_fsm(Cc1101TxTerminalResult::Failed);
  this->TransitionTo(Cc1101TxState::Idle, now);
}

}  // namespace elero
}  // namespace esphome
