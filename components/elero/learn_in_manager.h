#pragma once

#include "elero_packet.h"
#include "time_provider.h"
#include "tx_client.h"
#include "esphome/core/log.h"
#include <cstdint>

#ifdef UNIT_TEST
#ifndef ESP_LOGV
#define ESP_LOGV(tag, format, ...) ((void)0)
#endif
#ifndef ESP_LOGVV
#define ESP_LOGVV(tag, format, ...) ((void)0)
#endif
#ifndef ESP_LOGD
#define ESP_LOGD(tag, format, ...) ((void)0)
#endif
#ifndef ESP_LOGI
#define ESP_LOGI(tag, format, ...) ((void)0)
#endif
#ifndef ESP_LOGW
#define ESP_LOGW(tag, format, ...) ((void)0)
#endif
#ifndef ESP_LOGE
#define ESP_LOGE(tag, format, ...) ((void)0)
#endif
#endif

namespace esphome::elero {

class Elero;

struct LearnInStartRequest {
  uint32_t src_addr{0};
  uint8_t channel{0};
  uint8_t packets{packet::program::PACKETS};
  uint32_t session_timeout_ms{300000};
};

enum class LearnInState : uint8_t {
  IDLE = 0,
  PROGRAMMING,
  WAIT_UP,
  CONFIRMING_UP,
  WAIT_DOWN,
  CONFIRMING_DOWN,
  COMPLETE,
  FAILED,
  CANCELLED,
  TIMED_OUT,
};

const char *learn_in_state_str(LearnInState state);

class LearnInManager : public TxClient {
 public:
  [[nodiscard]] bool start(const LearnInStartRequest &request);
  [[nodiscard]] bool confirm_up();
  [[nodiscard]] bool confirm_down();
  void cancel();

  template<typename Hub>
  void loop(uint32_t now, Hub *hub) {
    if (hub == nullptr) {
      return;
    }

    if (is_active() && session_deadline_ms_ != 0 &&
        time_reached_(now, session_deadline_ms_)) {
      state_ = LearnInState::TIMED_OUT;
      reset_transport_();
      ESP_LOGW("elero.learn_in", "Learn-in session timed out");
      return;
    }

    // Hardware deadlines belong to the driver; queued work waits without retries.
    if (tx_pending_) return;

    if (queued_step_ == 0) {
      return;
    }

    if (!time_reached_(now, next_attempt_ms_)) {
      return;
    }

    command_.payload[4] = queued_step_;
    if (hub->request_tx(this, command_)) {
      tx_pending_ = true;
      ESP_LOGD("elero.learn_in",
               "TX queued state=%s cmd=0x%02x packet %u/%u src=0x%06x ch=%u",
               learn_in_state_str(state_), queued_step_, sent_packets_ + 1,
               packets_per_step_, command_.src_addr, command_.channel);
    }
  }

  void on_tx_complete(bool success) override;

  [[nodiscard]] LearnInState state() const { return state_; }
  [[nodiscard]] bool is_active() const;
  [[nodiscard]] bool is_busy() const { return tx_pending_ || queued_step_ != 0; }
  [[nodiscard]] uint32_t src_addr() const { return command_.src_addr; }
  [[nodiscard]] uint8_t channel() const { return command_.channel; }

 private:
  [[nodiscard]] static bool time_reached_(uint32_t now, uint32_t deadline);
  [[nodiscard]] uint32_t calculate_backoff_ms_() const;
  [[nodiscard]] bool is_terminal_() const;
  [[nodiscard]] bool prime_session_(const LearnInStartRequest &request);
  [[nodiscard]] bool queue_step_(uint8_t cmd_byte, LearnInState pending_state);
  void fail_session_();
  void increase_counter_();
  void reset_transport_();
  void reset_all_();

  EleroCommand command_{1, 0, 0, 0, packet::msg_type::PROGRAM,
                        packet::program::TYPE2, packet::program::HOP, {0}};
  LearnInState state_{LearnInState::IDLE};
  uint8_t packets_per_step_{packet::program::PACKETS};
  uint8_t queued_step_{0};
  uint8_t sent_packets_{0};
  uint8_t retries_{0};
  uint32_t next_attempt_ms_{0};
  uint32_t session_deadline_ms_{0};
  bool tx_pending_{false};
};

}  // namespace esphome::elero
