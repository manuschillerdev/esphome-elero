#include "nvs_config.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include "nvs_flash.h"
#include "nvs.h"
#endif

namespace esphome::elero {

static constexpr const char* TAG = "elero.nvs";

// NVS keys
static constexpr const char* KEY_COVERS = "covers";
static constexpr const char* KEY_LIGHTS = "lights";
static constexpr const char* KEY_NUM_COVERS = "num_cvr";
static constexpr const char* KEY_NUM_LIGHTS = "num_lgt";

const char* nvs_result_to_string(NvsResult result) {
  switch (result) {
    case NvsResult::OK:
      return "OK";
    case NvsResult::NOT_FOUND:
      return "NOT_FOUND";
    case NvsResult::INVALID_DATA:
      return "INVALID_DATA";
    case NvsResult::STORAGE_FULL:
      return "STORAGE_FULL";
    case NvsResult::WRITE_ERROR:
      return "WRITE_ERROR";
    case NvsResult::READ_ERROR:
      return "READ_ERROR";
    case NvsResult::NOT_INITIALIZED:
      return "NOT_INITIALIZED";
    default:
      return "UNKNOWN";
  }
}

#ifdef USE_ESP32

bool EspNvsStorage::init() {
  if (initialized_) {
    return true;
  }

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
    err = nvs_flash_erase();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(err));
      return false;
    }
    err = nvs_flash_init();
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
    return false;
  }

  initialized_ = true;
  ESP_LOGI(TAG, "NVS initialized");
  return true;
}

NvsResult EspNvsStorage::load_devices(const char* key, const char* count_key,
                                      std::vector<NvsDeviceConfig>& out) {
  if (!initialized_) {
    return NvsResult::NOT_INITIALIZED;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Namespace doesn't exist yet - not an error, just no data
    ESP_LOGD(TAG, "No NVS namespace '%s' yet", NVS_NAMESPACE);
    return NvsResult::NOT_FOUND;
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return NvsResult::READ_ERROR;
  }

  // Read count
  uint8_t count = 0;
  err = nvs_get_u8(handle, count_key, &count);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    ESP_LOGD(TAG, "No '%s' key in NVS", count_key);
    return NvsResult::NOT_FOUND;
  }
  if (err != ESP_OK) {
    nvs_close(handle);
    ESP_LOGE(TAG, "Failed to read count: %s", esp_err_to_name(err));
    return NvsResult::READ_ERROR;
  }

  if (count == 0) {
    nvs_close(handle);
    return NvsResult::OK;
  }

  // Read blob
  size_t required_size = count * sizeof(NvsDeviceConfig);
  size_t blob_size = required_size;

  // First query the size
  err = nvs_get_blob(handle, key, nullptr, &blob_size);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    ESP_LOGW(TAG, "Count key exists but blob '%s' missing", key);
    return NvsResult::INVALID_DATA;
  }
  if (err != ESP_OK) {
    nvs_close(handle);
    ESP_LOGE(TAG, "Failed to query blob size: %s", esp_err_to_name(err));
    return NvsResult::READ_ERROR;
  }

  if (blob_size != required_size) {
    nvs_close(handle);
    ESP_LOGW(TAG, "Blob size mismatch: expected %u, got %u", required_size, blob_size);
    return NvsResult::INVALID_DATA;
  }

  // Allocate and read
  out.resize(count);
  err = nvs_get_blob(handle, key, out.data(), &blob_size);
  nvs_close(handle);

  if (err != ESP_OK) {
    out.clear();
    ESP_LOGE(TAG, "Failed to read blob: %s", esp_err_to_name(err));
    return NvsResult::READ_ERROR;
  }

  // Validate version and filter invalid entries
  size_t valid_count = 0;
  for (size_t i = 0; i < out.size(); ++i) {
    if (out[i].version == 1 && out[i].is_valid()) {
      if (valid_count != i) {
        out[valid_count] = out[i];
      }
      ++valid_count;
    } else {
      ESP_LOGW(TAG, "Skipping invalid config at index %u (version=%u, valid=%d)",
               i, out[i].version, out[i].is_valid());
    }
  }
  out.resize(valid_count);

  ESP_LOGI(TAG, "Loaded %u devices from '%s'", valid_count, key);
  return NvsResult::OK;
}

