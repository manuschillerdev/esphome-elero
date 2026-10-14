#pragma once

#include <cstdint>
#include <cstring>

namespace esphome {
namespace elero {

/// Hub-level NVS config — persists user-overridable hub settings (currently only
/// the display name shown in HA discovery and the web UI). Stored under a
/// separate preference key from per-device configs so its layout can evolve
/// independently.

constexpr uint8_t NVS_HUB_CONFIG_VERSION = 1;
constexpr size_t NVS_HUB_NAME_MAX = 32;

struct NvsHubConfig {
  uint8_t version{NVS_HUB_CONFIG_VERSION};
  uint8_t reserved[3]{};
  char name[NVS_HUB_NAME_MAX]{};

  bool is_valid() const { return version == NVS_HUB_CONFIG_VERSION; }

  void set_name(const char *n) {
    if (n == nullptr) {
      name[0] = '\0';
      return;
    }
    strncpy(name, n, NVS_HUB_NAME_MAX - 1);
    name[NVS_HUB_NAME_MAX - 1] = '\0';
  }
};

static_assert(sizeof(NvsHubConfig) == 36, "NvsHubConfig must be 36 bytes for NVS storage");

namespace nvs_pref_key {
inline constexpr const char *HUB = "elero_hub";
}  // namespace nvs_pref_key

}  // namespace elero
}  // namespace esphome
