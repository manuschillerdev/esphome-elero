#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "elero/cc1101_tx_fsm.h"

namespace esphome {
namespace elero {
namespace {

class FakeTxOwner : public Cc1101TxFsmOwner {
 public:
  bool prepare_result{true};
  std::vector<Cc1101TxPhaseResult> wait_started_results{
      Cc1101TxPhaseResult::Succeeded};
  std::vector<Cc1101TxPhaseResult> wait_done_results{
      Cc1101TxPhaseResult::Succeeded};
  Cc1101TxPhaseResult return_to_rx_result{Cc1101TxPhaseResult::Succeeded};

  std::vector<Cc1101TxState> entered_states;
  std::vector<RadioMode> mode_changes;
  std::vector<Cc1101TxTerminalResult> terminal_results;
  int recover_calls{0};

  bool tx_prepare_for_fsm() override { return prepare_result; }

  Cc1101TxPhaseResult tx_wait_started_for_fsm() override {
    return PopOrLast(wait_started_results);
  }

  Cc1101TxPhaseResult tx_wait_done_for_fsm() override {
    return PopOrLast(wait_done_results);
  }

  Cc1101TxPhaseResult tx_return_to_rx_for_fsm() override { return return_to_rx_result; }

  void tx_on_state_enter_for_fsm(Cc1101TxState state, uint32_t) override {
    entered_states.push_back(state);
  }

  void tx_set_terminal_result_for_fsm(Cc1101TxTerminalResult result) override {
    terminal_results.push_back(result);
  }

  void tx_set_mode_for_fsm(RadioMode mode) override { mode_changes.push_back(mode); }

  void tx_recover_for_fsm() override { ++recover_calls; }

