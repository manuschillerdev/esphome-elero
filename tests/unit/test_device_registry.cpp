/// @file test_device_registry.cpp
/// @brief Unit tests for DeviceRegistry — command dispatch, RF routing, CRUD, loop logic.
///
/// Uses unity-build pattern: stubs are defined first, then production .cpp files are
/// #included directly so everything compiles in one translation unit.
///
/// ## What is NOT testable on host (requires ESP32 + CC1101 hardware):
///
/// - RF task loop (rf_task_func_) — FreeRTOS task on Core 0, real SPI
/// - ISR → atomic flag → task notification wakeup timing
/// - Cross-core queue backpressure (rx_queue full, tx_done_queue ordering)
/// - drain_fifo_() — real CC1101 FIFO burst reads, multi-packet parsing from wire
/// - handle_tx_state_() — MARCSTATE polling, GDO0 interrupt detection, TX timeout
/// - Radio health watchdog — detecting stuck MARCSTATE without ISR
/// - SPI bus reliability and transaction isolation
///
/// These paths are validated through hardware testing and RF observability sensors.

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cstring>
#include <array>
#include <queue>

// ═══════════════════════════════════════════════════════════════════════════════
// ESPHome stubs — must come before any production includes
// ═══════════════════════════════════════════════════════════════════════════════

#define ESP_LOGV(tag, format, ...) ((void)0)
#define ESP_LOGVV(tag, format, ...) ((void)0)
#define ESP_LOGD(tag, format, ...) ((void)0)
#define ESP_LOGI(tag, format, ...) ((void)0)
#define ESP_LOGW(tag, format, ...) ((void)0)
#define ESP_LOGE(tag, format, ...) ((void)0)
#define LOG_PIN(msg, pin) ((void)0)
#define ESP_LOGCONFIG(tag, format, ...) ((void)0)
#define IRAM_ATTR

#ifndef UNIT_TEST
#define UNIT_TEST
#endif

#include "elero/time_provider.h"

// Provide implementations for stubs declared in header files
namespace esphome {

uint32_t millis() { return esphome::elero::get_time_provider().millis(); }
uint32_t fnv1_hash(const std::string &str) {
    uint32_t hash = 2166136261u;
    for (char c : str) { hash = (hash * 16777619u) ^ static_cast<uint8_t>(c); }
    return hash;
}

class InternalGPIOPin {
 public:
  void setup() {}
  template<typename... Args> void attach_interrupt(Args &&...) {}
};

namespace gpio {
enum InterruptType { INTERRUPT_FALLING_EDGE };
}  // namespace gpio

namespace setup_priority {
constexpr float DATA = 0.0f;
}

}  // namespace esphome

// ═══════════════════════════════════════════════════════════════════════════════
// Unity build — real production code compiled here
// ═══════════════════════════════════════════════════════════════════════════════

#include "elero/cover_sm.cpp"
#include "elero/light_sm.cpp"
#include "elero/device_registry.cpp"

// Stub Elero methods (device_registry.cpp includes elero.h)
namespace esphome {
namespace elero {

// Auto-complete TX — registry tests verify dispatch logic, not TX pipeline
bool Elero::request_tx(TxClient *client, const EleroCommand &) {
    client->on_tx_complete(true);
    return true;
}

void Elero::setup() {}
void Elero::loop() {}
void Elero::dump_config() {}
void Elero::dispatch_packet(const RfPacketInfo &) {}
bool Elero::send_raw_command(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) { return false; }
void Elero::reinit_frequency(uint8_t, uint8_t, uint8_t) {}

}  // namespace elero

ESPPreferences prefs_instance_;
ESPPreferences *global_preferences = &prefs_instance_;

}  // namespace esphome

using namespace esphome::elero;
namespace pkt = esphome::elero::packet;

// ═══════════════════════════════════════════════════════════════════════════════
// Mock OutputAdapter — records notifications for assertion
// ═══════════════════════════════════════════════════════════════════════════════

struct MockAdapter : public OutputAdapter {
    void setup(DeviceRegistry &) override {}
    void loop() override {}

    void on_device_added(const Device &dev) override {
        added.push_back(dev.config.dst_address);
    }
    void on_device_removed(const Device &dev) override {
        removed.push_back(dev.config.dst_address);
    }
    void on_state_changed(const Device &dev) override {
        state_changed.push_back(dev.config.dst_address);
    }
    void on_config_changed(const Device &dev) override {
        config_changed.push_back(dev.config.dst_address);
    }
    void on_rf_packet(const RfPacketInfo &) override {
        rf_packets++;
    }

