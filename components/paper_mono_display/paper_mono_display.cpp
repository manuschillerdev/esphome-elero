// Panel sequencing adapted from M5Stack's M5PaperMono-OTP-Demo (MIT).
// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "paper_mono_display.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstring>

namespace esphome::paper_mono_display {
static const char *const TAG = "paper_mono.display";
void PaperMonoDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "PaperMono panel: 480x800 monochrome, cooperative OTP refresh");
  LOG_PIN("  DC: ", dc_);
  LOG_PIN("  BUSY: ", busy_);
  ESP_LOGCONFIG(TAG, "  Full cleanup after 9 partial refreshes; BUSY timeout 20s");
}
void PaperMonoDisplay::clear_frame_() {
  std::memset(buffer_, 0xFF, ELERO_FRAME_SIZE);
}
void PaperMonoDisplay::setup() {
  this->spi_setup();
  dc_->setup();
  busy_->setup();
  this->init_internal_(ELERO_FRAME_SIZE);
  if (buffer_ == nullptr || !board_->enable_display_touch()) {
    fail_("Display buffer or board initialization failed");
    return;
  }
  ESP_LOGI(TAG, "PaperMono display ready");
}

void PaperMonoDisplay::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= 480 || y < 0 || y >= 800)
    return;
  // Visible portrait axes are transposed relative to the SSD1677's 800x480 RAM.
  const size_t index = x * 100 + y / 8;
  const uint8_t mask = 0x80 >> (y % 8);
  if (color.red || color.green || color.blue)
    buffer_[index] |= mask;
  else
    buffer_[index] &= ~mask;
}

void PaperMonoDisplay::command_(uint8_t command, std::initializer_list<uint8_t> data) {
  enable();
  dc_->digital_write(false);
  write_byte(command);
  dc_->digital_write(true);
  for (auto byte : data)
    write_byte(byte);
  disable();
}

void PaperMonoDisplay::start_ram_(uint8_t command) {
  command_(0x11, {0x03});
  command_(0x44, {0x00, 0x00, 0x1F, 0x03});  // RAM X 0..799.
  command_(0x45, {0x00, 0x00, 0xDF, 0x01});  // RAM Y 0..479.
  command_(0x4E, {0x00, 0x00});
  command_(0x4F, {0x00, 0x00});
  command_(command);
  offset_ = 0;
}

bool PaperMonoDisplay::upload_chunk_(bool invert) {
  const size_t length = std::min(staging_.size(), ELERO_FRAME_SIZE - offset_);
  for (size_t i = 0; i < length; ++i) {
    staging_[i] = invert ? static_cast<uint8_t>(~buffer_[offset_ + i]) : buffer_[offset_ + i];
  }
  enable();
  dc_->digital_write(true);
  write_array(staging_.data(), length);
  disable();
  offset_ += length;
  return offset_ == ELERO_FRAME_SIZE;
}

void PaperMonoDisplay::phase_(Phase phase) {
  phase_state_ = phase;
  phase_started_ = millis();
}

void PaperMonoDisplay::fail_(const char *message) {
  ESP_LOGE(TAG, "%s; radio remains independent", message);
  mark_failed();
}

bool PaperMonoDisplay::wait_done_() {
  const uint32_t elapsed = millis() - phase_started_;
  if (elapsed > 20000) {
    fail_("Panel BUSY timeout");
    return false;
  }
  return elapsed >= 10 && !busy_->digital_read();
}

void PaperMonoDisplay::poll_touch_() {
  if (millis() - last_touch_poll_ < 40)
    return;
  last_touch_poll_ = millis();
  uint16_t x, y;
  bool pressed;
  if (!board_->read_touch(x, y, pressed))
    return;
  if (pressed && !pressed_) {
    on_touch_(x, y);
  }
  pressed_ = pressed;
}

void PaperMonoDisplay::loop() {
  if (is_failed())
    return;
  poll_touch_();
  const uint32_t now = millis();
  switch (phase_state_) {
    case Phase::IDLE:
      if (!needs_redraw_() || now - last_refresh_ < 1000)
        return;
      render_part_ = 0;
      phase_(Phase::COMPOSE);
      return;
    case Phase::COMPOSE:
      if (!render_next_(render_part_++))
        return;
      // Freeze this frame until refresh completion; later events only mark dirty.
      full_ = !baseline_ || partials_ >= 9;
      refresh_started_ = now;
      if (!board_->write_display_reset(false)) {
        fail_("Display reset failed");
        return;
      }
      phase_(Phase::RESET_LOW);
      ESP_LOGD(TAG, "Starting %s refresh", full_ ? "full" : "partial");
      return;
    case Phase::RESET_LOW:
      if (now - phase_started_ < 10)
        return;
      if (!board_->write_display_reset(true)) {
        fail_("Display reset failed");
        return;
      }
      phase_(Phase::RESET_HIGH);
      return;
    case Phase::RESET_HIGH:
      if (!wait_done_())
        return;
      if (full_) {
        command_(0x12);
        phase_(Phase::SOFT_RESET);
      } else {
        command_(0x3C, {0x80});
        start_ram_(0x24);
        phase_(Phase::UPLOAD_PARTIAL);
      }
      return;
    case Phase::SOFT_RESET:
      if (!wait_done_())
        return;
      command_(0x18, {0x80});
      command_(0x0C, {0xAE, 0xC7, 0xC3, 0xC0, 0x80});
      command_(0x01, {0xDF, 0x01, 0x02});
      command_(0x3C, {0x01});
      command_(0x21, {0x00});
      command_(0x22, {0xF8});
      start_ram_(0x24);
      phase_(Phase::UPLOAD_INVERT);
      return;
    case Phase::UPLOAD_INVERT:
      if (!upload_chunk_(true))
        return;
      command_(0x20);
      phase_(Phase::WAIT_INVERT);
      return;
    case Phase::WAIT_INVERT:
      if (!wait_done_())
        return;
      command_(0x22, {0x14});
      start_ram_(0x26);
      phase_(Phase::UPLOAD_OLD);
      return;
    case Phase::UPLOAD_OLD:
      if (!upload_chunk_(false))
        return;
      start_ram_(0x24);
      phase_(Phase::UPLOAD_NEW);
      return;
    case Phase::UPLOAD_NEW:
      if (!upload_chunk_(false))
        return;
      command_(0x20);
      phase_(Phase::WAIT_FULL);
      return;
    case Phase::UPLOAD_PARTIAL:
      if (!upload_chunk_(false))
        return;
      command_(0x21, {0x00});
      command_(0x22, {0xFF});
      command_(0x20);
      phase_(Phase::WAIT_PARTIAL);
      return;
    case Phase::WAIT_FULL:
    case Phase::WAIT_PARTIAL:
      if (!wait_done_())
        return;
      baseline_ = true;
      partials_ = full_ ? 0 : partials_ + 1;
      command_(0x10, {0x01});
      phase_(Phase::SLEEP);
      return;
    case Phase::SLEEP:
      if (now - phase_started_ < 100)
        return;
      ESP_LOGD(TAG, "Refresh complete in %lums", static_cast<unsigned long>(now - refresh_started_));
      on_presented_();
      last_refresh_ = now;
      phase_(Phase::IDLE);
      return;
  }
}

}  // namespace esphome::paper_mono_display