 private:
  static Cc1101TxPhaseResult PopOrLast(std::vector<Cc1101TxPhaseResult> &results) {
    if (results.empty()) {
      return Cc1101TxPhaseResult::Pending;
    }
    if (results.size() == 1) {
      return results.front();
    }
    Cc1101TxPhaseResult result = results.front();
    results.erase(results.begin());
    return result;
  }
};

TEST(Cc1101TxFsmTest, SuccessPathRequiresNamedPhases) {
  FakeTxOwner owner;
  Cc1101TxFsm fsm(owner);

  ASSERT_TRUE(fsm.Start(100));
  EXPECT_EQ(fsm.state(), Cc1101TxState::Prepare);

  fsm.Poll(101);
  ASSERT_EQ(fsm.state(), Cc1101TxState::WaitTxDone);

  fsm.Poll(102);

  EXPECT_TRUE(fsm.is_idle());
  EXPECT_EQ(owner.mode_changes,
            std::vector<RadioMode>({RadioMode::TX, RadioMode::RX}));
  EXPECT_EQ(owner.entered_states,
            std::vector<Cc1101TxState>({Cc1101TxState::Prepare,
                                        Cc1101TxState::WaitTxStarted,
                                        Cc1101TxState::WaitTxDone,
                                        Cc1101TxState::ReturnToRx,
                                        Cc1101TxState::Idle}));
  const std::vector<Cc1101TxTerminalResult> expected_terminal_results{
      Cc1101TxTerminalResult::None, Cc1101TxTerminalResult::Success};
  EXPECT_EQ(owner.terminal_results, expected_terminal_results);
  EXPECT_EQ(owner.recover_calls, 0);
}

TEST(Cc1101TxFsmTest, PrepareFailureRecoversImmediately) {
  FakeTxOwner owner;
  owner.prepare_result = false;
  Cc1101TxFsm fsm(owner);

  ASSERT_TRUE(fsm.Start(10));
  fsm.Poll(11);

  EXPECT_TRUE(fsm.is_idle());
  EXPECT_EQ(owner.entered_states,
            std::vector<Cc1101TxState>({Cc1101TxState::Prepare,
                                        Cc1101TxState::Recover,
                                        Cc1101TxState::Idle}));
  const std::vector<Cc1101TxTerminalResult> expected_terminal_results{
      Cc1101TxTerminalResult::None, Cc1101TxTerminalResult::Failed};
  EXPECT_EQ(owner.terminal_results, expected_terminal_results);
  EXPECT_EQ(owner.recover_calls, 1);
}

TEST(Cc1101TxFsmTest, StartProofPendingThenFailureStaysLocalToThatPhase) {
  FakeTxOwner owner;
  owner.wait_started_results = {Cc1101TxPhaseResult::Pending,
                                Cc1101TxPhaseResult::Failed};
  Cc1101TxFsm fsm(owner);

  ASSERT_TRUE(fsm.Start(20));
  fsm.Poll(21);
  EXPECT_EQ(fsm.state(), Cc1101TxState::WaitTxStarted);

  fsm.Poll(22);

  EXPECT_TRUE(fsm.is_idle());
  EXPECT_EQ(owner.entered_states,
            std::vector<Cc1101TxState>({Cc1101TxState::Prepare,
                                        Cc1101TxState::WaitTxStarted,
                                        Cc1101TxState::Recover,
                                        Cc1101TxState::Idle}));
  EXPECT_EQ(owner.recover_calls, 1);
}

TEST(Cc1101TxFsmTest, TxDoneFailureDoesNotReportSuccess) {
  FakeTxOwner owner;
  owner.wait_done_results = {Cc1101TxPhaseResult::Pending,
                             Cc1101TxPhaseResult::Failed};
  Cc1101TxFsm fsm(owner);

  ASSERT_TRUE(fsm.Start(30));
  fsm.Poll(31);
  ASSERT_EQ(fsm.state(), Cc1101TxState::WaitTxDone);

  fsm.Poll(32);
  EXPECT_EQ(fsm.state(), Cc1101TxState::WaitTxDone);

  fsm.Poll(33);

  EXPECT_TRUE(fsm.is_idle());
  EXPECT_EQ(owner.entered_states,
            std::vector<Cc1101TxState>({Cc1101TxState::Prepare,
                                        Cc1101TxState::WaitTxStarted,
                                        Cc1101TxState::WaitTxDone,
                                        Cc1101TxState::Recover,
                                        Cc1101TxState::Idle}));
  ASSERT_EQ(owner.terminal_results.size(), 2u);
  EXPECT_EQ(owner.terminal_results[1], Cc1101TxTerminalResult::Failed);
  EXPECT_EQ(owner.recover_calls, 1);
}

TEST(Cc1101TxFsmTest, ReturnToRxFailureRoutesThroughRecovery) {
  FakeTxOwner owner;
  owner.return_to_rx_result = Cc1101TxPhaseResult::Failed;
  Cc1101TxFsm fsm(owner);

  ASSERT_TRUE(fsm.Start(40));
  fsm.Poll(41);
  ASSERT_EQ(fsm.state(), Cc1101TxState::WaitTxDone);

  fsm.Poll(42);

  EXPECT_TRUE(fsm.is_idle());
  EXPECT_EQ(owner.entered_states,
            std::vector<Cc1101TxState>({Cc1101TxState::Prepare,
                                        Cc1101TxState::WaitTxStarted,
                                        Cc1101TxState::WaitTxDone,
                                        Cc1101TxState::ReturnToRx,
                                        Cc1101TxState::Recover,
                                        Cc1101TxState::Idle}));
  const std::vector<Cc1101TxTerminalResult> expected_terminal_results{
      Cc1101TxTerminalResult::None, Cc1101TxTerminalResult::Failed};
  EXPECT_EQ(owner.terminal_results, expected_terminal_results);
  EXPECT_EQ(owner.mode_changes,
            std::vector<RadioMode>({RadioMode::TX, RadioMode::RX}));
  EXPECT_EQ(owner.recover_calls, 1);
}

TEST(Cc1101TxFsmTest, AbortRecoversSynchronouslyFromIdleAndActiveTx) {
  {
    FakeTxOwner owner;
    Cc1101TxFsm fsm(owner);

    fsm.Abort(40);

    EXPECT_TRUE(fsm.is_idle());
    EXPECT_EQ(owner.recover_calls, 1);
    const std::vector<Cc1101TxTerminalResult> expected_terminal_results{
        Cc1101TxTerminalResult::Failed};
    EXPECT_EQ(owner.terminal_results, expected_terminal_results);
  }

  {
    FakeTxOwner owner;
    owner.wait_done_results = {Cc1101TxPhaseResult::Pending};
    Cc1101TxFsm fsm(owner);

    ASSERT_TRUE(fsm.Start(50));
    fsm.Poll(51);
    ASSERT_EQ(fsm.state(), Cc1101TxState::WaitTxDone);

    fsm.Abort(52);

    EXPECT_TRUE(fsm.is_idle());
    EXPECT_EQ(owner.entered_states,
              std::vector<Cc1101TxState>({Cc1101TxState::Prepare,
                                          Cc1101TxState::WaitTxStarted,
                                          Cc1101TxState::WaitTxDone,
                                          Cc1101TxState::Recover,
                                          Cc1101TxState::Idle}));
    EXPECT_EQ(owner.recover_calls, 1);
    const std::vector<Cc1101TxTerminalResult> expected_terminal_results{
        Cc1101TxTerminalResult::None, Cc1101TxTerminalResult::Failed};
    EXPECT_EQ(owner.terminal_results, expected_terminal_results);
  }
}

}  // namespace
}  // namespace elero
}  // namespace esphome