    std::vector<uint32_t> added;
    std::vector<uint32_t> removed;
    std::vector<uint32_t> state_changed;
    std::vector<uint32_t> config_changed;
    int rf_packets{0};

    void clear() {
        added.clear(); removed.clear(); state_changed.clear();
        config_changed.clear(); rf_packets = 0;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════════

static NvsDeviceConfig make_cover_config(uint32_t addr, const char *name = "Test Cover") {
    NvsDeviceConfig cfg{};
    cfg.type = DeviceType::COVER;
    cfg.dst_address = addr;
    cfg.src_address = 0xF0D008;
    cfg.channel = 4;
    cfg.open_duration_ms = 10000;
    cfg.close_duration_ms = 10000;
    cfg.poll_interval_ms = 300000;
    strncpy(cfg.name, name, NVS_NAME_MAX - 1);
    return cfg;
}

static NvsDeviceConfig make_light_config(uint32_t addr, const char *name = "Test Light") {
    NvsDeviceConfig cfg{};
    cfg.type = DeviceType::LIGHT;
    cfg.dst_address = addr;
    cfg.src_address = 0xF0D008;
    cfg.channel = 6;
    cfg.dim_duration_ms = 5000;
    strncpy(cfg.name, name, NVS_NAME_MAX - 1);
    return cfg;
}

static RfPacketInfo make_status_pkt(uint32_t src, uint8_t state_byte, float rssi = -50.0f) {
    RfPacketInfo pkt{};
    pkt.timestamp_ms = esphome::millis();
    pkt.src = src;
    pkt.dst = 0xF0D008;
    pkt.type = pkt::msg_type::STATUS;
    pkt.state = state_byte;
    pkt.rssi = rssi;
    pkt.echo = false;
    return pkt;
}

static RfPacketInfo make_command_pkt(uint32_t src, uint32_t dst, uint8_t cmd,
                                      bool echo = false) {
    RfPacketInfo pkt{};
    pkt.timestamp_ms = esphome::millis();
    pkt.src = src;
    pkt.dst = dst;
    pkt.type = pkt::msg_type::COMMAND;
    pkt.command = cmd;
    pkt.echo = echo;
    pkt.channel = 4;
    pkt.rssi = -55.0f;
    return pkt;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Fixture
// ═══════════════════════════════════════════════════════════════════════════════

class DeviceRegistryTest : public ::testing::Test {
 protected:
    MockTimeProvider mock_time_;
    MockAdapter adapter_;
    DeviceRegistry registry_;
    Elero hub_;

    void SetUp() override {
        set_time_provider(&mock_time_);
        mock_time_.reset();
        registry_.set_hub(&hub_);
        registry_.add_adapter(&adapter_);
    }

    void TearDown() override {
        set_time_provider(nullptr);
    }

    Device *add_cover(uint32_t addr = 0xA831E5) {
        return registry_.register_device(make_cover_config(addr));
    }

    Device *add_light(uint32_t addr = 0xC41A2B) {
        return registry_.register_device(make_light_config(addr));
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// CRUD — only non-trivial paths
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(DeviceRegistryTest, RegisterDuplicate_UpdatesConfigInPlace) {
    auto *dev1 = add_cover(0xA831E5);
    auto cfg2 = make_cover_config(0xA831E5, "Updated Cover");
    auto *dev2 = registry_.register_device(cfg2);
    EXPECT_EQ(dev1, dev2);  // Same slot, not a new allocation
    EXPECT_STREQ(dev2->config.name, "Updated Cover");
    EXPECT_EQ(registry_.count_active(), 1u);
    EXPECT_EQ(adapter_.config_changed.size(), 1u);
}

TEST_F(DeviceRegistryTest, Remove_NotifiesBeforeDeactivation) {
    add_cover(0xA831E5);
    adapter_.clear();

    EXPECT_TRUE(registry_.remove(0xA831E5, DeviceType::COVER));
    EXPECT_EQ(registry_.count_active(), 0u);
    // If notify_removed_ wasn't called, MQTT adapter would leak discovery topics
    EXPECT_EQ(adapter_.removed.size(), 1u);
}

TEST_F(DeviceRegistryTest, SlotExhaustion_ReturnsNull) {
    for (uint32_t i = 0; i < DeviceRegistry::MAX_DEVICES; ++i) {
        ASSERT_NE(registry_.register_device(make_cover_config(0x100000 + i)), nullptr);
    }
    EXPECT_EQ(registry_.register_device(make_cover_config(0xFFFFFF)), nullptr);
}

TEST_F(DeviceRegistryTest, RegisterDevice_RejectsWhenNvsEnabled) {
    registry_.set_nvs_enabled(true);
    EXPECT_EQ(registry_.register_device(make_cover_config(0xA831E5)), nullptr);
    EXPECT_EQ(registry_.count_active(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// COMMAND DISPATCH — Cover
// These test the centralized command entry points that ALL adapters call.
// If these are wrong, every mode (native, MQTT, WebSocket) is broken.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(DeviceRegistryTest, CoverUp_EnqueuesCommandPlusCheck) {
    auto *dev = add_cover();
    adapter_.clear();

    registry_.command_cover(*dev, pkt::command::UP);

    auto &cover = std::get<CoverDevice>(dev->logic);
    EXPECT_TRUE(std::holds_alternative<cover_sm::Opening>(cover.state));
    // UP + follow-up CHECK — if CHECK is missing, we never learn the blind is moving
    EXPECT_EQ(dev->sender.queue_size(), 2u);
    EXPECT_EQ(adapter_.state_changed.size(), 1u);
}

TEST_F(DeviceRegistryTest, CoverStop_ClearsQueueFirst) {
    auto *dev = add_cover();
    registry_.command_cover(*dev, pkt::command::UP);  // Queue: UP + CHECK

    registry_.command_cover(*dev, pkt::command::STOP);

    // STOP must clear the queue BEFORE enqueuing — otherwise UP packets leak through
    auto &cover = std::get<CoverDevice>(dev->logic);
    EXPECT_FALSE(cover_sm::is_moving(cover.state));
    EXPECT_EQ(dev->sender.queue_size(), 2u);  // Only STOP + CHECK remain
    EXPECT_EQ(cover.target_position, cover_sm::NO_TARGET);
}

TEST_F(DeviceRegistryTest, CoverDown_TracksLastDirection) {
    auto *dev = add_cover();
    registry_.command_cover(*dev, pkt::command::DOWN);

    auto &cover = std::get<CoverDevice>(dev->logic);
    // last_direction drives toggle logic in adapters
    EXPECT_EQ(cover.last_direction, cover_sm::Operation::CLOSING);
}

// ═══════════════════════════════════════════════════════════════════════════════
// COMMAND DISPATCH — Set Position
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(DeviceRegistryTest, SetPosition_IntermediateTarget) {
    auto *dev = add_cover();
    registry_.set_cover_position(*dev, 0.5f);

    auto &cover = std::get<CoverDevice>(dev->logic);
    EXPECT_FLOAT_EQ(cover.target_position, 0.5f);
    EXPECT_TRUE(std::holds_alternative<cover_sm::Opening>(cover.state));
}

TEST_F(DeviceRegistryTest, SetPosition_EndpointHasNoTarget) {
    // Full open/close: blind handles the endpoint itself, no intermediate stop needed
    auto *dev = add_cover();
    registry_.set_cover_position(*dev, 1.0f);

    auto &cover = std::get<CoverDevice>(dev->logic);
    EXPECT_EQ(cover.target_position, cover_sm::NO_TARGET);
}

TEST_F(DeviceRegistryTest, SetPosition_NoDurations_Rejected) {
    auto cfg = make_cover_config(0xA831E5);
    cfg.open_duration_ms = 0;
    cfg.close_duration_ms = 0;
    auto *dev = registry_.register_device(cfg);
    adapter_.clear();

    registry_.set_cover_position(*dev, 0.5f);
    // Without durations, position tracking is impossible — must be a no-op
    EXPECT_EQ(adapter_.state_changed.size(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// COMMAND DISPATCH — Light
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(DeviceRegistryTest, LightOff_ClearsQueueBeforeEnqueue) {
    auto *dev = add_light();
    registry_.command_light(*dev, pkt::command::UP);  // On, queue has UP

    registry_.command_light(*dev, pkt::command::DOWN);

    auto &light = std::get<LightDevice>(dev->logic);
    EXPECT_FALSE(light_sm::is_on(light.state));
    // Same principle as cover STOP — clear first to prevent stale UP leaking
    EXPECT_EQ(dev->sender.queue_size(), 1u);
}

TEST_F(DeviceRegistryTest, SetBrightness_ZeroTurnsOff) {
    auto *dev = add_light();
    registry_.command_light(*dev, pkt::command::UP);

    registry_.set_light_brightness(*dev, 0.0f);
    EXPECT_FALSE(light_sm::is_on(std::get<LightDevice>(dev->logic).state));
}

TEST_F(DeviceRegistryTest, SetBrightness_PartialStartsDimming) {
    auto *dev = add_light();
    registry_.command_light(*dev, pkt::command::UP);  // On at 1.0

    registry_.set_light_brightness(*dev, 0.5f);

    auto &light = std::get<LightDevice>(dev->logic);
    EXPECT_TRUE(std::holds_alternative<light_sm::DimmingDown>(light.state));
}

TEST_F(DeviceRegistryTest, SetBrightness_NoDimSupport_InstantOn) {
    auto cfg = make_light_config(0xC41A2B);
    cfg.dim_duration_ms = 0;
    auto *dev = registry_.register_device(cfg);

    registry_.set_light_brightness(*dev, 0.5f);

    auto &light = std::get<LightDevice>(dev->logic);
    // Non-dimmable light: any nonzero brightness = full on, no dimming state
    EXPECT_TRUE(light_sm::is_on(light.state));
    EXPECT_FALSE(light_sm::is_dimming(light.state));
}

// ═══════════════════════════════════════════════════════════════════════════════
// RF DISPATCH — the primary RX path from Core 0 → registry
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(DeviceRegistryTest, RfStatus_UpdatesMetadataAndTransitionsFsm) {
    auto *dev = add_cover();
    adapter_.clear();
    mock_time_.advance(1000);

    auto rf = make_status_pkt(0xA831E5, pkt::state::MOVING_UP, -45.0f);
    registry_.on_rf_packet(rf, mock_time_.millis());

    // rf_meta updated (RSSI, state_raw, last_seen)
    EXPECT_FLOAT_EQ(dev->rf.last_rssi, -45.0f);
    EXPECT_EQ(dev->rf.last_state_raw, pkt::state::MOVING_UP);
    // FSM transitioned
    EXPECT_TRUE(cover_sm::is_moving(std::get<CoverDevice>(dev->logic).state));
}

TEST_F(DeviceRegistryTest, RfStatus_TiltSetAndClearedByMovement) {
    auto *dev = add_cover();

    registry_.on_rf_packet(make_status_pkt(0xA831E5, pkt::state::TILT), mock_time_.millis());
    EXPECT_TRUE(std::get<CoverDevice>(dev->logic).tilted);

    // Movement clears tilt — this is stateful across packets and easy to break
    registry_.on_rf_packet(make_status_pkt(0xA831E5, pkt::state::MOVING_UP), mock_time_.millis());
    EXPECT_FALSE(std::get<CoverDevice>(dev->logic).tilted);
}

TEST_F(DeviceRegistryTest, RfStatus_NotifiesEvenWhenFsmUnchanged) {
    // Design decision: every status packet notifies adapters (for RSSI freshness).
    // If accidentally changed to "only on FSM change", RSSI stops updating.
    auto *dev = add_cover();
    registry_.on_rf_packet(make_status_pkt(0xA831E5, pkt::state::TOP), mock_time_.millis());
    adapter_.clear();

    registry_.on_rf_packet(make_status_pkt(0xA831E5, pkt::state::TOP, -40.0f), mock_time_.millis());

    EXPECT_GE(adapter_.state_changed.size(), 1u);
    EXPECT_FLOAT_EQ(dev->rf.last_rssi, -40.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// RF DISPATCH — Echo filtering and remote auto-discovery
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(DeviceRegistryTest, RfCommand_EchoFiltered) {
    add_cover(0xA831E5);
    registry_.set_nvs_enabled(true);
    adapter_.clear();

    // Echo = our own TX bounced back. Must not trigger remote discovery.
    auto rf = make_command_pkt(0xF0D008, 0xA831E5, pkt::command::UP, /*echo=*/true);
    registry_.on_rf_packet(rf, mock_time_.millis());
    EXPECT_EQ(adapter_.added.size(), 0u);
}

TEST_F(DeviceRegistryTest, RfCommand_DiscoversRemoteEphemerally) {
    add_cover(0xA831E5);
    registry_.set_nvs_enabled(true);
    adapter_.clear();

    auto rf = make_command_pkt(0xBBBBBB, 0xA831E5, pkt::command::UP, /*echo=*/false);
    registry_.on_rf_packet(rf, mock_time_.millis());

    ASSERT_EQ(adapter_.added.size(), 1u);
    auto *remote = registry_.find(0xBBBBBB, DeviceType::REMOTE);
    ASSERT_NE(remote, nullptr);
    // Ephemeral until user saves — updated_at stays 0 so MQTT adapter skips it
    EXPECT_EQ(remote->config.updated_at, 0u);
}

TEST_F(DeviceRegistryTest, RfCommand_NativeMode_NoDiscovery) {
    // NVS disabled = native mode = devices come from YAML only
    add_cover(0xA831E5);
    adapter_.clear();

    registry_.on_rf_packet(make_command_pkt(0xBBBBBB, 0xA831E5, pkt::command::UP, false),
                           mock_time_.millis());
    EXPECT_EQ(registry_.find(0xBBBBBB, DeviceType::REMOTE), nullptr);
}

TEST_F(DeviceRegistryTest, RfCommand_UpdatesExistingRemote) {
    add_cover(0xA831E5);
    registry_.set_nvs_enabled(true);

    // First packet discovers remote
    registry_.on_rf_packet(make_command_pkt(0xBBBBBB, 0xA831E5, pkt::command::UP, false),
                           mock_time_.millis());
    adapter_.clear();

    // Second packet updates it — must NOT create a duplicate (would fill 48 slots)
    registry_.on_rf_packet(make_command_pkt(0xBBBBBB, 0xA831E5, pkt::command::DOWN, false),
                           mock_time_.millis());

    EXPECT_EQ(adapter_.added.size(), 0u);
    auto &rd = std::get<RemoteDevice>(registry_.find(0xBBBBBB, DeviceType::REMOTE)->logic);
    EXPECT_EQ(rd.last_command, pkt::command::DOWN);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LOOP — integration tests for the main loop logic
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(DeviceRegistryTest, Loop_CoverAutoStopsAtTargetPosition) {
    auto *dev = add_cover();
    registry_.set_cover_position(*dev, 0.5f);

    auto &cover = std::get<CoverDevice>(dev->logic);
    ASSERT_FLOAT_EQ(cover.target_position, 0.5f);

    // 5000ms = 50% of 10000ms open_duration → position reaches target
    mock_time_.advance(5000);
    registry_.loop(mock_time_.millis());

    // Auto-stop fires: clears target, transitions to Stopping
    EXPECT_FALSE(cover_sm::is_moving(cover.state));
    EXPECT_EQ(cover.target_position, cover_sm::NO_TARGET);
}

TEST_F(DeviceRegistryTest, Loop_CoverMovementTimeout) {
    auto *dev = add_cover();
    registry_.command_cover(*dev, pkt::command::UP);

    // 120001ms > TIMEOUT_MOVEMENT (120000ms) — blind must stop
    mock_time_.advance(120001);
    registry_.loop(mock_time_.millis());

    EXPECT_FALSE(cover_sm::is_moving(std::get<CoverDevice>(dev->logic).state));
}

TEST_F(DeviceRegistryTest, Loop_LightDimComplete_EnqueuesRelease) {
    auto *dev = add_light();
    registry_.command_light(*dev, pkt::command::UP);

    // Start dimming down
    auto &light = std::get<LightDevice>(dev->logic);
    auto ctx = light_context(dev->config);
    light.state = light_sm::on_set_brightness(light.state, 0.5f, mock_time_.millis(), ctx);
    ASSERT_TRUE(light_sm::is_dimming(light.state));
    dev->sender.clear_queue();

    // Advance past dim completion
    mock_time_.advance(dev->config.dim_duration_ms + 100);
    registry_.loop(mock_time_.millis());

    // RELEASE must be enqueued — without it, the physical light keeps dimming
    EXPECT_GE(dev->sender.queue_size(), 1u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// POLL STAGGER — prevents RF collision when multiple blinds poll simultaneously
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(DeviceRegistryTest, PollStagger_AssignsIncreasingOffsets) {
    auto *dev1 = add_cover(0x111111);
    auto *dev2 = add_cover(0x222222);

    auto &c1 = std::get<CoverDevice>(dev1->logic);
    auto &c2 = std::get<CoverDevice>(dev2->logic);

    EXPECT_EQ(c1.poll.offset_ms, 0u);
    EXPECT_EQ(c2.poll.offset_ms, pkt::timing::POLL_OFFSET_SPACING);
}