NvsResult EspNvsStorage::save_devices(const char* key, const char* count_key,
                                      const std::vector<NvsDeviceConfig>& configs,
                                      uint8_t max_count) {
  if (!initialized_) {
    return NvsResult::NOT_INITIALIZED;
  }

  if (configs.size() > max_count) {
    ESP_LOGE(TAG, "Too many devices: %u > %u", configs.size(), max_count);
    return NvsResult::STORAGE_FULL;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
    return NvsResult::WRITE_ERROR;
  }

  // Write count
  uint8_t count = static_cast<uint8_t>(configs.size());
  err = nvs_set_u8(handle, count_key, count);
  if (err != ESP_OK) {
    nvs_close(handle);
    ESP_LOGE(TAG, "Failed to write count: %s", esp_err_to_name(err));
    return NvsResult::WRITE_ERROR;
  }

  // Write blob (if any devices)
  if (count > 0) {
    err = nvs_set_blob(handle, key, configs.data(), count * sizeof(NvsDeviceConfig));
    if (err != ESP_OK) {
      nvs_close(handle);
      if (err == ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
        ESP_LOGE(TAG, "NVS full");
        return NvsResult::STORAGE_FULL;
      }
      ESP_LOGE(TAG, "Failed to write blob: %s", esp_err_to_name(err));
      return NvsResult::WRITE_ERROR;
    }
  } else {
    // Erase blob if no devices
    err = nvs_erase_key(handle, key);
    // Ignore NOT_FOUND error (key might not exist)
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
      nvs_close(handle);
      ESP_LOGE(TAG, "Failed to erase blob: %s", esp_err_to_name(err));
      return NvsResult::WRITE_ERROR;
    }
  }

  // Commit
  err = nvs_commit(handle);
  nvs_close(handle);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(err));
    return NvsResult::WRITE_ERROR;
  }

  ESP_LOGI(TAG, "Saved %u devices to '%s'", count, key);
  return NvsResult::OK;
}

NvsResult EspNvsStorage::load_covers(std::vector<NvsDeviceConfig>& out) {
  return load_devices(KEY_COVERS, KEY_NUM_COVERS, out);
}

NvsResult EspNvsStorage::load_lights(std::vector<NvsDeviceConfig>& out) {
  return load_devices(KEY_LIGHTS, KEY_NUM_LIGHTS, out);
}

NvsResult EspNvsStorage::save_covers(const std::vector<NvsDeviceConfig>& configs) {
  return save_devices(KEY_COVERS, KEY_NUM_COVERS, configs, MAX_DYNAMIC_COVERS);
}

NvsResult EspNvsStorage::save_lights(const std::vector<NvsDeviceConfig>& configs) {
  return save_devices(KEY_LIGHTS, KEY_NUM_LIGHTS, configs, MAX_DYNAMIC_LIGHTS);
}

NvsResult EspNvsStorage::save_device(const NvsDeviceConfig& config) {
  if (!config.is_valid()) {
    ESP_LOGW(TAG, "Refusing to save invalid config");
    return NvsResult::INVALID_DATA;
  }

  // Load existing devices of this type
  std::vector<NvsDeviceConfig> existing;
  const char* key = config.is_cover() ? KEY_COVERS : KEY_LIGHTS;
  const char* count_key = config.is_cover() ? KEY_NUM_COVERS : KEY_NUM_LIGHTS;
  uint8_t max_count = config.is_cover() ? MAX_DYNAMIC_COVERS : MAX_DYNAMIC_LIGHTS;

  NvsResult result = load_devices(key, count_key, existing);
  if (result != NvsResult::OK && result != NvsResult::NOT_FOUND) {
    return result;
  }

  // Find existing entry or add new
  bool found = false;
  for (auto& entry : existing) {
    if (entry.dst_address == config.dst_address) {
      entry = config;
      found = true;
      ESP_LOGD(TAG, "Updated device 0x%06x", config.dst_address);
      break;
    }
  }

  if (!found) {
    if (existing.size() >= max_count) {
      ESP_LOGE(TAG, "Cannot add device: limit reached (%u)", max_count);
      return NvsResult::STORAGE_FULL;
    }
    existing.push_back(config);
    ESP_LOGD(TAG, "Added new device 0x%06x", config.dst_address);
  }

  return save_devices(key, count_key, existing, max_count);
}

