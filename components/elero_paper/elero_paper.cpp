#include "elero_paper.h"
#include "paper_text.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstring>

namespace esphome::elero_paper {

static const char *const TAG = "elero.paper";
static const Color BLACK(0, 0, 0);
static const Color WHITE(255, 255, 255);

void PaperAdapter::setup(elero::DeviceRegistry &registry) {
  display_.set_registry(&registry);
}
void PaperAdapter::on_device_added(const elero::Device &) {
  display_.update();
}
void PaperAdapter::on_device_removed(const elero::Device &) {
  display_.update();
}
void PaperAdapter::on_state_changed(const elero::Device &, uint16_t) {
  display_.update();
}
void PaperAdapter::on_config_changed(const elero::Device &) {
  display_.update();
}
void PaperAdapter::on_rf_packet(const elero::RfPacketInfo &packet) {
  display_.received_packet(packet);
}

void PaperAdapter::on_group_upserted(const elero::NvsGroupConfig &) {
  display_.update();
}
void PaperAdapter::on_group_removed(const char *) {
  display_.update();
}
void PaperAdapter::on_hub_config_changed() {
  display_.update();
}
void PaperAdapter::on_channel_command_result(const elero::ChannelCommandResult &result) {
  display_.channel_command_result(result);
}

void PaperFrontend::set_registry(elero::DeviceRegistry *registry) {
  registry_ = registry;
  ui_.setup(hub_, registry);
#ifdef USE_ELERO_PAPER_STORAGE
  storage_.setup(board_, hub_);
  ui_.set_storage([this]() { return storage_.files(); },
                  [this](bool restore, const std::string &file) { return storage_.transfer(restore, file); });
#endif
}

void PaperFrontend::received_packet(const elero::RfPacketInfo &packet) {
  ui_.on_packet(packet);
}

void PaperFrontend::draw_icon_(PaperIcon icon, int x, int y, int size) {
  // Coordinates use a 32-pixel canvas; small navigation icons share the shapes.
  const auto stroke = [this, x, y, size](int x1, int y1, int x2, int y2) {
    for (int offset = -1; offset <= 1; ++offset) {
      line(x + x1 * size / 32 + offset, y + y1 * size / 32, x + x2 * size / 32 + offset, y + y2 * size / 32, BLACK);
      line(x + x1 * size / 32, y + y1 * size / 32 + offset, x + x2 * size / 32, y + y2 * size / 32 + offset, BLACK);
    }
  };
  switch (icon) {
    case PaperIcon::NONE:
      break;
    case PaperIcon::UP:
    case PaperIcon::DOWN: {
      const int tip = icon == PaperIcon::UP ? 4 : 28;
      const int shoulder = icon == PaperIcon::UP ? 15 : 17;
      stroke(16, 4, 16, 28);
      stroke(5, shoulder, 16, tip);
      stroke(16, tip, 27, shoulder);
      break;
    }
    case PaperIcon::STOP:
      filled_rectangle(x + size / 8, y + size / 8, size * 3 / 4, size * 3 / 4, BLACK);
      break;
    case PaperIcon::PREVIOUS:
    case PaperIcon::NEXT: {
      const int tip = icon == PaperIcon::PREVIOUS ? 9 : 23;
      const int end = 32 - tip;
      stroke(end, 4, tip, 16);
      stroke(tip, 16, end, 28);
      break;
    }
    case PaperIcon::LIGHT_ON:
    case PaperIcon::LIGHT_OFF:
      for (int radius = 8; radius <= 10; ++radius)
        circle(x + size / 2, y + 12 * size / 32, radius * size / 32, BLACK);
      stroke(9, 20, 11, 25);
      stroke(23, 20, 21, 25);
      stroke(11, 25, 21, 25);
      stroke(12, 29, 20, 29);
      if (icon == PaperIcon::LIGHT_OFF)
        stroke(3, 3, 29, 29);
      break;
    case PaperIcon::EDIT:
      stroke(6, 21, 22, 5);
      stroke(22, 5, 28, 11);
      stroke(28, 11, 12, 27);
      stroke(12, 27, 4, 29);
      stroke(4, 29, 6, 21);
      stroke(19, 8, 25, 14);
      break;
    case PaperIcon::DELETE:
      stroke(4, 7, 28, 7);
      stroke(12, 3, 20, 3);
      stroke(7, 7, 9, 28);
      stroke(25, 7, 23, 28);
      stroke(9, 28, 23, 28);
      stroke(13, 12, 13, 23);
      stroke(19, 12, 19, 23);
      break;
    case PaperIcon::CHANNELS:
      for (int row = 0; row < 3; ++row) {
        stroke(4, 6 + row * 10, 7, 6 + row * 10);
        stroke(13, 6 + row * 10, 28, 6 + row * 10);
      }
      break;
    case PaperIcon::DEVICES:
      stroke(5, 3, 27, 3);
      stroke(5, 3, 5, 29);
      stroke(27, 3, 27, 29);
      stroke(5, 29, 27, 29);
      for (int row = 0; row < 3; ++row)
        stroke(9, 9 + row * 6, 23, 9 + row * 6);
      break;
    case PaperIcon::MENU:
      for (int row = 0; row < 3; ++row)
        stroke(4, 6 + row * 10, 28, 6 + row * 10);
      break;
  }
}

void PaperFrontend::render_(size_t part) {
  if (part == 0) {
    ui_.build();
    clear_frame_();
    return;
  }
  const auto &item = ui_.frame()[part - 1];
  const bool button = item.action.kind != ActionKind::NONE;
  if (button)
    rectangle(item.x, item.y, item.width, item.height, BLACK);
  const bool has_icon = item.icon != PaperIcon::NONE;
  if (has_icon) {
    const int size = item.text.empty() ? 32 : 22;
    draw_icon_(item.icon, item.x + (item.width - size) / 2, item.y + (item.text.empty() ? (item.height - size) / 2 : 4),
               size);
    if (item.text.empty())
      return;
  }
  auto *font = item.title ? title_font_ : font_;
  std::string remaining = item.text;
  int y = item.y + (button ? (remaining.find('\n') == std::string::npos ? (item.height - 26) / 2 : 7) : 0);
  if (has_icon)
    y = item.y + 28;
  do {
    size_t newline = remaining.find('\n');
    std::string line = remaining.substr(0, newline);
    int x1, y1, width, height;
    for (;;) {
      get_text_bounds(0, 0, line.c_str(), font, display::TextAlign::TOP_LEFT, &x1, &y1, &width, &height);
      if (width <= item.width - (button ? 16 : 0) || line.empty())
        break;
      pop_codepoint(line);
    }
    const int x = button ? item.x + (item.width - width) / 2 : item.x;
    print(x, y, font, BLACK, line.c_str(), WHITE);
    if (newline == std::string::npos)
      break;
    remaining.erase(0, newline + 1);
    y += 28;
  } while (!remaining.empty() && y < item.y + item.height);
}

void PaperFrontend::loop() {
  if (registry_ == nullptr)
    return;
  ui_.loop();
#ifdef USE_ELERO_PAPER_STORAGE
  storage_.loop(ui_);
#endif
  PaperMonoDisplay::loop();
}

bool PaperFrontend::render_next_(size_t part) {
  render_(part);
  return part == ui_.frame().size();
}

void PaperFrontend::dump_config() {
  PaperMonoDisplay::dump_config();
  ESP_LOGCONFIG(TAG, "Elero PaperMono frontend: registry observer, NVS enabled");
#ifdef USE_ELERO_PAPER_STORAGE
  ESP_LOGCONFIG(TAG, "  microSD backup support: enabled (worker starts on demand)");
#else
  ESP_LOGCONFIG(TAG, "  microSD backup support: disabled");
#endif
}

}  // namespace esphome::elero_paper
