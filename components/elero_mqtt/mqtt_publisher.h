#pragma once

/// @file mqtt_publisher.h
/// @brief Abstract interface for MQTT publishing operations.
///
/// This interface enables dependency injection and testing with mocks.
/// The production implementation wraps ESPHome's global_mqtt_client.

#include <cstdint>
#include <string>
#include <functional>

namespace esphome::elero {

/// Abstract interface for MQTT publish/subscribe operations.
///
/// This interface decouples the EleroMqttBridge from ESPHome's MQTT client,
/// enabling:
/// - Unit testing with mock implementations
/// - Alternative MQTT backends
/// - Clear API boundaries
class MqttPublisher {
 public:
  virtual ~MqttPublisher() = default;
  MqttPublisher() = default;
  MqttPublisher(const MqttPublisher&) = default;
  MqttPublisher& operator=(const MqttPublisher&) = default;
  MqttPublisher(MqttPublisher&&) = default;
  MqttPublisher& operator=(MqttPublisher&&) = default;

  /// Publish a raw string payload to a topic.
  /// @param topic MQTT topic
  /// @param payload String payload
  /// @param retain Whether to set the retain flag
  /// @return true if publish was successful
  [[nodiscard]] virtual bool publish(const std::string& topic, const std::string& payload,
                                     bool retain = false) = 0;

  /// Subscribe to a topic with a callback.
  /// @param topic MQTT topic (supports wildcards)
  /// @param callback Function called with (topic, payload) on message
  /// @return true if subscription was successful
  [[nodiscard]] virtual bool subscribe(const std::string& topic,
                                       std::function<void(const std::string&, const std::string&)> callback) = 0;

  /// Unsubscribe from a topic.
  /// @param topic MQTT topic
  /// @return true if unsubscription was successful
  [[nodiscard]] virtual bool unsubscribe(const std::string& topic) = 0;

  /// Check if MQTT client is connected.
  [[nodiscard]] virtual bool is_connected() const = 0;
};

}  // namespace esphome::elero
