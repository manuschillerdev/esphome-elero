#include "paper_ui.h"
#include "paper_text.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace esphome::elero_paper {
using namespace elero;
namespace {
Action field_action(EditField field) {
  return {ActionKind::FIELD, static_cast<int>(field)};
}
std::string hex(uint32_t v) {
  char b[16];
  snprintf(b, sizeof(b), "%06X", v);
  return b;
}
std::string number(uint32_t v) {
  return std::to_string(v);
}
std::string label(const Device &d) {
  return d.config.name[0] ? d.config.name : hex(d.config.dst_address);
}
std::string lower(std::string s) {
  for (auto &c : s)
    c = std::tolower(static_cast<unsigned char>(c));
  return s;
}
std::string state(const Device &d) {
  if (d.config.is_cover()) {
    auto s = compute_cover_snapshot(d, millis());
    return std::string(s.ha_state) + (std::isfinite(s.position) ? " " + number(lroundf(s.position * 100)) + "%" : "");
  }
  if (d.config.is_light()) {
    auto s = compute_light_snapshot(d, millis());
    return s.is_on ? "On" : "Off";
  }
  return "Remote";
}
}  // namespace

void PaperUi::setup(Elero *hub, DeviceRegistry *registry) {
  hub_ = hub;
  registry_ = registry;
  preferences_ = global_preferences->make_preference<uint32_t>(fnv1_hash("paper_selected_remote_v1"));
  if (!preferences_.load(&remote_) || remote_ > 0xFFFFFF)
    remote_ = 0;
  sniffing_ = remote_ == 0;
  ESP_LOGI("elero.paper", "NVS channel source=%06X; sniffing=%d", remote_, sniffing_);
  uint8_t mac[6]{};
  get_mac_address_raw(mac);
  learn_source_ = remote_ ? remote_ : (static_cast<uint32_t>(mac[3]) << 16) | (mac[4] << 8) | mac[5];
  if (!learn_source_)
    learn_source_ = 1;
  raw_source_ = remote_;
}
bool PaperUi::save_remote_() {
  return preferences_.save(&remote_) && global_preferences->sync();
}
void PaperUi::changed() {
  // Packet browsing and edit drafts remain stable as RF updates arrive.
  if (page_ != Page::CHANNELS && page_ != Page::MENU && page_ != Page::KEYBOARD && page_ != Page::PACKETS &&
      page_ != Page::PACKET && page_ != Page::EDIT && page_ != Page::MEMBERS)
    dirty_ = true;
}
void PaperUi::loop() {
  if (hub_->learn_in_state() != last_learn_state_) {
    last_learn_state_ = hub_->learn_in_state();
    if (page_ == Page::LEARN) {
      message_.clear();
      dirty_ = true;
    }
  }
}
void PaperUi::on_packet(const RfPacketInfo &p) {
  packets_.push_front({++packet_sequence_, p});
  if (packets_.size() > 50)
    packets_.pop_back();
  if (sniffing_ && p.crc_ok && p.src &&
      (p.type == packet::msg_type::BUTTON || p.type == packet::msg_type::BUTTON_GROUP ||
       packet::is_command_packet(p.type)) &&
      (p.command == packet::command::UP || p.command == packet::command::DOWN || p.command == packet::command::STOP)) {
    remote_ = p.src;
    sniffing_ = false;
    raw_source_ = remote_;
    learn_source_ = remote_;
    const bool saved = save_remote_();
    message_ = saved ? "Remote saved: " + hex(remote_) : "Remote selected; flash save failed";
    dirty_ = true;
    ++revision_;
    navigation_pending_ = true;
    ESP_LOGD("elero.paper", "Selected remote %06X; saved=%d", remote_, saved);
  }
}
void PaperUi::go_(Page p) {
  page_ = p;
  page_index_ = 0;
  message_.clear();
  dirty_ = true;
  ++revision_;
  navigation_pending_ = true;
}
void PaperUi::status_(const std::string &s) {
  message_ = s;
  dirty_ = true;
}
void PaperUi::storage_completed(const std::vector<std::string> &files, const std::string &message, bool listing) {
  if (listing)
    files_ = files;
  if (page_ == Page::BACKUP) {
    ++revision_;
    navigation_pending_ = true;
    status_(message);
  }
}
void PaperUi::text_(int y, const std::string &s, bool title) {
  // Render truncation is pixel-based in the display; preserve the original label here.
  frame_.push_back({24, y, 432, title ? 45 : 30, s, {}, title});
}
void PaperUi::button_(int x, int y, int w, const std::string &s, Action a, int h) {
  frame_.push_back({x, y, w, h, s, std::move(a), false});
}
void PaperUi::icon_button_(int x, int y, int w, PaperIcon icon, Action action, int h, const std::string &label) {
  frame_.push_back({x, y, w, h, label, std::move(action), false, icon});
}
void PaperUi::row_(int r, const std::string &s, Action a) {
  button_(24, 178 + r * 84, 432, s, std::move(a), 72);
}
void PaperUi::pager_(size_t count, int per_page) {
  int pages = std::max(1, static_cast<int>((count + per_page - 1) / per_page));
  page_index_ = std::clamp(page_index_, 0, pages - 1);
  if (page_index_ > 0)
    icon_button_(24, 650, 120, PaperIcon::PREVIOUS, {ActionKind::PAGE, -1});
  text_(664, "                 " + number(page_index_ + 1) + " / " + number(pages));
  if (page_index_ + 1 < pages)
    icon_button_(336, 650, 120, PaperIcon::NEXT, {ActionKind::PAGE, 1});
}
void PaperUi::controls_(int y, bool light, ActionKind kind, uint32_t address, DeviceType type, const std::string &id) {
  const uint8_t cmds[] = {packet::command::UP, packet::command::STOP, packet::command::DOWN};
  const PaperIcon icons[] = {light ? PaperIcon::LIGHT_ON : PaperIcon::UP, PaperIcon::STOP,
                             light ? PaperIcon::LIGHT_OFF : PaperIcon::DOWN};
  for (int i = 0; i < 3; ++i)
    icon_button_(24 + 148 * i, y, 136, icons[i], {kind, cmds[i], address, type, id}, 70);
}
Device *PaperUi::selected_device_() {
  return registry_->find(selected_, selected_type_);
}
std::vector<Device *> PaperUi::devices_() {
  std::vector<Device *> result;
  registry_->for_each_active([&](Device &d) {
    if (type_filter_ && static_cast<int>(d.config.type) != type_filter_ - 1)
      return;
    if (saved_filter_ == 1 && d.config.updated_at == 0)
      return;
    if (saved_filter_ == 2 && d.config.updated_at != 0)
      return;
    if (!remote_filter_.empty() && lower(hex(d.config.src_address)).find(lower(remote_filter_)) == std::string::npos)
      return;
    if (!search_.empty() && lower(label(d) + " " + hex(d.config.dst_address)).find(lower(search_)) == std::string::npos)
      return;
    result.push_back(&d);
  });
  std::sort(result.begin(), result.end(), [&](const Device *a, const Device *b) {
    if (sort_ == 1)
      return a->config.dst_address < b->config.dst_address;
    if (sort_ == 2)
      return a->rf.last_rssi > b->rf.last_rssi;
    return lower(label(*a)) < lower(label(*b));
  });
  return result;
}
void PaperUi::snapshot_packets_() {
  packet_view_.clear();
  for (const auto &entry : packets_) {
    if (packet_filter_ && entry.packet.src != packet_filter_ && entry.packet.dst != packet_filter_)
      continue;
    if (log_channel_ >= 0 && entry.packet.channel != log_channel_)
      continue;
    if (log_type_ >= 0 && entry.packet.type != log_type_)
      continue;
    if (log_command_ >= 0 && entry.packet.command != log_command_)
      continue;
    if (log_state_ >= 0 && entry.packet.state != log_state_)
      continue;
    packet_view_.push_back(entry);
  }
  if (log_sort_)
    std::stable_sort(packet_view_.begin(), packet_view_.end(),
                     [](const auto &a, const auto &b) { return a.packet.rssi > b.packet.rssi; });
}

