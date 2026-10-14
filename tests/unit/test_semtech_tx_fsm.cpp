#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "elero/semtech_tx_fsm.h"

namespace esphome {
namespace elero {
namespace {

class FakeTxOwner : public SemtechTxFsmOwner {
 public:
  bool prepare_result{true};
  std::vector<SemtechTxPhaseResult> wait_done_results{
      SemtechTxPhaseResult::Succeeded};
  bool return_to_rx_result{true};
  std::vector<SemtechTxPhaseResult> wait_rx_ready_results{
      SemtechTxPhaseResult::Succeeded};

  std::vector<SemtechTxState> entered_states;
  std::vector<SemtechTxTerminalResult> terminal_results;
  int recover_calls{0};

  bool tx_prepare_for_fsm() override { return prepare_result; }

  SemtechTxPhaseResult tx_wait_done_for_fsm() override {
    return PopOrLast(wait_done_results);
  }

  bool tx_return_to_rx_for_fsm() override { return return_to_rx_result; }

  SemtechTxPhaseResult tx_wait_rx_ready_for_fsm() override {
    return PopOrLast(wait_rx_ready_results);
  }

  void tx_on_state_enter_for_fsm(SemtechTxState state, uint32_t) override {
    entered_states.push_back(state);
  }

  void tx_set_terminal_result_for_fsm(SemtechTxTerminalResult result) override {
    terminal_results.push_back(result);
  }

  void tx_recover_for_fsm() override { ++recover_calls; }

