/// @file test_mqtt_device_manager.cpp
/// @brief Unit tests for MqttDeviceManager with mock dependencies.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <vector>
#include <string>
#include <functional>
#include <map>

// UNIT_TEST is defined via CMake target_compile_definitions

// Mock out ESPHome dependencies
#define ESP_LOGI(tag, ...) (void)0
#define ESP_LOGW(tag, ...) (void)0
#define ESP_LOGD(tag, ...) (void)0
#define ESP_LOGE(tag, ...) (void)0
#define ESP_LOGCONFIG(tag, ...) (void)0

// Stub out millis() for timing
inline uint32_t millis() { return 0; }

// Include the interfaces we're testing
#include "elero/nvs_config.h"
#include "elero/device_manager.h"
#include "elero_mqtt/mqtt_publisher.h"

using namespace esphome::elero;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

// ═══════════════════════════════════════════════════════════════════════════════
// Mock Implementations
// ═══════════════════════════════════════════════════════════════════════════════

/// Mock NVS storage for testing without ESP32 hardware
class MockNvsStorage : public NvsStorage {
 public:
  // Implement NvsStorage interface with GMock
  MOCK_METHOD(NvsResult, load_covers, (std::vector<NvsDeviceConfig>&), (override));
  MOCK_METHOD(NvsResult, load_lights, (std::vector<NvsDeviceConfig>&), (override));
  MOCK_METHOD(NvsResult, save_covers, (const std::vector<NvsDeviceConfig>&), (override));
  MOCK_METHOD(NvsResult, save_lights, (const std::vector<NvsDeviceConfig>&), (override));
  MOCK_METHOD(NvsResult, save_device, (const NvsDeviceConfig&), (override));
  MOCK_METHOD(NvsResult, remove_device, (uint32_t, DeviceType), (override));
  MOCK_METHOD(NvsResult, erase_all, (), (override));

  // Helper to populate covers on load
  std::vector<NvsDeviceConfig> stored_covers_;
  std::vector<NvsDeviceConfig> stored_lights_;

  void setup_default_behavior() {
    ON_CALL(*this, load_covers(_))
        .WillByDefault(Invoke([this](std::vector<NvsDeviceConfig>& out) {
          out = stored_covers_;
          return stored_covers_.empty() ? NvsResult::NOT_FOUND : NvsResult::OK;
        }));
    ON_CALL(*this, load_lights(_))
        .WillByDefault(Invoke([this](std::vector<NvsDeviceConfig>& out) {
          out = stored_lights_;
          return stored_lights_.empty() ? NvsResult::NOT_FOUND : NvsResult::OK;
        }));
    ON_CALL(*this, save_covers(_)).WillByDefault(Return(NvsResult::OK));
    ON_CALL(*this, save_lights(_)).WillByDefault(Return(NvsResult::OK));
    ON_CALL(*this, save_device(_)).WillByDefault(Return(NvsResult::OK));
    ON_CALL(*this, remove_device(_, _)).WillByDefault(Return(NvsResult::OK));
    ON_CALL(*this, erase_all()).WillByDefault(Return(NvsResult::OK));
  }
};

/// Mock MQTT publisher for testing without network
class MockMqttPublisher : public MqttPublisher {
 public:
  MOCK_METHOD(bool, publish, (const std::string&, const std::string&, bool), (override));
  MOCK_METHOD(bool, subscribe, (const std::string&, std::function<void(const std::string&, const std::string&)>), (override));
  MOCK_METHOD(bool, unsubscribe, (const std::string&), (override));
  MOCK_METHOD(bool, is_connected, (), (const, override));

  // Captured subscriptions for simulating incoming messages
  std::map<std::string, std::function<void(const std::string&, const std::string&)>> subscriptions_;

  // Captured publishes for verification
  std::vector<std::tuple<std::string, std::string, bool>> published_;

  void setup_default_behavior() {
    ON_CALL(*this, is_connected()).WillByDefault(Return(true));
    ON_CALL(*this, publish(_, _, _))
        .WillByDefault(Invoke([this](const std::string& topic, const std::string& payload, bool retain) {
          published_.emplace_back(topic, payload, retain);
          return true;
        }));
    ON_CALL(*this, subscribe(_, _))
        .WillByDefault(Invoke([this](const std::string& topic,
                                      std::function<void(const std::string&, const std::string&)> cb) {
          subscriptions_[topic] = std::move(cb);
          return true;
        }));
    ON_CALL(*this, unsubscribe(_)).WillByDefault(Return(true));
  }