void PaperUi::build() {
  frame_.clear();
  dirty_ = false;
  frame_revision_ = revision_;
  icon_button_(24, 18, 136, PaperIcon::CHANNELS, {ActionKind::GO, static_cast<int>(Page::CHANNELS)}, 58, "Channels");
  icon_button_(172, 18, 136, PaperIcon::DEVICES, {ActionKind::GO, static_cast<int>(Page::DEVICES)}, 58, "Devices");
  icon_button_(320, 18, 136, PaperIcon::MENU, {ActionKind::GO, static_cast<int>(Page::MENU)}, 58, "Menu");
  switch (page_) {
    case Page::CHANNELS: {
      text_(92, sniffing_ ? "Press your remote" : "Remote " + hex(remote_), true);
      for (int row = 0; row < 5; ++row) {
        int channel = page_index_ * 5 + row + 1;
        frame_.push_back({24, 197 + row * 84, 74, 30, "CH " + number(channel)});
        const PaperIcon icons[] = {PaperIcon::UP, PaperIcon::STOP, PaperIcon::DOWN};
        const int cmds[] = {packet::command::UP, packet::command::STOP, packet::command::DOWN};
        for (int col = 0; col < 3; ++col)
          icon_button_(104 + col * 116, 178 + row * 84, 108, icons[col],
                       {ActionKind::CHANNEL, cmds[col], remote_, DeviceType::REMOTE, number(channel)}, 68);
      }
      pager_(255);
      button_(24, 716, 432, "Sniff remote", {ActionKind::SNIFF});
      break;
    }
    case Page::DEVICES:
    case Page::MEMBERS: {
      bool members = page_ == Page::MEMBERS;
      text_(92, members ? "Select group members" : "Devices & discovery", true);
      auto devices = devices_();
      if (members) {
        devices.clear();
        registry_->for_each_active([&](Device &d) {
          if (!d.config.is_remote())
            devices.push_back(&d);
        });
      }
      pager_(devices.size());
      for (size_t i = page_index_ * 5; i < devices.size() && i < static_cast<size_t>((page_index_ + 1) * 5); ++i) {
        auto &d = *devices[i];
        bool selected = std::find(selection_.begin(), selection_.end(), d.config.dst_address) != selection_.end();
        row_(i % 5,
             (members ? (selected ? "[x] " : "[ ] ") : "") + label(d) + "\n" + state(d) +
                 (d.config.updated_at ? " / saved" : " / unsaved"),
             {members ? ActionKind::MEMBER : ActionKind::DEVICE, 0, d.config.dst_address, d.config.type});
      }
      if (devices.empty())
        text_(205, "No matches. Operate a remote.");
      if (members)
        button_(24, 716, 432, "Done: " + number(selection_.size()) + " selected",
                {ActionKind::GO, static_cast<int>(Page::GROUP)});
      else {
        button_(24, 716, 136, "Filters", {ActionKind::GO, static_cast<int>(Page::FILTERS)});
        button_(172, 716, 136, "Bulk", {ActionKind::GO, static_cast<int>(Page::BULK)});
        button_(320, 716, 136, "Add", {ActionKind::EDIT, 1});
      }
      break;
    }
    case Page::DEVICE: {
      auto *d = selected_device_();
      if (!d) {
        text_(100, "Device removed", true);
        break;
      }
      text_(92, label(*d), true);
      text_(145, hex(d->config.dst_address) + " / " + device_type_str(d->config.type));
      text_(185, state(*d) + " / RF " + elero_state_to_string(d->rf.last_state_raw));
      text_(225, "Signal " + std::to_string(static_cast<int>(d->rf.last_rssi)) + " dBm");
      if (d->config.is_remote())
        button_(24, 260, 432, "Control channel: " + number(remote_channel_), field_action(EditField::REMOTE_CHANNEL),
                48);
      else
        text_(265, "Remote " + hex(d->config.src_address) + " CH " + number(d->config.channel));
      text_(305, std::string(d->config.updated_at ? "Saved" : "Unsaved") +
                     (d->config.is_enabled() ? " / active" : " / inactive"));
      if (!d->config.is_remote()) {
        controls_(365, d->config.is_light(), ActionKind::COMMAND, selected_, selected_type_);
        button_(24, 455, 208, "Check", {ActionKind::COMMAND, packet::command::CHECK, selected_, selected_type_});
        if (d->config.is_cover() && d->config.supports_tilt)
          button_(248, 455, 208, "Tilt", {ActionKind::COMMAND, packet::command::TILT, selected_, selected_type_});
      } else {
        controls_(365, false, ActionKind::CHANNEL, selected_, DeviceType::REMOTE, number(remote_channel_));
        button_(24, 455, 432, "Use for all channels", {ActionKind::SNIFF, 1, selected_});
      }
      icon_button_(24, 545, d->config.is_remote() ? 432 : 208, PaperIcon::EDIT, {ActionKind::EDIT});
      if (!d->config.is_remote())
        button_(248, 545, 208, "Select", {ActionKind::SELECT, 0, selected_, selected_type_});
      button_(24, 635, 208, "Packets", {ActionKind::LOG_REFRESH, 1, selected_});
      icon_button_(248, 635, 208, PaperIcon::DELETE, {ActionKind::REMOVE, 0, selected_, selected_type_});
      break;
    }
    case Page::EDIT: {
      text_(92, "Device settings", true);
      std::vector<std::pair<std::string, int>> fields = {
          {"Name: " + std::string(draft_.name), 0},
          {"Address: " + hex(draft_.dst_address), 1},
          {"Type: " + std::string(device_type_str(draft_.type)), 2},
          {std::string("Active: ") + (draft_.is_enabled() ? "yes" : "no"), 3}};
      if (!draft_.is_remote()) {
        fields.push_back({"Remote: " + hex(draft_.src_address), 4});
        fields.push_back({"Channel: " + number(draft_.channel), 5});
      }
      if (draft_.is_cover()) {
        fields.push_back({"Open time (ms): " + number(draft_.open_duration_ms), 6});
        fields.push_back({"Close time (ms): " + number(draft_.close_duration_ms), 7});
        fields.push_back({std::string("Tilt: ") + (draft_.supports_tilt ? "yes" : "no"), 8});
      }
      if (draft_.is_light())
        fields.push_back({"Dim time (ms): " + number(draft_.dim_duration_ms), 9});
      pager_(fields.size());
      for (size_t i = page_index_ * 5; i < fields.size() && i < static_cast<size_t>((page_index_ + 1) * 5); ++i)
        row_(i % 5, fields[i].first, {ActionKind::FIELD, fields[i].second});
      button_(24, 716, 432, "Save to NVS", {ActionKind::SAVE});
      break;
    }
    case Page::GROUPS: {
      text_(92, "Groups", true);
      std::vector<NvsGroupConfig> groups;
      registry_->for_each_group([&](const auto &g) { groups.push_back(g); });
      pager_(groups.size());
      for (size_t i = page_index_ * 5; i < groups.size() && i < static_cast<size_t>((page_index_ + 1) * 5); ++i)
        row_(i % 5, std::string(groups[i].name) + "\n" + number(groups[i].member_count) + " members",
             {ActionKind::GROUP, 0, 0, DeviceType::COVER, groups[i].id});
      button_(24, 716, 432, "Create group", {ActionKind::NEW_GROUP});
      break;
    }
    case Page::GROUP: {
      const auto *saved_group = registry_->find_group(group_id_.c_str());
      const auto *member =
          saved_group && saved_group->member_count ? registry_->find(saved_group->device_ids[0]) : nullptr;
      const bool light = member && member->config.is_light();
      bool tilt = saved_group && !light;
      if (saved_group)
        for (uint8_t i = 0; i < saved_group->member_count; ++i) {
          const auto *device = registry_->find(saved_group->device_ids[i]);
          tilt = tilt && device && device->config.supports_tilt;
        }
      text_(92, group_draft_.name[0] ? group_draft_.name : "New group", true);
      text_(145, number(selection_.size()) + " selected members");
      if (saved_group)
        controls_(195, light, ActionKind::GROUP_COMMAND, 0, light ? DeviceType::LIGHT : DeviceType::COVER, group_id_);
      icon_button_(24, 290, 432, PaperIcon::EDIT, field_action(EditField::GROUP_NAME));
      button_(24, 375, 432, "Choose members", {ActionKind::GO, static_cast<int>(Page::MEMBERS)});
      button_(24, 460, 432, "Save group", {ActionKind::SAVE_GROUP});
      if (!group_id_.empty()) {
        if (tilt)
          button_(24, 545, 208, "Tilt",
                  {ActionKind::GROUP_COMMAND, packet::command::TILT, 0, DeviceType::COVER, group_id_});
        icon_button_(248, 545, 208, PaperIcon::DELETE, {ActionKind::REMOVE_GROUP, 0, 0, DeviceType::COVER, group_id_});
      }
      break;
    }
    case Page::MENU: {
      text_(92, "Tools", true);
      const Page pages[] = {Page::GROUPS, Page::HUB, Page::LEARN, Page::PACKETS, Page::RAW, Page::BACKUP};
      const char *names[] = {"Groups", "Hub settings", "Learn-in", "RF packets", "Simulate remote", "Backup / restore"};
      for (int i = 0; i < 6; ++i)
        button_(24, 165 + i * 86, 432, names[i], {ActionKind::GO, static_cast<int>(pages[i])}, 68);
      break;
    }
    case Page::HUB: {
      text_(92, registry_->hub_display_name(), true);
      uint32_t f = (hub_->get_freq2() << 16) | (hub_->get_freq1() << 8) | hub_->get_freq0();
      char freq[48];
      snprintf(freq, sizeof(freq), "SX1262 / %.3f MHz", f * 26.0 / 65536.0);
      text_(155, freq);
      text_(195, "Firmware " + std::string(hub_->get_version()));
      text_(235, number(registry_->count_active()) + " devices / " + number(registry_->count_groups()) + " groups");
      text_(275, "NVS enabled / local frontend");
      icon_button_(24, 340, 432, PaperIcon::EDIT, field_action(EditField::HUB_NAME));
      button_(24, 425, 432, "Check all devices", {ActionKind::CHECK_ALL});
      button_(24, 510, 432, "Backup / restore", {ActionKind::GO, static_cast<int>(Page::BACKUP)});
      button_(24, 595, 432, "Restart", {ActionKind::RESTART});
      break;
    }
    case Page::LEARN: {
      text_(92, "Learn-in", true);
      text_(145, learn_in_state_str(hub_->learn_in_state()));
      text_(185, "Put the receiver in learn mode.");
      button_(24, 235, 432, "Source: " + hex(learn_source_), field_action(EditField::LEARN_SOURCE));
      button_(24, 315, 208, "CH " + number(learn_channel_), field_action(EditField::LEARN_CHANNEL));
      button_(248, 315, 208, number(learn_timeout_) + " seconds", field_action(EditField::LEARN_TIMEOUT));
      if (!hub_->is_learn_in_active())
        button_(24, 405, 432, "Start learn-in", {ActionKind::LEARN_START});
      else {
        if (hub_->learn_in_state() == LearnInState::WAIT_UP)
          button_(24, 405, 432, "Confirm UP movement", {ActionKind::LEARN_UP});
        if (hub_->learn_in_state() == LearnInState::WAIT_DOWN)
          button_(24, 405, 432, "Confirm DOWN movement", {ActionKind::LEARN_DOWN});
        button_(24, 510, 432, "Cancel learn-in", {ActionKind::LEARN_CANCEL});
      }
      break;
    }
    case Page::RAW: {
      text_(92, "Simulate remote", true);
      std::vector<std::pair<std::string, int>> fields = {
          {"Source: " + hex(raw_source_), 40},           {"Destination: " + hex(raw_destination_), 41},
          {"Channel: " + number(raw_channel_), 42},      {"Command (hex): " + hex(raw_command_), 43},
          {"Envelope (hex): " + hex(raw_type_), 44},     {"Type2 (hex): " + hex(raw_type2_), 45},
          {"Hop (hex): " + hex(raw_hop_), 46},           {"Payload 1 (hex): " + hex(raw_payload1_), 47},
          {"Payload 2 (hex): " + hex(raw_payload2_), 48}};
      pager_(fields.size());
      for (size_t i = page_index_ * 5; i < fields.size() && i < static_cast<size_t>((page_index_ + 1) * 5); ++i)
        row_(i % 5, fields[i].first, {ActionKind::FIELD, fields[i].second});
      button_(24, 716, 432, "Send command", {ActionKind::RAW_SEND});
      break;
    }
    case Page::PACKETS: {
      text_(92, "RF packets", true);
      text_(140, "Frozen list / refresh for latest");
      pager_(packet_view_.size());
      for (size_t i = page_index_ * 5; i < packet_view_.size() && i < static_cast<size_t>((page_index_ + 1) * 5); ++i) {
        const auto &entry = packet_view_[i];
        const auto &p = entry.packet;
        row_(i % 5,
             hex(p.src) + " > " + hex(p.dst) + "\nCH " + number(p.channel) + " / " + elero_command_to_string(p.command),
             {ActionKind::PACKET, static_cast<int>(entry.id)});
      }
      button_(24, 716, 136, "Refresh", {ActionKind::LOG_REFRESH});
      button_(172, 716, 136, "Filter", {ActionKind::GO, static_cast<int>(Page::LOG_FILTERS)});
      button_(320, 716, 136, "Clear", {ActionKind::LOG_CLEAR});
      break;
    }
    case Page::PACKET: {
      auto &p = selected_packet_;
      text_(92, "Packet details", true);
      text_(165, hex(p.src) + " > " + hex(p.dst));
      text_(210, "Channel " + number(p.channel) + " / counter " + number(p.cnt));
      text_(255, "Type " + hex(p.type) + " / type2 " + hex(p.type2));
      text_(300, "Command " + hex(p.command) + " / state " + hex(p.state));
      text_(345, "RSSI " + std::to_string(static_cast<int>(p.rssi)) + " dBm / LQI " + number(p.lqi));
      text_(390, "Hop " + number(p.hop) + " / CRC " + (p.crc_ok ? std::string("OK") : "bad"));
      text_(435, "Received at " + number(p.timestamp_ms / 1000) + "s uptime");
      if (packet::is_command_packet(p.type) || p.type == packet::msg_type::BUTTON)
        button_(24, 535, 432, "Replay command", {ActionKind::REPLAY});
      button_(24, 635, 432, "Back to packets", {ActionKind::GO, static_cast<int>(Page::PACKETS)});
      break;
    }
    case Page::LOG_FILTERS: {
      text_(92, "Packet filters", true);
      row_(0, "Address: " + (packet_filter_ ? hex(packet_filter_) : "Any"), field_action(EditField::PACKET_ADDRESS));
      row_(1, "Channel: " + (log_channel_ < 0 ? "Any" : number(log_channel_)), field_action(EditField::PACKET_CHANNEL));
      row_(2, "Type: " + (log_type_ < 0 ? "Any" : hex(log_type_)), field_action(EditField::PACKET_TYPE));
      row_(3, "Command: " + (log_command_ < 0 ? "Any" : hex(log_command_)), field_action(EditField::PACKET_COMMAND));
      row_(4, "State: " + (log_state_ < 0 ? "Any" : hex(log_state_)), field_action(EditField::PACKET_STATE));
      button_(24, 650, 208, log_sort_ ? "Sort: signal" : "Sort: latest", {ActionKind::FILTER, 4});
      button_(248, 650, 208, "Apply", {ActionKind::LOG_REFRESH, 2});
      break;
    }
    case Page::FILTERS: {
      text_(92, "Device filters", true);
      row_(0, "Search: " + search_, field_action(EditField::SEARCH));
      const char *types[] = {"All", "Covers", "Lights", "Remotes"};
      row_(1, std::string("Type: ") + types[type_filter_], {ActionKind::FILTER, 0});
      const char *saved[] = {"All", "Saved", "Unsaved"};
      row_(2, std::string("Status: ") + saved[saved_filter_], {ActionKind::FILTER, 1});
      row_(3, "Remote: " + remote_filter_, field_action(EditField::REMOTE_FILTER));
      const char *sort[] = {"Name", "Address", "Signal"};
      row_(4, std::string("Sort: ") + sort[sort_], {ActionKind::FILTER, 2});
      button_(24, 650, 208, "Reset", {ActionKind::FILTER, 3});
      button_(248, 650, 208, "Apply", {ActionKind::GO, static_cast<int>(Page::DEVICES)});
      break;
    }
    case Page::BULK: {
      text_(92, "Selected devices", true);
      text_(155, number(selection_.size()) + " selected");
      controls_(225, false, ActionKind::BULK);
      button_(24, 340, 432, "Select all filtered devices", {ActionKind::SELECT, 1});
      button_(24, 425, 432, "Clear selection", {ActionKind::SELECT, 2});
      button_(24, 510, 432, "Create group from selection", {ActionKind::NEW_GROUP, 1});
      break;
    }
    case Page::KEYBOARD: {
      text_(92, edit_label_, true);
      text_(150, edit_value_.empty() ? "_" : edit_value_);
      const std::string keys = numeric_             ? "1234567890ABCDEF."
                               : keyboard_set_ == 0 ? "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               : keyboard_set_ == 1 ? "abcdefghijklmnopqrstuvwxyz"
                                                    : "0123456789-_.";
      int columns = numeric_ ? 4 : 6;
      int w = 432 / columns;
      for (size_t i = 0; i < keys.size(); ++i)
        button_(24 + (i % columns) * w, 215 + (i / columns) * 60, w - 4, std::string(1, keys[i]),
                {ActionKind::KEY, keys[i]}, 54);
      button_(24, 555, 102, "Space", {ActionKind::KEY, ' '});
      button_(134, 555, 102, "Delete", {ActionKind::KEY, -1});
      button_(244, 555, 102, "Clear", {ActionKind::KEY, -3});
      button_(354, 555, 102, "Keys", {ActionKind::KEY, -2});
      button_(24, 635, 208, "Cancel", {ActionKind::KEY_CANCEL});
      button_(248, 635, 208, "Done", {ActionKind::KEY_SAVE});
      break;
    }
    case Page::CONFIRM:
      text_(92, "Confirm", true);
      text_(185, confirm_label_);
      text_(240, "This action changes the hub.");
      button_(24, 365, 208, "Cancel", {ActionKind::KEY_CANCEL});
      button_(248, 365, 208, "Confirm", {ActionKind::CONFIRM});
      break;
    case Page::BACKUP:
      text_(92, "Backup / restore", true);
      if (!transfer_) {
        text_(185, "microSD support is not enabled");
        break;
      }
      text_(145, "JSON compatible with the web UI");
      button_(24, 190, 432, "Export to microSD", {ActionKind::EXPORT});
      pager_(files_.size(), 4);
      for (size_t i = page_index_ * 4; i < files_.size() && i < static_cast<size_t>((page_index_ + 1) * 4); ++i)
        button_(24, 280 + (i % 4) * 84, 432, files_[i], {ActionKind::FILE, 0, 0, DeviceType::COVER, files_[i]}, 72);
      button_(24, 716, 432, "Refresh files", {ActionKind::GO, static_cast<int>(Page::BACKUP)});
      break;
  }
  if (!message_.empty()) {
    frame_.erase(std::remove_if(frame_.begin(), frame_.end(),
                                [](const PaperElement &item) {
                                  return item.action.kind == ActionKind::NONE && item.y >= 140 && item.y < 172;
                                }),
                 frame_.end());
    text_(142, message_);
  }
}
void PaperUi::present() {
  visible_ = frame_;
  navigation_pending_ = revision_ != frame_revision_;
}
void PaperUi::touch(uint16_t x, uint16_t y) {
  for (auto it = visible_.rbegin(); it != visible_.rend(); ++it) {
    if (it->action.kind == ActionKind::NONE || x < it->x || x >= it->x + it->width || y < it->y ||
        y >= it->y + it->height)
      continue;
    bool stop = (it->action.kind == ActionKind::COMMAND || it->action.kind == ActionKind::CHANNEL ||
                 it->action.kind == ActionKind::GROUP_COMMAND || it->action.kind == ActionKind::BULK) &&
                it->action.value == packet::command::STOP;
    if (navigation_pending_ && !stop)
      return;
    execute_(it->action);
    return;
  }
}

