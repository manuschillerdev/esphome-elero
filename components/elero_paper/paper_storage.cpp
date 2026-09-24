#include "esphome/core/defines.h"
#ifdef USE_ELERO_PAPER_STORAGE
#include "paper_storage.h"
#include "paper_ui.h"
#include "esphome/components/elero_config/config_snapshot.h"

namespace esphome::elero_paper {
using paper_mono_storage::Operation;

std::string PaperStorage::files() {
  return storage_.start(Operation::LIST) ? "Reading backups..." : storage_.error();
}

std::string PaperStorage::transfer(bool restore, const std::string &filename) {
  if (restore)
    return storage_.start(Operation::READ, filename) ? "Reading backup..." : storage_.error();
  auto data = elero::config_snapshot::export_json(*hub_->get_registry(), hub_->get_version());
  return storage_.start(Operation::WRITE, "", std::move(data)) ? "Writing backup..." : storage_.error();
}

void PaperStorage::loop(PaperUi &ui) {
  auto result = storage_.take_result();
  if (!result)
    return;
  std::string message = result->error;
  if (message.empty()) {
    switch (result->operation) {
      case Operation::LIST:
        message = result->files.empty() ? "No JSON backups on this card" : "Choose a backup to restore";
        break;
      case Operation::READ:
        message = import_(result->data);
        break;
      case Operation::WRITE:
        message = "Saved " + result->filename;
        break;
    }
  }
  ui.storage_completed(result->files, message, result->operation == Operation::LIST);
}

std::string PaperStorage::import_(const std::string &data) {
  auto *registry = hub_->get_registry();
  std::string result = "Invalid JSON backup";
  json::parse_json(data, [&](JsonObject root) {
    int version = root["snapshot_version"] | 0;
    if (version < 1 || version > elero::config_snapshot::VERSION || !root["devices"].is<JsonArray>()) {
      result = "Unsupported backup format";
      return false;
    }
    // Validate records before mutating NVS. The shared importer reports capacity/member errors.
    std::string error;
    for (JsonVariant entry : root["devices"].as<JsonArray>()) {
      elero::NvsDeviceConfig config{};
      if (!entry.is<JsonObject>() ||
          !elero::config_snapshot::parse_device_config(entry.as<JsonObject>(), config, error)) {
        result = "Invalid device: " + error;
        return false;
      }
    }
    if (root["groups"].is<JsonArray>())
      for (JsonVariant entry : root["groups"].as<JsonArray>()) {
        elero::NvsGroupConfig config{};
        if (!entry.is<JsonObject>() ||
            !elero::config_snapshot::parse_group_config(entry.as<JsonObject>(), config, error)) {
          result = "Invalid group: " + error;
          return false;
        }
      }
    auto report = elero::config_snapshot::import_json(*registry, root);
    ESP_LOGI("elero.paper.storage", "Import result: %s", report.c_str());
    json::parse_json(report, [&](JsonObject r) {
      auto done = (r["added"] | 0U) + (r["updated"] | 0U) + (r["groups_added"] | 0U) + (r["groups_updated"] | 0U);
      auto skipped = (r["skipped"] | 0U) + (r["groups_skipped"] | 0U);
      result = std::to_string(done) + " restored, " + std::to_string(skipped) + " skipped";
      if (!(r["persisted"] | false)) result = "Restore flash write failed";
      return true;
    });
    return true;
  });
  return result;
}
}  // namespace esphome::elero_paper
#endif
