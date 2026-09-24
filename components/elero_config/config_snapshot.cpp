#include "esphome/core/defines.h"
#ifdef USE_JSON
#include "config_snapshot.h"
#include "esphome/components/elero/elero_strings.h"
#include "esphome/core/application.h"
namespace esphome::elero::config_snapshot {
constexpr uint8_t SNAPSHOT_VERSION = VERSION;
static bool parse_device_type(const char *str, DeviceType &out) {
  if (!str)
    return false;
  for (auto type : {DeviceType::COVER, DeviceType::LIGHT, DeviceType::REMOTE}) {
    if (strcmp(str, device_type_str(type)) == 0) {
      out = type;
      return true;
    }
  }
  return false;
}
static uint32_t parse_hex32(JsonObject root, const char *key) {
  if (root[key].is<const char *>())
    return strtoul(root[key].as<const char *>(), nullptr, 0);
  return root[key] | 0U;
}
static uint8_t parse_hex_or(JsonObject root, const char *key, uint8_t fallback) {
  return root[key].isNull() ? fallback : static_cast<uint8_t>(parse_hex32(root, key));
}
bool parse_device_config(JsonObject root, NvsDeviceConfig &config, std::string &error) {
  if (!parse_device_type(root["device_type"] | "", config.type)) {
    error = "Invalid device_type";
    return false;
  }

  uint32_t dst_addr = parse_hex32(root, "dst_address");
  if (dst_addr == 0) {
    error = "Missing dst_address";
    return false;
  }
  config.dst_address = dst_addr;

  const char *name = root["name"];
  if (name != nullptr) {
    config.set_name(name);
  }

  // Enabled flag (defaults to true if not specified)
  config.set_enabled(root["enabled"] | true);

  // RF params (covers and lights only)
  if (!config.is_remote()) {
    config.src_address = parse_hex32(root, "src_address");
    if (root["channel"])
      config.channel = root["channel"].as<uint8_t>();
    config.hop = parse_hex_or(root, "hop", packet::defaults::HOP);
    config.payload_1 = parse_hex_or(root, "payload_1", packet::defaults::PAYLOAD_1);
    config.payload_2 = parse_hex_or(root, "payload_2", packet::defaults::PAYLOAD_2);
    config.type_byte = parse_hex_or(root, "msg_type", packet::msg_type::COMMAND);
    config.type2 = parse_hex_or(root, "type2", packet::defaults::TYPE2);

    // Timing
    if (root["open_duration_ms"].is<uint32_t>())
      config.open_duration_ms = root["open_duration_ms"].as<uint32_t>();
    if (root["close_duration_ms"].is<uint32_t>())
      config.close_duration_ms = root["close_duration_ms"].as<uint32_t>();

    if (config.is_cover()) {
      config.supports_tilt = (root["supports_tilt"] | false) ? 1 : 0;
    }
    if (config.is_light()) {
      if (root["dim_duration_ms"].is<uint32_t>())
        config.dim_duration_ms = root["dim_duration_ms"].as<uint32_t>();
    }
  }

  return true;
}
bool parse_group_config(JsonObject root, NvsGroupConfig &config, std::string &error) {
  const char *id = root["id"];
  if (id == nullptr || id[0] == '\0') {
    error = "Missing group id";
    return false;
  }
  if (strlen(id) >= NVS_GROUP_ID_MAX) {
    error = "Group id is too long";
    return false;
  }
  config.set_id(id);

  const char *name = root["name"];
  if (name == nullptr || name[0] == '\0') {
    error = "Missing group name";
    return false;
  }
  if (strlen(name) >= NVS_NAME_MAX) {
    error = "Group name is too long";
    return false;
  }
  config.set_name(name);

  if (!root["device_ids"].is<JsonArray>()) {
    error = "Missing device_ids";
    return false;
  }
  JsonArray ids = root["device_ids"].as<JsonArray>();
  if (ids.size() < 2) {
    error = "Group requires at least 2 devices";
    return false;
  }
  if (ids.size() > NVS_GROUP_MAX_MEMBERS) {
    error = "Too many group members";
    return false;
  }

  uint8_t count = 0;
  for (JsonVariant v : ids) {
    uint32_t addr = 0;
    if (v.is<const char *>()) {
      addr = (uint32_t)strtoul(v.as<const char *>(), nullptr, 0);
    } else {
      addr = v.as<uint32_t>();
    }
    if (addr == 0) {
      error = "Invalid device id";
      return false;
    }
    config.device_ids[count++] = addr;
  }
  config.member_count = count;
  return true;
}
void build_device(const NvsDeviceConfig &cfg, JsonObject out) {
  out["device_type"] = device_type_str(cfg.type);
  out["dst_address"] = hex_str(cfg.dst_address);
  out["name"] = cfg.name;
  out["enabled"] = cfg.is_enabled();

  if (!cfg.is_remote()) {
    out["src_address"] = hex_str(cfg.src_address);
    out["channel"] = cfg.channel;
    out["hop"] = hex_str8(cfg.hop);
    out["payload_1"] = hex_str8(cfg.payload_1);
    out["payload_2"] = hex_str8(cfg.payload_2);
    out["msg_type"] = hex_str8(cfg.type_byte);
    out["type2"] = hex_str8(cfg.type2);
  }
  if (cfg.is_cover()) {
    out["open_duration_ms"] = cfg.open_duration_ms;
    out["close_duration_ms"] = cfg.close_duration_ms;
    out["supports_tilt"] = cfg.supports_tilt != 0;
    out["ha_device_class"] = cfg.ha_device_class;
  }
  if (cfg.is_light()) {
    out["dim_duration_ms"] = cfg.dim_duration_ms;
  }
}
std::string export_json(DeviceRegistry &registry_ref, const char *version) {
  return json::build_json([&](JsonObject root) {
    auto *registry = &registry_ref;

    root["snapshot_version"] = SNAPSHOT_VERSION;
    root["exported_at"] = millis();

    JsonObject exporter = root["exporter"].to<JsonObject>();
    exporter["device"] = App.get_name();
    exporter["version"] = version;

    JsonObject hub = root["hub"].to<JsonObject>();
    // Only include the override if one is set; absence == "no override".
    if (registry != nullptr) {
      const std::string &display = registry->hub_display_name();
      // We want the persisted *override*, not the effective name. The display
      // name is override-or-default; if it equals the default, no override is set.
      const std::string &def = registry->hub_default_name();
      if (display != def) {
        hub["name_override"] = display;
      }
    }

    JsonArray devices = root["devices"].to<JsonArray>();
    JsonArray groups = root["groups"].to<JsonArray>();
    if (registry != nullptr) {
      registry->for_each_active([&](const Device &dev) {
        // Skip auto-discovered remotes that haven't been persisted yet.
        if (dev.config.updated_at == 0)
          return;
        JsonObject obj = devices.add<JsonObject>();
        build_device(dev.config, obj);
      });
      registry->for_each_group([&](const NvsGroupConfig &group) {
        JsonObject obj = groups.add<JsonObject>();
        obj["id"] = group.id;
        obj["name"] = group.name;
        JsonArray ids = obj["device_ids"].to<JsonArray>();
        for (uint8_t i = 0; i < group.member_count; ++i) {
          ids.add(hex_str(group.device_ids[i]));
        }
      });
    }
  });
}
std::string import_json(DeviceRegistry &registry_ref, JsonObject snap) {
  auto *registry = &registry_ref;
  uint32_t added = 0;
  uint32_t updated = 0;
  uint32_t skipped = 0;
  uint32_t groups_added = 0;
  uint32_t groups_updated = 0;
  uint32_t groups_skipped = 0;
  bool hub_applied = false;

  // Collect errors as a serialized JSON array (built incrementally to avoid
  // building two ArduinoJson docs at once).
  std::string errors_json = "[";
  auto append_error = [&](int idx, const std::string &msg) {
    if (errors_json.size() > 1)
      errors_json += ',';
    std::string entry = json::build_json([&](JsonObject e) {
      e["index"] = idx;
      e["msg"] = msg;
    });
    errors_json += entry;
  };

  // Apply hub overrides (currently just name_override). set_hub_name_override
  // returns false when the value matches what's already persisted — avoid
  // claiming a no-op as a successful restore in the import_result toast.
  if (snap["hub"].is<JsonObject>()) {
    JsonObject hub_obj = snap["hub"].as<JsonObject>();
    if (hub_obj["name_override"].is<const char *>()) {
      const char *name = hub_obj["name_override"].as<const char *>();
      std::string error;
      hub_applied = registry->set_hub_name_override(name == nullptr ? "" : name, &error);
      if (!error.empty())
        append_error(-1, error);
    }
  }

  // Apply each device through parse_device_config_ + upsert.
  if (snap["devices"].is<JsonArray>()) {
    JsonArray devs = snap["devices"].as<JsonArray>();
    int idx = -1;
    for (JsonVariant v : devs) {
      ++idx;
      if (!v.is<JsonObject>()) {
        append_error(idx, "Device entry is not an object");
        ++skipped;
        continue;
      }
      JsonObject obj = v.as<JsonObject>();
      NvsDeviceConfig cfg{};
      std::string error;
      if (!parse_device_config(obj, cfg, error)) {
        append_error(idx, error);
        ++skipped;
        continue;
      }

      bool was_existing = (registry->find(cfg.dst_address, cfg.type) != nullptr);
      if (registry->upsert(cfg, &error) == nullptr) {
        append_error(idx, error);
        ++skipped;
        continue;
      }
      if (was_existing)
        ++updated;
      else
        ++added;
    }
  }

  if (snap["groups"].is<JsonArray>()) {
    JsonArray group_arr = snap["groups"].as<JsonArray>();
    int idx = -1;
    for (JsonVariant v : group_arr) {
      ++idx;
      if (!v.is<JsonObject>()) {
        append_error(idx, "Group entry is not an object");
        ++groups_skipped;
        continue;
      }
      JsonObject obj = v.as<JsonObject>();
      NvsGroupConfig cfg{};
      std::string error;
      if (!parse_group_config(obj, cfg, error)) {
        append_error(idx, error);
        ++groups_skipped;
        continue;
      }
      bool was_existing = (registry->find_group(cfg.id) != nullptr);
      if (registry->upsert_group(cfg, &error) == nullptr) {
        append_error(idx, error.empty() ? "Failed to upsert group" : error);
        ++groups_skipped;
        continue;
      }
      if (was_existing)
        ++groups_updated;
      else
        ++groups_added;
    }
  }

  const auto persisted = registry->sync_configuration();
  if (!persisted.ok())
    append_error(-1, persisted.message);
  errors_json += "]";

  std::string reply = json::build_json([&](JsonObject r) {
    r["added"] = added;
    r["updated"] = updated;
    r["skipped"] = skipped;
    r["groups_added"] = groups_added;
    r["groups_updated"] = groups_updated;
    r["groups_skipped"] = groups_skipped;
    r["hub_applied"] = hub_applied;
    r["persisted"] = persisted.ok();
    r["errors"] = serialized(errors_json);
  });
  return reply;
}
}  // namespace esphome::elero::config_snapshot
#endif