void PaperUi::edit_(EditField field, const std::string &value, const std::string &name) {
  return_page_ = page_;
  edit_field_ = field;
  edit_value_ = value;
  edit_label_ = name;
  numeric_ = field_spec(field).format != FieldFormat::TEXT;
  keyboard_set_ = 0;
  go_(Page::KEYBOARD);
}
void PaperUi::confirm_(Action action, const std::string &name) {
  return_page_ = page_;
  confirm_action_ = std::move(action);
  confirm_label_ = name;
  go_(Page::CONFIRM);
}
void PaperUi::on_channel_command_result(const ChannelCommandResult &result) {
  if (result.operation_id != pending_channel_operation_ || result.status == TransmissionStatus::QUEUED)
    return;
  pending_channel_operation_ = 0;
  switch (result.status) {
    case TransmissionStatus::TRANSMITTED:
      status_("Command transmitted");
      break;
    case TransmissionStatus::FAILED:
      status_("Transmission failed");
      break;
    case TransmissionStatus::CANCELLED:
      status_("Command replaced by Stop");
      break;
    case TransmissionStatus::QUEUED:
      break;
  }
}
void PaperUi::execute_(Action a) {
  switch (a.kind) {
    case ActionKind::GO: {
      Page next = static_cast<Page>(a.value);
      if (next == Page::PACKETS && page_ != Page::PACKET) {
        packet_filter_ = 0;
        snapshot_packets_();
      }
      go_(next);
      if (next == Page::BACKUP && list_files_)
        status_(list_files_());
      break;
    }
    case ActionKind::PAGE:
      page_index_ = std::max(0, page_index_ + a.value);
      dirty_ = true;
      ++revision_;
      navigation_pending_ = true;
      break;
    case ActionKind::DEVICE:
      selected_ = a.address;
      selected_type_ = a.type;
      if (auto *d = selected_device_(); d && d->config.is_remote()) {
        auto channel = std::get<RemoteDevice>(d->logic).last_channel;
        remote_channel_ = channel ? channel : 1;
      }
      go_(Page::DEVICE);
      break;
    case ActionKind::COMMAND:
      if (auto *d = registry_->find(a.address, a.type)) {
        status_(registry_->command_device(*d, a.value).message);
      } else
        status_("Device no longer exists");
      break;
    case ActionKind::CHANNEL: {
      if (!a.address || (page_ == Page::CHANNELS && sniffing_)) {
        status_("Sniff a remote first");
        break;
      }
      const auto result = hub_->request_channel_command(a.address, std::strtoul(a.id.c_str(), nullptr, 10), a.value);
      if (result.ok())
        pending_channel_operation_ = result.operation_id;
      status_(result.message);
      break;
    }
    case ActionKind::SNIFF:
      if (a.value == 1) {
        remote_ = a.address;
        sniffing_ = false;
        const bool saved = save_remote_();
        go_(Page::CHANNELS);
        status_(saved ? "Remote saved" : "Remote selected; flash save failed");
        break;
      } else
        sniffing_ = true;
      go_(Page::CHANNELS);
      break;
    case ActionKind::EDIT:
      if (a.value == 1) {
        draft_ = {};
        draft_.src_address = remote_;
        draft_.channel = 1;
        selected_ = 0;
      } else if (auto *d = selected_device_())
        draft_ = d->config;
      else {
        status_("Device no longer exists");
        break;
      }
      go_(Page::EDIT);
      break;
    case ActionKind::FIELD: {
      switch (static_cast<EditField>(a.value)) {
        case EditField::DEVICE_NAME:
          edit_(EditField::DEVICE_NAME, draft_.name, "Device name");
          break;
        case EditField::DEVICE_ADDRESS:
          if (selected_)
            status_("Existing address is fixed");
          else
            edit_(EditField::DEVICE_ADDRESS, hex(draft_.dst_address), "Address (hex)");
          break;
        case EditField::DEVICE_TYPE:
          if (selected_)
            status_("Existing type is fixed");
          else {
            draft_.type = static_cast<DeviceType>((static_cast<int>(draft_.type) + 1) % 3);
            dirty_ = true;
          }
          break;
        case EditField::DEVICE_ENABLED:
          draft_.set_enabled(!draft_.is_enabled());
          dirty_ = true;
          break;
        case EditField::DEVICE_REMOTE:
          edit_(EditField::DEVICE_REMOTE, hex(draft_.src_address), "Remote (hex)");
          break;
        case EditField::DEVICE_CHANNEL:
          edit_(EditField::DEVICE_CHANNEL, number(draft_.channel), "Channel 1-255");
          break;
        case EditField::OPEN_DURATION:
          edit_(EditField::OPEN_DURATION, number(draft_.open_duration_ms), "Open time in ms");
          break;
        case EditField::CLOSE_DURATION:
          edit_(EditField::CLOSE_DURATION, number(draft_.close_duration_ms), "Close time in ms");
          break;
        case EditField::DEVICE_TILT:
          draft_.supports_tilt = !draft_.supports_tilt;
          dirty_ = true;
          break;
        case EditField::DIM_DURATION:
          edit_(EditField::DIM_DURATION, number(draft_.dim_duration_ms), "Dim time in ms");
          break;
        case EditField::GROUP_NAME:
          edit_(EditField::GROUP_NAME, group_draft_.name, "Group name");
          break;
        case EditField::HUB_NAME:
          edit_(EditField::HUB_NAME, registry_->hub_display_name(), "Hub name");
          break;
        case EditField::SEARCH:
          edit_(EditField::SEARCH, search_, "Search devices");
          break;
        case EditField::REMOTE_FILTER:
          edit_(EditField::REMOTE_FILTER, remote_filter_, "Remote filter (hex)");
          break;
        case EditField::LEARN_SOURCE:
          edit_(EditField::LEARN_SOURCE, hex(learn_source_), "Learn source (hex)");
          break;
        case EditField::LEARN_CHANNEL:
          edit_(EditField::LEARN_CHANNEL, number(learn_channel_), "Learn channel");
          break;
        case EditField::LEARN_TIMEOUT:
          edit_(EditField::LEARN_TIMEOUT, number(learn_timeout_), "Timeout seconds");
          break;
        case EditField::RAW_SOURCE:
          edit_(EditField::RAW_SOURCE, hex(raw_source_), "Source (hex)");
          break;
        case EditField::RAW_DESTINATION:
          edit_(EditField::RAW_DESTINATION, hex(raw_destination_), "Destination (hex)");
          break;
        case EditField::RAW_CHANNEL:
          edit_(EditField::RAW_CHANNEL, number(raw_channel_), "Channel");
          break;
        case EditField::RAW_COMMAND:
          edit_(EditField::RAW_COMMAND, hex(raw_command_), "Command (hex)");
          break;
        case EditField::RAW_TYPE:
          edit_(EditField::RAW_TYPE, hex(raw_type_), "Envelope (hex)");
          break;
        case EditField::RAW_TYPE2:
          edit_(EditField::RAW_TYPE2, hex(raw_type2_), "Type2 (hex)");
          break;
        case EditField::RAW_HOP:
          edit_(EditField::RAW_HOP, hex(raw_hop_), "Hop (hex)");
          break;
        case EditField::RAW_PAYLOAD1:
          edit_(EditField::RAW_PAYLOAD1, hex(raw_payload1_), "Payload 1 (hex)");
          break;
        case EditField::RAW_PAYLOAD2:
          edit_(EditField::RAW_PAYLOAD2, hex(raw_payload2_), "Payload 2 (hex)");
          break;
        case EditField::PACKET_ADDRESS:
          edit_(EditField::PACKET_ADDRESS, packet_filter_ ? hex(packet_filter_) : "", "Address / blank = any");
          break;
        case EditField::PACKET_CHANNEL:
          edit_(EditField::PACKET_CHANNEL, log_channel_ < 0 ? "" : number(log_channel_), "Channel / blank = any");
          break;
        case EditField::PACKET_TYPE:
          edit_(EditField::PACKET_TYPE, log_type_ < 0 ? "" : hex(log_type_), "Type hex / blank = any");
          break;
        case EditField::PACKET_COMMAND:
          edit_(EditField::PACKET_COMMAND, log_command_ < 0 ? "" : hex(log_command_), "Cmd hex / blank = any");
          break;
        case EditField::PACKET_STATE:
          edit_(EditField::PACKET_STATE, log_state_ < 0 ? "" : hex(log_state_), "State hex / blank = any");
          break;
        case EditField::REMOTE_CHANNEL:
          edit_(EditField::REMOTE_CHANNEL, number(remote_channel_), "Control channel 1-255");
          break;
      }
      break;
    }
    case ActionKind::SAVE: {
      const auto result = registry_->save_device(draft_);
      if (!result.ok()) {
        status_(result.message);
        break;
      }
      selected_ = draft_.dst_address;
      selected_type_ = draft_.type;
      go_(Page::DEVICE);
      status_(result.message);
      break;
    }
    case ActionKind::REMOVE:
      if (!a.value) {
        a.value = 1;
        confirm_(a, "Delete device " + hex(a.address) + "?");
      } else {
        const auto result = registry_->delete_device(a.address, a.type);
        go_(Page::DEVICES);
        status_(result.ok() ? "Device deleted" : result.message);
      }
      break;
    case ActionKind::GROUP:
      if (auto *g = registry_->find_group(a.id.c_str())) {
        group_draft_ = *g;
        group_id_ = g->id;
        selection_.assign(g->device_ids, g->device_ids + g->member_count);
        go_(Page::GROUP);
      }
      break;
    case ActionKind::NEW_GROUP:
      group_id_.clear();
      group_draft_ = {};
      if (!a.value)
        selection_.clear();
      {
        char id[24];
        snprintf(id, sizeof(id), "paper-%08X", random_uint32());
        group_draft_.set_id(id);
      }
      group_draft_.set_name("New group");
      go_(Page::GROUP);
      break;
    case ActionKind::MEMBER:
    case ActionKind::SELECT: {
      if (a.kind == ActionKind::SELECT && a.value == 1) {
        selection_.clear();
        for (auto *d : devices_())
          if (!d->config.is_remote())
            selection_.push_back(d->config.dst_address);
      } else if (a.kind == ActionKind::SELECT && a.value == 2)
        selection_.clear();
      else {
        auto *d = registry_->find(a.address);
        if (!d || d->config.is_remote()) {
          status_("Select covers or lights");
          break;
        }
        auto it = std::find(selection_.begin(), selection_.end(), a.address);
        if (it != selection_.end())
          selection_.erase(it);
        else
          selection_.push_back(a.address);
      }
      dirty_ = true;
      if (page_ == Page::DEVICE)
        status_(number(selection_.size()) + " selected for bulk / groups");
      break;
    }
    case ActionKind::SAVE_GROUP: {
      if (selection_.size() < 2 || selection_.size() > NVS_GROUP_MAX_MEMBERS || !group_draft_.name[0]) {
        status_("Name and at least 2 members required");
        break;
      }
      group_draft_.member_count = selection_.size();
      std::copy(selection_.begin(), selection_.end(), group_draft_.device_ids);
      const auto result = registry_->save_group(group_draft_);
      if (!result.ok())
        status_(result.message);
      else {
        group_id_ = group_draft_.id;
        status_("Group saved");
      }
      break;
    }
    case ActionKind::GROUP_COMMAND: {
      status_(registry_->send_group_command(a.id.c_str(), a.value).message);
      break;
    }
    case ActionKind::REMOVE_GROUP:
      if (!a.value) {
        a.value = 1;
        confirm_(a, "Delete this group?");
      } else {
        const auto result = registry_->delete_group(a.id.c_str());
        go_(Page::GROUPS);
        status_(result.ok() ? "Group deleted" : result.message);
      }
      break;
    case ActionKind::BULK: {
      size_t queued = 0;
      for (auto id : selection_)
        if (auto *d = registry_->find(id); d && registry_->command_device(*d, a.value).ok())
          ++queued;
      status_(number(queued) + " queued, " + number(selection_.size() - queued) + " rejected");
      break;
    }
    case ActionKind::FILTER:
      if (a.value == 0)
        type_filter_ = (type_filter_ + 1) % 4;
      if (a.value == 1)
        saved_filter_ = (saved_filter_ + 1) % 3;
      if (a.value == 2)
        sort_ = (sort_ + 1) % 3;
      if (a.value == 4)
        log_sort_ = !log_sort_;
      if (a.value == 3) {
        type_filter_ = saved_filter_ = sort_ = 0;
        search_.clear();
        remote_filter_.clear();
      }
      dirty_ = true;
      break;
    case ActionKind::KEY:
      if (a.value == -1)
        pop_codepoint(edit_value_);
      else if (a.value == -3)
        edit_value_.clear();
      else if (a.value == -2) {
        keyboard_set_ = (keyboard_set_ + 1) % 3;
        ++revision_;
        navigation_pending_ = true;
      } else if (edit_value_.size() < (numeric_ ? 8U : 23U))
        edit_value_ += static_cast<char>(a.value);
      dirty_ = true;
      break;
    case ActionKind::KEY_CANCEL:
      go_(return_page_);
      break;
    case ActionKind::KEY_SAVE:
      apply_edit_();
      break;
    case ActionKind::CHECK_ALL: {
      size_t queued = 0, rejected = 0;
      registry_->for_each_active([&](Device &d) {
        if (!d.config.is_remote()) {
          if (registry_->command_device(d, packet::command::CHECK).ok())
            ++queued;
          else
            ++rejected;
        }
      });
      status_(number(queued) + " checks queued, " + number(rejected) + " rejected");
      break;
    }
    case ActionKind::RESTART:
      if (!a.value) {
        a.value = 1;
        confirm_(a, "Save and restart the hub?");
      } else if (global_preferences->sync())
        App.safe_reboot();
      else
        status_("Flash write failed; restart cancelled");
      break;
    case ActionKind::LEARN_START: {
      LearnInStartRequest request{};
      request.src_addr = learn_source_;
      request.channel = learn_channel_;
      request.session_timeout_ms = learn_timeout_ * 1000;
      status_(hub_->start_learn_in(request) ? "Learn-in started" : "Could not start learn-in");
      break;
    }
    case ActionKind::LEARN_UP:
      status_(hub_->confirm_learn_in_up() ? "UP confirmed" : "Not ready for UP");
      break;
    case ActionKind::LEARN_DOWN:
      status_(hub_->confirm_learn_in_down() ? "DOWN confirmed" : "Not ready for DOWN");
      break;
    case ActionKind::LEARN_CANCEL:
      hub_->cancel_learn_in();
      status_("Learn-in cancelled");
      break;
    case ActionKind::RAW_SEND:
      if (!raw_source_ || (raw_type_ != packet::msg_type::BUTTON && !raw_destination_)) {
        status_("Source / destination required");
        break;
      }
      if (raw_type_ != packet::msg_type::BUTTON && raw_type_ != packet::msg_type::COMMAND &&
          raw_type_ != packet::msg_type::COMMAND_ALT) {
        status_("Use envelope 44, 6A or 69");
        break;
      }
      if (!a.value) {
        a.value = 1;
        confirm_(a, "Transmit this raw command?");
      } else {
        go_(Page::RAW);
        bool ok = hub_->send_raw_command(raw_destination_, raw_source_, raw_channel_, raw_command_, raw_payload1_,
                                         raw_payload2_, raw_type_, raw_type2_, raw_hop_);
        status_(ok ? "Raw command queued" : "Radio queue full");
      }
      break;
    case ActionKind::LOG_REFRESH:
      if (a.value == 1) {
        packet_filter_ = a.address;
        go_(Page::PACKETS);
      }
      if (a.value == 2)
        go_(Page::PACKETS);
      snapshot_packets_();
      dirty_ = true;
      ++revision_;
      navigation_pending_ = true;
      break;
    case ActionKind::LOG_CLEAR:
      packets_.clear();
      packet_view_.clear();
      dirty_ = true;
      ++revision_;
      navigation_pending_ = true;
      break;
    case ActionKind::PACKET:
      for (const auto &p : packet_view_)
        if (p.id == static_cast<uint32_t>(a.value)) {
          selected_packet_ = p.packet;
          go_(Page::PACKET);
          break;
        }
      break;
    case ActionKind::REPLAY:
      raw_source_ = selected_packet_.src;
      raw_destination_ = selected_packet_.dst;
      raw_channel_ = selected_packet_.channel;
      raw_command_ = selected_packet_.command;
      raw_type_ = selected_packet_.type;
      raw_type2_ = selected_packet_.type2;
      raw_hop_ = selected_packet_.hop;
      raw_payload1_ = selected_packet_.payload[0];
      raw_payload2_ = selected_packet_.payload[1];
      go_(Page::RAW);
      status_("Review, then Send command");
      break;
    case ActionKind::CONFIRM: {
      Action pending = confirm_action_;
      go_(return_page_);
      execute_(pending);
      break;
    }
    case ActionKind::EXPORT:
      status_(transfer_ ? transfer_(false, "") : "Storage unavailable");
      break;
    case ActionKind::FILE:
      backup_file_ = a.id;
      confirm_({ActionKind::IMPORT}, "Restore " + a.id + "?");
      break;
    case ActionKind::IMPORT:
      go_(Page::BACKUP);
      status_(transfer_ ? transfer_(true, backup_file_) : "Storage unavailable");
      break;
    case ActionKind::NONE:
      break;
  }
}

