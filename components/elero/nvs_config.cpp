#include "nvs_config.h"
#include "esphome/core/log.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <algorithm>

namespace esphome::elero {

static constexpr const char *TAG = "elero.nvs";
static constexpr const char *NVS_NAMESPACE = "elero";

const char *EspNvsStorage::type_key(DeviceType type) {
  switch (type) {
    case DeviceType::COVER: return "covers";
    case DeviceType::LIGHT: return "lights";
    case DeviceType::REMOTE: return "remotes";
    default: return "unknown";
  }
}

const char *EspNvsStorage::count_key(DeviceType type) {
  switch (type) {
    case DeviceType::COVER: return "num_cvr";
    case DeviceType::LIGHT: return "num_lgt";
    case DeviceType::REMOTE: return "num_rmt";
    default: return "num_unk";
  }
}

bool EspNvsStorage::load_devices(DeviceType type, std::vector<NvsDeviceConfig> &out) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return true;  // No data yet, not an error
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return false;
  }

  uint8_t count = 0;
  err = nvs_get_u8(handle, count_key(type), &count);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return true;  // No devices stored
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read count for %s: %s", type_key(type), esp_err_to_name(err));
    nvs_close(handle);
    return false;
  }

  size_t blob_size = count * sizeof(NvsDeviceConfig);
  std::vector<NvsDeviceConfig> raw(count);
  err = nvs_get_blob(handle, type_key(type), raw.data(), &blob_size);
  nvs_close(handle);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read %s blob: %s", type_key(type), esp_err_to_name(err));
    return false;
  }

  // Validate entries
  for (auto &cfg : raw) {
    if (cfg.is_valid()) {
      out.push_back(cfg);
    } else {
      ESP_LOGW(TAG, "Skipping invalid %s entry (v%d, addr=0x%06x)", type_key(type), cfg.version, cfg.dst_address);
    }
  }

  ESP_LOGI(TAG, "Loaded %d %s from NVS", out.size(), type_key(type));
  return true;
}

bool EspNvsStorage::save_devices(DeviceType type, const std::vector<NvsDeviceConfig> &configs) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
    return false;
  }

  uint8_t count = configs.size();
  err = nvs_set_u8(handle, count_key(type), count);
  if (err != ESP_OK) {
    nvs_close(handle);
    return false;
  }

  if (count > 0) {
    err = nvs_set_blob(handle, type_key(type), configs.data(), count * sizeof(NvsDeviceConfig));
    if (err != ESP_OK) {
      nvs_close(handle);
      return false;
    }
  } else {
    nvs_erase_key(handle, type_key(type));
  }

  err = nvs_commit(handle);
  nvs_close(handle);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGD(TAG, "Saved %d %s to NVS", count, type_key(type));
  return true;
}

bool EspNvsStorage::save_device(const NvsDeviceConfig &config) {
  std::vector<NvsDeviceConfig> devices;
  load_devices(config.type, devices);

  // Replace existing or append
  auto it = std::find_if(devices.begin(), devices.end(),
                         [&](const NvsDeviceConfig &c) { return c.dst_address == config.dst_address; });
  if (it != devices.end()) {
    *it = config;
  } else {
    devices.push_back(config);
  }

  return save_devices(config.type, devices);
}

bool EspNvsStorage::remove_device(DeviceType type, uint32_t dst_address) {
  std::vector<NvsDeviceConfig> devices;
  load_devices(type, devices);

  auto it = std::remove_if(devices.begin(), devices.end(),
                           [dst_address](const NvsDeviceConfig &c) { return c.dst_address == dst_address; });
  if (it == devices.end()) {
    return false;  // Not found
  }
  devices.erase(it, devices.end());

  return save_devices(type, devices);
}

bool EspNvsStorage::erase_all() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) return false;

  err = nvs_erase_all(handle);
  nvs_commit(handle);
  nvs_close(handle);
  return err == ESP_OK;
}

}  // namespace esphome::elero
