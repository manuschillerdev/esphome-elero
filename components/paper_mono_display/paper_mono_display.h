#pragma once

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/paper_mono/paper_mono.h"
#include <array>
#include <initializer_list>

namespace esphome::paper_mono_display {

// Hardware rendering lifecycle. Derived frontends supply frames and receive touch
// and presentation callbacks; the driver knows nothing about their domain.
class PaperMonoDisplay : public display::DisplayBuffer,
                         public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                               spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_20MHZ> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_BINARY; }
  void set_board(paper_mono::PaperMono *board) { board_ = board; }
  void set_dc_pin(InternalGPIOPin *pin) { dc_ = pin; }
  void set_busy_pin(InternalGPIOPin *pin) { busy_ = pin; }

 protected:
  void clear_frame_();
  int get_width_internal() override { return 480; }
  int get_height_internal() override { return 800; }
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  virtual bool needs_redraw_() const = 0;
  virtual bool render_next_(size_t part) = 0;
  virtual void on_presented_() = 0;
  virtual void on_touch_(uint16_t x, uint16_t y) = 0;
  paper_mono::PaperMono *board_{nullptr};

 private:
  enum class Phase : uint8_t {
    IDLE,
    COMPOSE,
    RESET_LOW,
    RESET_HIGH,
    SOFT_RESET,
    UPLOAD_INVERT,
    WAIT_INVERT,
    UPLOAD_OLD,
    UPLOAD_NEW,
    WAIT_FULL,
    UPLOAD_PARTIAL,
    WAIT_PARTIAL,
    SLEEP
  };
  void phase_(Phase phase);
  void command_(uint8_t command, std::initializer_list<uint8_t> data = {});
  void start_ram_(uint8_t command);
  bool upload_chunk_(bool invert);
  void poll_touch_();
  bool wait_done_();
  void fail_(const char *message);
  static constexpr size_t ELERO_FRAME_SIZE = 48000;
  InternalGPIOPin *dc_{nullptr};
  InternalGPIOPin *busy_{nullptr};
  Phase phase_state_{Phase::IDLE};
  std::array<uint8_t, 2048> staging_{};
  size_t offset_{0};
  uint32_t phase_started_{0};
  uint32_t refresh_started_{0};
  uint32_t last_refresh_{0};
  uint32_t last_touch_poll_{0};
  uint8_t partials_{0};
  size_t render_part_{0};
  bool baseline_{false};
  bool full_{true};
  bool pressed_{false};
};

}  // namespace esphome::paper_mono_display