void PaperUi::apply_edit_() {
  uint32_t n = 0;
  const auto spec = field_spec(edit_field_);
  if (numeric_ && !edit_value_.empty()) {
    const bool hexadecimal = spec.format == FieldFormat::HEXADECIMAL;
    const bool valid_digits = std::all_of(edit_value_.begin(), edit_value_.end(), [hexadecimal](unsigned char c) {
      return hexadecimal ? std::isxdigit(c) : std::isdigit(c);
    });
    if (!valid_digits) {
      status_("Enter digits only");
      return;
    }
    char *end = nullptr;
    const unsigned long value = std::strtoul(edit_value_.c_str(), &end, hexadecimal ? 16 : 10);
    if (!end || *end || value < spec.minimum || value > spec.maximum) {
      status_("Invalid value / out of range");
      return;
    }
    n = value;
  } else if (numeric_ && !spec.optional) {
    status_("Enter a value");
    return;
  }
  switch (edit_field_) {
    case EditField::DEVICE_NAME:
      draft_.set_name(edit_value_.c_str());
      break;
    case EditField::DEVICE_ADDRESS:
      draft_.dst_address = n;
      break;
    case EditField::DEVICE_REMOTE:
      draft_.src_address = n;
      break;
    case EditField::DEVICE_CHANNEL:
      draft_.channel = n;
      break;
    case EditField::OPEN_DURATION:
      draft_.open_duration_ms = n;
      break;
    case EditField::CLOSE_DURATION:
      draft_.close_duration_ms = n;
      break;
    case EditField::DIM_DURATION:
      draft_.dim_duration_ms = n;
      break;
    case EditField::GROUP_NAME:
      group_draft_.set_name(edit_value_.c_str());
      break;
    case EditField::HUB_NAME: {
      const auto result = registry_->save_hub_name(edit_value_);
      if (!result.ok()) {
        status_(result.message);
        return;
      }
      break;
    }
    case EditField::SEARCH:
      search_ = edit_value_;
      break;
    case EditField::REMOTE_FILTER:
      remote_filter_ = edit_value_;
      break;
    case EditField::LEARN_SOURCE:
      learn_source_ = n;
      break;
    case EditField::LEARN_CHANNEL:
      learn_channel_ = n;
      break;
    case EditField::LEARN_TIMEOUT:
      learn_timeout_ = n;
      break;
    case EditField::RAW_SOURCE:
      raw_source_ = n;
      break;
    case EditField::RAW_DESTINATION:
      raw_destination_ = n;
      break;
    case EditField::RAW_CHANNEL:
      raw_channel_ = n;
      break;
    case EditField::RAW_COMMAND:
      raw_command_ = n;
      break;
    case EditField::RAW_TYPE:
      raw_type_ = n;
      break;
    case EditField::RAW_TYPE2:
      raw_type2_ = n;
      break;
    case EditField::RAW_HOP:
      raw_hop_ = n;
      break;
    case EditField::RAW_PAYLOAD1:
      raw_payload1_ = n;
      break;
    case EditField::RAW_PAYLOAD2:
      raw_payload2_ = n;
      break;
    case EditField::PACKET_ADDRESS:
      packet_filter_ = n;
      break;
    case EditField::PACKET_CHANNEL:
      log_channel_ = edit_value_.empty() ? -1 : n;
      break;
    case EditField::PACKET_TYPE:
      log_type_ = edit_value_.empty() ? -1 : n;
      break;
    case EditField::PACKET_COMMAND:
      log_command_ = edit_value_.empty() ? -1 : n;
      break;
    case EditField::PACKET_STATE:
      log_state_ = edit_value_.empty() ? -1 : n;
      break;
    case EditField::REMOTE_CHANNEL:
      remote_channel_ = n;
      break;
    case EditField::DEVICE_TYPE:
    case EditField::DEVICE_ENABLED:
    case EditField::DEVICE_TILT:
      break;
  }
  go_(return_page_);
}
}  // namespace esphome::elero_paper