 private:
  static SemtechTxPhaseResult PopOrLast(std::vector<SemtechTxPhaseResult> &results) {
    if (results.empty()) {
      return SemtechTxPhaseResult::Pending;
    }
    if (results.size() == 1) {
      return results.front();
    }
    SemtechTxPhaseResult result = results.front();
    results.erase(results.begin());
    return result;
  }
};

TEST(SemtechTxFsmTest, SuccessPathUsesPrepareWaitDoneAndReturnToRx) {
  FakeTxOwner owner;
  SemtechTxFsm fsm(owner);

  ASSERT_TRUE(fsm.Start(100));
  EXPECT_EQ(fsm.state(), SemtechTxState::Prepare);

  fsm.Poll(101);
  ASSERT_EQ(fsm.state(), SemtechTxState::WaitTxDone);

  fsm.Poll(102);
  ASSERT_EQ(fsm.state(), SemtechTxState::WaitRxReady);

  fsm.Poll(103);

  EXPECT_TRUE(fsm.is_idle());
  EXPECT_EQ(owner.entered_states,
            std::vector<SemtechTxState>({SemtechTxState::Prepare,
                                        SemtechTxState::WaitTxDone,
                                        SemtechTxState::ReturnToRx,
                                        SemtechTxState::WaitRxReady,
                                        SemtechTxState::Idle}));
  EXPECT_EQ(owner.terminal_results,
            std::vector<SemtechTxTerminalResult>({SemtechTxTerminalResult::None,
                                                 SemtechTxTerminalResult::Success}));
  EXPECT_EQ(owner.recover_calls, 0);
}

TEST(SemtechTxFsmTest, PrepareFailureRecoversImmediately) {
  FakeTxOwner owner;
  owner.prepare_result = false;
  SemtechTxFsm fsm(owner);

  ASSERT_TRUE(fsm.Start(10));
  fsm.Poll(11);

  EXPECT_TRUE(fsm.is_idle());
  EXPECT_EQ(owner.entered_states,
            std::vector<SemtechTxState>({SemtechTxState::Prepare,
                                        SemtechTxState::Recover,
                                        SemtechTxState::Idle}));
  EXPECT_EQ(owner.terminal_results,
            std::vector<SemtechTxTerminalResult>({SemtechTxTerminalResult::None,
                                                 SemtechTxTerminalResult::Failed}));
  EXPECT_EQ(owner.recover_calls, 1);
}

TEST(SemtechTxFsmTest, WaitDoneCanRemainPendingBeforeFailing) {
  FakeTxOwner owner;
  owner.wait_done_results = {SemtechTxPhaseResult::Pending,
                             SemtechTxPhaseResult::Failed};
  SemtechTxFsm fsm(owner);

  ASSERT_TRUE(fsm.Start(20));
  fsm.Poll(21);
  ASSERT_EQ(fsm.state(), SemtechTxState::WaitTxDone);

  fsm.Poll(22);
  EXPECT_EQ(fsm.state(), SemtechTxState::WaitTxDone);

  fsm.Poll(23);

  EXPECT_TRUE(fsm.is_idle());
  EXPECT_EQ(owner.entered_states,
            std::vector<SemtechTxState>({SemtechTxState::Prepare,
                                        SemtechTxState::WaitTxDone,
                                        SemtechTxState::Recover,
                                        SemtechTxState::Idle}));
  EXPECT_EQ(owner.terminal_results,
            std::vector<SemtechTxTerminalResult>({SemtechTxTerminalResult::None,
                                                 SemtechTxTerminalResult::Failed}));
  EXPECT_EQ(owner.recover_calls, 1);
}

TEST(SemtechTxFsmTest, ReturnToRxFailureRoutesThroughRecovery) {
  FakeTxOwner owner;
  owner.return_to_rx_result = false;
  SemtechTxFsm fsm(owner);

  ASSERT_TRUE(fsm.Start(30));
  fsm.Poll(31);
  ASSERT_EQ(fsm.state(), SemtechTxState::WaitTxDone);

  fsm.Poll(32);

  EXPECT_TRUE(fsm.is_idle());
  EXPECT_EQ(owner.entered_states,
            std::vector<SemtechTxState>({SemtechTxState::Prepare,
                                        SemtechTxState::WaitTxDone,
                                        SemtechTxState::ReturnToRx,
                                        SemtechTxState::Recover,
                                        SemtechTxState::Idle}));
  EXPECT_EQ(owner.terminal_results,
            std::vector<SemtechTxTerminalResult>({SemtechTxTerminalResult::None,
                                                 SemtechTxTerminalResult::Failed}));
  EXPECT_EQ(owner.recover_calls, 1);
}

TEST(SemtechTxFsmTest, WaitRxReadyCanRemainPendingBeforeSucceeding) {
  FakeTxOwner owner;
  owner.wait_rx_ready_results = {SemtechTxPhaseResult::Pending,
                                 SemtechTxPhaseResult::Succeeded};
  SemtechTxFsm fsm(owner);

  ASSERT_TRUE(fsm.Start(40));
  fsm.Poll(41);
  ASSERT_EQ(fsm.state(), SemtechTxState::WaitTxDone);

  fsm.Poll(42);
  ASSERT_EQ(fsm.state(), SemtechTxState::WaitRxReady);

  fsm.Poll(43);
  EXPECT_EQ(fsm.state(), SemtechTxState::WaitRxReady);

  fsm.Poll(44);
  EXPECT_TRUE(fsm.is_idle());
  EXPECT_EQ(owner.entered_states,
            std::vector<SemtechTxState>({SemtechTxState::Prepare,
                                        SemtechTxState::WaitTxDone,
                                        SemtechTxState::ReturnToRx,
                                        SemtechTxState::WaitRxReady,
                                        SemtechTxState::Idle}));
}

TEST(SemtechTxFsmTest, WaitRxReadyFailureRoutesThroughRecovery) {
  FakeTxOwner owner;
  owner.wait_rx_ready_results = {SemtechTxPhaseResult::Failed};
  SemtechTxFsm fsm(owner);

  ASSERT_TRUE(fsm.Start(45));
  fsm.Poll(46);
  ASSERT_EQ(fsm.state(), SemtechTxState::WaitTxDone);

  fsm.Poll(47);
  ASSERT_EQ(fsm.state(), SemtechTxState::WaitRxReady);

  fsm.Poll(48);
  EXPECT_TRUE(fsm.is_idle());
  EXPECT_EQ(owner.entered_states,
            std::vector<SemtechTxState>({SemtechTxState::Prepare,
                                        SemtechTxState::WaitTxDone,
                                        SemtechTxState::ReturnToRx,
                                        SemtechTxState::WaitRxReady,
                                        SemtechTxState::Recover,
                                        SemtechTxState::Idle}));
  EXPECT_EQ(owner.terminal_results,
            std::vector<SemtechTxTerminalResult>({SemtechTxTerminalResult::None,
                                                 SemtechTxTerminalResult::Failed}));
  EXPECT_EQ(owner.recover_calls, 1);
}

TEST(SemtechTxFsmTest, AbortRecoversFromIdleAndActiveTx) {
  {
    FakeTxOwner owner;
    SemtechTxFsm fsm(owner);

    fsm.Abort(40);

    EXPECT_TRUE(fsm.is_idle());
    EXPECT_EQ(owner.recover_calls, 1);
    EXPECT_EQ(owner.terminal_results,
              std::vector<SemtechTxTerminalResult>({SemtechTxTerminalResult::Failed}));
  }

  {
    FakeTxOwner owner;
    owner.wait_done_results = {SemtechTxPhaseResult::Pending};
    SemtechTxFsm fsm(owner);

    ASSERT_TRUE(fsm.Start(50));
    fsm.Poll(51);
    ASSERT_EQ(fsm.state(), SemtechTxState::WaitTxDone);

    fsm.Abort(52);

    EXPECT_TRUE(fsm.is_idle());
    EXPECT_EQ(owner.entered_states,
              std::vector<SemtechTxState>({SemtechTxState::Prepare,
                                          SemtechTxState::WaitTxDone,
                                          SemtechTxState::Recover,
                                          SemtechTxState::Idle}));
    EXPECT_EQ(owner.terminal_results,
              std::vector<SemtechTxTerminalResult>({SemtechTxTerminalResult::None,
                                                   SemtechTxTerminalResult::Failed}));
    EXPECT_EQ(owner.recover_calls, 1);
  }
}

}  // namespace
}  // namespace elero
}  // namespace esphome