NvsResult EspNvsStorage::remove_device(uint32_t address, DeviceType type) {
  // Load existing devices of this type
  std::vector<NvsDeviceConfig> existing;
  const char* key = (type == DeviceType::COVER) ? KEY_COVERS : KEY_LIGHTS;
  const char* count_key = (type == DeviceType::COVER) ? KEY_NUM_COVERS : KEY_NUM_LIGHTS;
  uint8_t max_count = (type == DeviceType::COVER) ? MAX_DYNAMIC_COVERS : MAX_DYNAMIC_LIGHTS;

  NvsResult result = load_devices(key, count_key, existing);
  if (result == NvsResult::NOT_FOUND) {
    // Nothing to remove
    return NvsResult::NOT_FOUND;
  }
  if (result != NvsResult::OK) {
    return result;
  }

  // Find and remove
  auto it = std::remove_if(existing.begin(), existing.end(),
                           [address](const NvsDeviceConfig& cfg) {
                             return cfg.dst_address == address;
                           });

  if (it == existing.end()) {
    ESP_LOGD(TAG, "Device 0x%06x not found", address);
    return NvsResult::NOT_FOUND;
  }

  existing.erase(it, existing.end());
  ESP_LOGD(TAG, "Removed device 0x%06x", address);

  return save_devices(key, count_key, existing, max_count);
}

NvsResult EspNvsStorage::erase_all() {
  if (!initialized_) {
    return NvsResult::NOT_INITIALIZED;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Nothing to erase
    return NvsResult::OK;
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS for erase: %s", esp_err_to_name(err));
    return NvsResult::WRITE_ERROR;
  }

  err = nvs_erase_all(handle);
  if (err != ESP_OK) {
    nvs_close(handle);
    ESP_LOGE(TAG, "Failed to erase: %s", esp_err_to_name(err));
    return NvsResult::WRITE_ERROR;
  }

  err = nvs_commit(handle);
  nvs_close(handle);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit erase: %s", esp_err_to_name(err));
    return NvsResult::WRITE_ERROR;
  }

  ESP_LOGI(TAG, "Erased all NVS data");
  return NvsResult::OK;
}

#else  // !USE_ESP32

// Stub implementation for non-ESP32 platforms (compilation only)
bool EspNvsStorage::init() { return false; }
NvsResult EspNvsStorage::load_covers(std::vector<NvsDeviceConfig>& /*out*/) { return NvsResult::NOT_INITIALIZED; }
NvsResult EspNvsStorage::load_lights(std::vector<NvsDeviceConfig>& /*out*/) { return NvsResult::NOT_INITIALIZED; }
NvsResult EspNvsStorage::save_covers(const std::vector<NvsDeviceConfig>& /*configs*/) { return NvsResult::NOT_INITIALIZED; }
NvsResult EspNvsStorage::save_lights(const std::vector<NvsDeviceConfig>& /*configs*/) { return NvsResult::NOT_INITIALIZED; }
NvsResult EspNvsStorage::save_device(const NvsDeviceConfig& /*config*/) { return NvsResult::NOT_INITIALIZED; }
NvsResult EspNvsStorage::remove_device(uint32_t /*address*/, DeviceType /*type*/) { return NvsResult::NOT_INITIALIZED; }
NvsResult EspNvsStorage::erase_all() { return NvsResult::NOT_INITIALIZED; }
NvsResult EspNvsStorage::load_devices(const char* /*key*/, const char* /*count_key*/, std::vector<NvsDeviceConfig>& /*out*/) { return NvsResult::NOT_INITIALIZED; }
NvsResult EspNvsStorage::save_devices(const char* /*key*/, const char* /*count_key*/, const std::vector<NvsDeviceConfig>& /*configs*/, uint8_t /*max_count*/) { return NvsResult::NOT_INITIALIZED; }

#endif  // USE_ESP32

}  // namespace esphome::elero
