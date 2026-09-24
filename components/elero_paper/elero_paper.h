#pragma once

#include "esphome/components/paper_mono_display/paper_mono_display.h"
#include "esphome/components/elero/device_registry.h"
#include "esphome/components/elero/elero.h"
#include "esphome/components/paper_mono/paper_mono.h"
#include "paper_ui.h"
#include "paper_storage.h"

namespace esphome::elero_paper {

class PaperFrontend;

// Registry drives this adapter; ESPHome alone drives the display's loop.
class PaperAdapter : public elero::OutputAdapter {
 public:
  explicit PaperAdapter(PaperFrontend &display) : display_(display) {}
  void setup(elero::DeviceRegistry &registry) override;
  void loop() override {}
  void on_device_added(const elero::Device &) override;
  void on_device_removed(const elero::Device &) override;
  void on_state_changed(const elero::Device &, uint16_t) override;
  void on_config_changed(const elero::Device &) override;
  void on_rf_packet(const elero::RfPacketInfo &) override;
  void on_group_upserted(const elero::NvsGroupConfig &) override;
  void on_group_removed(const char *) override;
  void on_hub_config_changed() override;
  void on_channel_command_result(const elero::ChannelCommandResult &result) override;

 private:
  PaperFrontend &display_;
};

class PaperFrontend : public paper_mono_display::PaperMonoDisplay {
 public:
  void loop() override;
  void update() override { ui_.changed(); }
  void dump_config() override;
  void set_font(display::BaseFont *font) { font_ = font; }
  void set_title_font(display::BaseFont *font) { title_font_ = font; }
  elero::OutputAdapter *get_adapter() { return &observer_; }
  void set_registry(elero::DeviceRegistry *registry);
  void set_hub(elero::Elero *hub) { hub_ = hub; }
  void received_packet(const elero::RfPacketInfo &packet);
  void channel_command_result(const elero::ChannelCommandResult &result) { ui_.on_channel_command_result(result); }

 protected:
  bool needs_redraw_() const override { return registry_ != nullptr && ui_.dirty(); }
  bool render_next_(size_t part) override;
  void on_presented_() override { ui_.present(); }
  void on_touch_(uint16_t x, uint16_t y) override { ui_.touch(x, y); }

 private:
  void render_(size_t part);
  void draw_icon_(PaperIcon icon, int x, int y, int size);
  PaperAdapter observer_{*this};
  elero::DeviceRegistry *registry_{nullptr};
  elero::Elero *hub_{nullptr};
  PaperUi ui_;
#ifdef USE_ELERO_PAPER_STORAGE
  PaperStorage storage_;
#endif
  display::BaseFont *font_{nullptr};
  display::BaseFont *title_font_{nullptr};
};

}  // namespace esphome::elero_paper
