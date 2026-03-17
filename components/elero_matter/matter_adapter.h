/// @file matter_adapter.h
/// @brief Matter output adapter — exposes Elero devices as Matter endpoints.
///
/// Maps CoverDevice → Window Covering, LightDevice → Dimmable/On-Off Light.
/// Commands from Matter controllers are queued thread-safely (Matter runs on
/// its own FreeRTOS task) and processed on the ESPHome main loop.

#pragma once

#include "../elero/output_adapter.h"
#include <map>
#include <mutex>
#include <queue>
#include <string>

namespace esphome {
namespace elero {

/// Command queued from Matter task to ESPHome main loop.
struct MatterCommand {
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
    int32_t value;
};

/// Maximum queued commands (defensive — prevents unbounded growth).
inline constexpr size_t MATTER_CMD_QUEUE_MAX = 64;

class MatterAdapter : public OutputAdapter {
 public:
    // ═════════════════════════════════════════════════════════════════════════
    // CONFIGURATION (called by codegen before setup)
    // ═════════════════════════════════════════════════════════════════════════

    /// Informational only — actual commissioning values come from sdkconfig.
    void set_vendor_id(uint16_t id) { vendor_id_ = id; }
    void set_product_id(uint16_t id) { product_id_ = id; }
    void set_device_name(const std::string &name) { device_name_ = name; }
    void set_discriminator(uint16_t disc) { discriminator_ = disc; }
    void set_passcode(uint32_t pass) { passcode_ = pass; }

    /// Pre-computed commissioning payloads (set by codegen from YAML values).
    void set_qr_code(const std::string &qr) { qr_code_ = qr; }
    void set_manual_code(const std::string &code) { manual_code_ = code; }

    // ═════════════════════════════════════════════════════════════════════════
    // OUTPUT ADAPTER INTERFACE
    // ═════════════════════════════════════════════════════════════════════════

    void setup(DeviceRegistry &registry) override;
    void loop() override;

    void on_device_added(const Device &dev) override;
    void on_device_removed(const Device &dev) override;
    void on_state_changed(const Device &dev) override;
    void on_config_changed(const Device &dev) override;

    /// Queue a command from the Matter task (thread-safe, bounded).
    void queue_command(const MatterCommand &cmd);

 private:
    // ── Endpoint lifecycle ──
    void create_cover_endpoint_(const Device &dev);
    void create_light_endpoint_(const Device &dev);
    void destroy_endpoint_(uint32_t address, DeviceType type);

    // ── Attribute updates (ESPHome → Matter) ──
    void update_cover_attributes_(const Device &dev);
    void update_light_attributes_(const Device &dev);

    // ── Command processing (Matter task → ESPHome main loop) ──
    void process_commands_();
    void handle_cover_command_(Device *dev, const MatterCommand &cmd);
    void handle_light_command_(Device *dev, const MatterCommand &cmd);

    // ── Configuration (informational — sdkconfig is authoritative) ──
    uint16_t vendor_id_{0xFFF1};
    uint16_t product_id_{0x8000};
    std::string device_name_{"Elero Gateway"};
    uint16_t discriminator_{3840};
    uint32_t passcode_{20202021};
    std::string qr_code_;       ///< Pre-computed "MT:..." QR payload
    std::string manual_code_;   ///< Pre-computed 11-digit manual pairing code

    // ── Runtime state ──
    DeviceRegistry *registry_{nullptr};
    void *node_{nullptr};  ///< esp_matter::node_t* — void* avoids CHIP headers in .h
    bool started_{false};  ///< True after esp_matter::start() succeeds

    // ── Bidirectional endpoint ↔ device maps ──
    std::map<uint16_t, std::pair<uint32_t, DeviceType>> ep_to_device_;
    std::map<uint64_t, uint16_t> device_to_ep_;

    // ── Thread-safe command queue (Matter task → ESPHome main loop) ──
    std::mutex cmd_mutex_;
    std::queue<MatterCommand> cmd_queue_;

    /// Composite map key: (address << 8) | device_type
    static uint64_t device_key_(uint32_t addr, DeviceType type) {
        return (static_cast<uint64_t>(addr) << 8) | static_cast<uint8_t>(type);
    }
};

}  // namespace elero
}  // namespace esphome
