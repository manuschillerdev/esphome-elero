#pragma once

#include <cinttypes>
#include "elero_packet.h"
#include "time_provider.h"
#include "tx_client.h"
#include "esphome/core/log.h"
#include <cstdint>
#include <optional>
#include <variant>

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

enum class LearnInMode : uint8_t { POWER_CYCLE, OPEN_STANDBY, TRAIN, REMOVE_LINK };

struct LearnInStartRequest {
  uint32_t src_addr{0};
  uint8_t channel{0};
  uint8_t packets{packet::program::PACKETS};
  uint32_t session_timeout_ms{300000};
  LearnInMode mode{LearnInMode::POWER_CYCLE};
  uint32_t receiver{0};
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
  OPENING_STANDBY,
  STANDBY_SENT,
  TRAIN_P1,
  TRAIN_P2,
  TRAIN_P3,
  WATCH_MOVEMENT,
  TRAIN_UP,
  TRAIN_DOWN,
  ACKNOWLEDGING,
  ACK_SENT,
  REMOVING_LINK,
  REMOVAL_SENT,
};

const char *learn_in_state_str(LearnInState state);

class LearnInManager : public TxClient {
 public:
  [[nodiscard]] bool start(const LearnInStartRequest &request);
  [[nodiscard]] bool confirm_up();
  [[nodiscard]] bool confirm_down();
  void cancel();
  void on_receiver_frame(std::span<const uint8_t> raw);
  [[nodiscard]] uint32_t receiver() const { return command_.dst_addr; }
  [[nodiscard]] LearnInMode mode() const { return mode_; }

  template<typename Hub>
  void loop(Hub *hub) {
    if (hub == nullptr || !is_active()) {
      return;
    }

    // A web request can start this session during the registry loop.
    const uint32_t now = get_time_provider().millis();
    if (now - session_start_ms_ >= session_timeout_ms_) {
      finish_(LearnInState::TIMED_OUT);
      ESP_LOGW("elero.learn_in", "Learn-in session timed out");
      return;
    }

    auto *step = std::get_if<Sending>(&state_);
    if (step == nullptr || !step->next_attempt_ms ||
        !time_reached_(now, *step->next_attempt_ms)) return;

    if (hub->request_tx(this, command_)) {
      step->next_attempt_ms.reset();
      ESP_LOGD("elero.learn_in",
               "TX queued state=%s cmd=0x%02x packet %u/%u src=0x%06" PRIx32 " ch=%u",
               learn_in_state_str(step->phase), command_.payload[4], step->sent_packets + 1,
               packets_per_step_, command_.src_addr, command_.channel);
    }
  }

  void on_tx_complete(bool success) override;

  [[nodiscard]] LearnInState state() const;
  [[nodiscard]] bool is_active() const;
  [[nodiscard]] bool is_busy() const { return std::holds_alternative<Sending>(state_); }
  [[nodiscard]] uint32_t src_addr() const { return command_.src_addr; }
  [[nodiscard]] uint8_t channel() const { return command_.channel; }

 private:
  [[nodiscard]] static bool time_reached_(uint32_t now, uint32_t deadline);
  struct Sending {
    LearnInState phase;
    LearnInState on_success;
    // An accepted packet has no retry deadline until its completion arrives.
    std::optional<uint32_t> next_attempt_ms;
    uint8_t sent_packets{0};
    uint8_t retries{0};
  };

  void begin_step_(uint8_t command, LearnInState phase, LearnInState on_success);
  void finish_(LearnInState state);
  void begin_training_step_(LearnInState phase, uint32_t delay_ms = 0);
  LearnInMode mode_{LearnInMode::POWER_CYCLE};
  uint8_t next_training_counter_{1};
  uint8_t confirmed_directions_{0};
  bool receiver_answered_{false};

  EleroCommand command_{1, 0, 0, 0, packet::msg_type::PROGRAM,
                        packet::program::TYPE2, packet::program::HOP, {0, packet::button::SINGLE_PAYLOAD_2}};
  std::variant<LearnInState, Sending> state_{LearnInState::IDLE};
  uint8_t packets_per_step_{packet::program::PACKETS};
  uint32_t session_start_ms_{0};
  uint32_t session_timeout_ms_{0};
};

}  // namespace esphome::elero