  /// Simulate an MQTT message arriving
  void simulate_message(const std::string& topic, const std::string& payload) {
    for (const auto& [pattern, cb] : subscriptions_) {
      if (topic_matches(pattern, topic)) {
        cb(topic, payload);
      }
    }
  }

  /// Check if a message was published
  bool was_published(const std::string& topic_contains) const {
    for (const auto& [topic, payload, retain] : published_) {
      if (topic.find(topic_contains) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  /// Get published payload for topic
  std::string get_published_payload(const std::string& topic_contains) const {
    for (const auto& [topic, payload, retain] : published_) {
      if (topic.find(topic_contains) != std::string::npos) {
        return payload;
      }
    }
    return "";
  }

 private:
  static bool topic_matches(const std::string& pattern, const std::string& topic) {
    // Simple prefix matching for + and # wildcards
    size_t wildcard_pos = pattern.find_first_of("+#");
    if (wildcard_pos != std::string::npos) {
      return topic.substr(0, wildcard_pos) == pattern.substr(0, wildcard_pos);
    }
    return pattern == topic;
  }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixture
// ═══════════════════════════════════════════════════════════════════════════════

class MqttDeviceManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    nvs_ = std::make_unique<MockNvsStorage>();
    mqtt_ = std::make_unique<MockMqttPublisher>();

    nvs_ptr_ = nvs_.get();
    mqtt_ptr_ = mqtt_.get();

    nvs_ptr_->setup_default_behavior();
    mqtt_ptr_->setup_default_behavior();
  }

  std::unique_ptr<MockNvsStorage> nvs_;
  std::unique_ptr<MockMqttPublisher> mqtt_;
  MockNvsStorage* nvs_ptr_;
  MockMqttPublisher* mqtt_ptr_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// NvsDeviceConfig Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MqttDeviceManagerTest, NvsDeviceConfigSize) {
  // Config must be exactly 56 bytes for NVS blob storage
  EXPECT_EQ(sizeof(NvsDeviceConfig), 56);
}

TEST_F(MqttDeviceManagerTest, NvsDeviceConfigDefaultState) {
  NvsDeviceConfig config{};

  EXPECT_EQ(config.version, 1);
  EXPECT_EQ(config.dst_address, 0u);
  EXPECT_EQ(config.src_address, 0u);
  EXPECT_EQ(config.channel, 0);
  EXPECT_FALSE(config.is_valid());
  EXPECT_FALSE(config.is_enabled());
}

TEST_F(MqttDeviceManagerTest, NvsDeviceConfigValidation) {
  NvsDeviceConfig config{};

  // Empty config is invalid
  EXPECT_FALSE(config.is_valid());

  // Need dst, src, and channel
  config.dst_address = 0xA831E5;
  EXPECT_FALSE(config.is_valid());

  config.src_address = 0xF0D008;
  EXPECT_FALSE(config.is_valid());

  config.channel = 4;
  EXPECT_TRUE(config.is_valid());
}

TEST_F(MqttDeviceManagerTest, NvsDeviceConfigEnabledFlag) {
  NvsDeviceConfig config{};

  EXPECT_FALSE(config.is_enabled());

  config.set_enabled(true);
  EXPECT_TRUE(config.is_enabled());

  config.set_enabled(false);
  EXPECT_FALSE(config.is_enabled());
}

TEST_F(MqttDeviceManagerTest, NvsDeviceConfigTypeHelpers) {
  NvsDeviceConfig cover = NvsDeviceConfig::create_cover(0xA831E5, 0xF0D008, 4, "Test Cover");
  EXPECT_TRUE(cover.is_cover());
  EXPECT_FALSE(cover.is_light());
  EXPECT_TRUE(cover.is_enabled());
  EXPECT_STREQ(cover.name, "Test Cover");

  NvsDeviceConfig light = NvsDeviceConfig::create_light(0xC41A2B, 0xF0D008, 6, "Test Light");
  EXPECT_FALSE(light.is_cover());
  EXPECT_TRUE(light.is_light());
  EXPECT_TRUE(light.is_enabled());
  EXPECT_STREQ(light.name, "Test Light");
}

TEST_F(MqttDeviceManagerTest, NvsDeviceConfigNameTruncation) {
  NvsDeviceConfig config{};

  // Long name should be truncated
  config.set_name("This is a very long name that exceeds the limit");
  EXPECT_EQ(strlen(config.name), MAX_DEVICE_NAME_LEN - 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// NVS Storage Mock Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MqttDeviceManagerTest, NvsLoadEmptyReturnsNotFound) {
  std::vector<NvsDeviceConfig> covers;
  NvsResult result = nvs_ptr_->load_covers(covers);

  EXPECT_EQ(result, NvsResult::NOT_FOUND);
  EXPECT_TRUE(covers.empty());
}

TEST_F(MqttDeviceManagerTest, NvsLoadWithData) {
  // Pre-populate storage
  nvs_ptr_->stored_covers_.push_back(
      NvsDeviceConfig::create_cover(0xA831E5, 0xF0D008, 4, "Living Room"));

  std::vector<NvsDeviceConfig> covers;
  NvsResult result = nvs_ptr_->load_covers(covers);

  EXPECT_EQ(result, NvsResult::OK);
  ASSERT_EQ(covers.size(), 1);
  EXPECT_EQ(covers[0].dst_address, 0xA831E5u);
  EXPECT_STREQ(covers[0].name, "Living Room");
}

TEST_F(MqttDeviceManagerTest, NvsSaveDevice) {
  NvsDeviceConfig config = NvsDeviceConfig::create_cover(0xA831E5, 0xF0D008, 4, "Test");

  EXPECT_CALL(*nvs_ptr_, save_device(_)).WillOnce(Return(NvsResult::OK));

  NvsResult result = nvs_ptr_->save_device(config);
  EXPECT_EQ(result, NvsResult::OK);
}

TEST_F(MqttDeviceManagerTest, NvsRemoveDevice) {
  EXPECT_CALL(*nvs_ptr_, remove_device(0xA831E5, DeviceType::COVER))
      .WillOnce(Return(NvsResult::OK));

  NvsResult result = nvs_ptr_->remove_device(0xA831E5, DeviceType::COVER);
  EXPECT_EQ(result, NvsResult::OK);
}

// ═══════════════════════════════════════════════════════════════════════════════
// MQTT Publisher Mock Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MqttDeviceManagerTest, MqttPublishCapture) {
  mqtt_ptr_->publish("elero/a831e5/state", "open", false);

  EXPECT_TRUE(mqtt_ptr_->was_published("a831e5/state"));
  EXPECT_EQ(mqtt_ptr_->get_published_payload("a831e5/state"), "open");
}

TEST_F(MqttDeviceManagerTest, MqttSubscribeAndSimulate) {
  bool received = false;
  std::string received_payload;

  mqtt_ptr_->subscribe("elero/+/cmd", [&](const std::string& topic, const std::string& payload) {
    received = true;
    received_payload = payload;
  });

  mqtt_ptr_->simulate_message("elero/a831e5/cmd", "close");

  EXPECT_TRUE(received);
  EXPECT_EQ(received_payload, "close");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mode and Type Enums
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MqttDeviceManagerTest, EleroModeValues) {
  EXPECT_EQ(static_cast<int>(EleroMode::NATIVE), 0);
  EXPECT_EQ(static_cast<int>(EleroMode::MQTT), 1);
}

TEST_F(MqttDeviceManagerTest, DeviceTypeValues) {
  EXPECT_EQ(static_cast<int>(DeviceType::COVER), 1);
  EXPECT_EQ(static_cast<int>(DeviceType::LIGHT), 2);
}

TEST_F(MqttDeviceManagerTest, NvsResultValues) {
  EXPECT_EQ(static_cast<int>(NvsResult::OK), 0);
  EXPECT_NE(static_cast<int>(NvsResult::NOT_FOUND), 0);
  EXPECT_NE(static_cast<int>(NvsResult::INVALID_DATA), 0);
}
