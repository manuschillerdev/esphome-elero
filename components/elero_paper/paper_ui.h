#pragma once

#include "esphome/components/elero/elero.h"
#include "esphome/components/elero/device_registry.h"
#include "esphome/components/elero/state_snapshot.h"
#include "esphome/core/preferences.h"
#include "paper_fields.h"
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace esphome::elero_paper {

enum class Page {
  CHANNELS,
  DEVICES,
  DEVICE,
  EDIT,
  GROUPS,
  GROUP,
  MEMBERS,
  MENU,
  HUB,
  LEARN,
  RAW,
  PACKETS,
  PACKET,
  LOG_FILTERS,
  FILTERS,
  BULK,
  KEYBOARD,
  CONFIRM,
  BACKUP
};
enum class ActionKind {
  NONE,
  GO,
  PAGE,
  DEVICE,
  COMMAND,
  EDIT,
  FIELD,
  SAVE,
  REMOVE,
  SNIFF,
  CHANNEL,
  GROUP,
  GROUP_COMMAND,
  NEW_GROUP,
  MEMBER,
  SAVE_GROUP,
  REMOVE_GROUP,
  SELECT,
  BULK,
  KEY,
  KEY_SAVE,
  KEY_CANCEL,
  FILTER,
  CHECK_ALL,
  RESTART,
  LEARN_START,
  LEARN_UP,
  LEARN_DOWN,
  LEARN_CANCEL,
  RAW_SEND,
  PACKET,
  REPLAY,
  LOG_REFRESH,
  LOG_CLEAR,
  CONFIRM,
  EXPORT,
  IMPORT,
  FILE
};
struct Action {
  ActionKind kind{ActionKind::NONE};
  int value{0};
  uint32_t address{0};
  elero::DeviceType type{elero::DeviceType::COVER};
  std::string id;
};
enum class PaperIcon : uint8_t {
  NONE,
  UP,
  STOP,
  DOWN,
  LIGHT_ON,
  LIGHT_OFF,
  PREVIOUS,
  NEXT,
  EDIT,
  DELETE,
  CHANNELS,
  DEVICES,
  MENU
};
struct PaperElement {
  int x, y, width, height;
  std::string text;
  Action action{};
  bool title{false};
  PaperIcon icon{PaperIcon::NONE};
};

// Presentation state only. Actions resolve stable IDs against the core when tapped.
class PaperUi {
 public:
  void setup(elero::Elero *hub, elero::DeviceRegistry *registry);
  void loop();
  void on_channel_command_result(const elero::ChannelCommandResult &result);
  void on_packet(const elero::RfPacketInfo &packet);
  void changed();
  bool dirty() const { return dirty_; }
  void build();
  const std::vector<PaperElement> &frame() const { return frame_; }
  void present();
  void touch(uint16_t x, uint16_t y);
  void storage_completed(const std::vector<std::string> &files, const std::string &message, bool listing);
  void set_storage(std::function<std::string()> files, std::function<std::string(bool, const std::string &)> transfer) {
    list_files_ = std::move(files);
    transfer_ = std::move(transfer);
  }

 private:
  void execute_(Action action);
  void go_(Page page);
  void text_(int y, const std::string &text, bool title = false);
  void button_(int x, int y, int w, const std::string &text, Action action, int h = 58);
  void icon_button_(int x, int y, int w, PaperIcon icon, Action action, int h = 58, const std::string &label = "");
  void row_(int row, const std::string &text, Action action);
  void pager_(size_t count, int per_page = 5);
  void controls_(int y, bool light, ActionKind kind, uint32_t address = 0,
                 elero::DeviceType type = elero::DeviceType::COVER, const std::string &id = "");
  void edit_(EditField field, const std::string &value, const std::string &label);
  void apply_edit_();
  void status_(const std::string &text);
  void confirm_(Action action, const std::string &label);
  elero::Device *selected_device_();
  std::vector<elero::Device *> devices_();
  void snapshot_packets_();
  bool save_remote_();

  elero::Elero *hub_{nullptr};
  elero::DeviceRegistry *registry_{nullptr};
  ESPPreferenceObject preferences_;
  std::function<std::string()> list_files_;
  std::function<std::string(bool, const std::string &)> transfer_;
  std::vector<PaperElement> frame_, visible_;
  bool dirty_{true}, navigation_pending_{true}, sniffing_{false};
  Page page_{Page::CHANNELS}, return_page_{Page::CHANNELS};
  uint32_t revision_{0}, frame_revision_{0};
  int page_index_{0};
  uint32_t remote_{0}, selected_{0};
  uint8_t remote_channel_{1};
  elero::DeviceType selected_type_{elero::DeviceType::COVER};
  std::string group_id_, message_, search_, remote_filter_;
  int type_filter_{0}, saved_filter_{0}, sort_{0};
  elero::NvsDeviceConfig draft_{};
  elero::NvsGroupConfig group_draft_{};
  std::vector<uint32_t> selection_;
  struct LoggedPacket {
    uint32_t id;
    elero::RfPacketInfo packet;
  };
  std::deque<LoggedPacket> packets_;
  std::vector<LoggedPacket> packet_view_;
  elero::RfPacketInfo selected_packet_{};
  uint32_t packet_sequence_{0}, packet_filter_{0};
  int log_channel_{-1}, log_type_{-1}, log_command_{-1}, log_state_{-1}, log_sort_{0};
  std::string edit_value_, edit_label_;
  EditField edit_field_{EditField::DEVICE_NAME};
  int keyboard_set_{0};
  bool numeric_{false};
  uint32_t pending_channel_operation_{0};
  Action confirm_action_{};
  std::string confirm_label_;
  elero::LearnInState last_learn_state_{elero::LearnInState::IDLE};
  uint32_t learn_source_{0};
  uint8_t learn_channel_{1};
  uint32_t learn_timeout_{300};
  uint32_t raw_source_{0}, raw_destination_{0};
  uint8_t raw_channel_{1}, raw_command_{elero::packet::command::UP};
  uint8_t raw_type_{elero::packet::msg_type::BUTTON}, raw_type2_{elero::packet::button::TYPE2};
  uint8_t raw_hop_{0}, raw_payload1_{0}, raw_payload2_{0};
  std::vector<std::string> files_;
  std::string backup_file_;
};

}  // namespace esphome::elero_paper
