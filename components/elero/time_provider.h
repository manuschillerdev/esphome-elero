/// @file time_provider.h
/// @brief Time abstraction for testability.
///
/// This module provides a simple time abstraction that allows unit tests
/// to control time deterministically without running actual timers.
///
/// Usage in production code:
///   uint32_t now = get_time_provider().millis();
///
/// Usage in tests:
///   MockTimeProvider mock;
///   set_time_provider(&mock);
///   mock.advance(100);  // Simulate 100ms passing

#pragma once

#include <cstdint>

namespace esphome::elero {

/// Abstract interface for time providers.
/// Allows dependency injection of time for testability.
class TimeProvider {
 public:
  virtual ~TimeProvider() = default;

  /// Get current time in milliseconds since boot.
  [[nodiscard]] virtual uint32_t millis() const = 0;
};

/// Production time provider using ESPHome's millis().
/// This is the default implementation used in production builds.
class SystemTimeProvider : public TimeProvider {
 public:
  [[nodiscard]] uint32_t millis() const override;
};

/// Mock time provider for unit tests.
/// Allows deterministic control of time in tests.
class MockTimeProvider : public TimeProvider {
 public:
  /// Current time value (directly settable for tests).
  uint32_t current_time{0};

  [[nodiscard]] uint32_t millis() const override { return current_time; }

  /// Advance time by the specified number of milliseconds.
  void advance(uint32_t ms) { current_time += ms; }

  /// Reset time to zero.
  void reset() { current_time = 0; }
};

/// Get the current time provider.
/// @return Reference to the active time provider (default: SystemTimeProvider)
TimeProvider& get_time_provider();

/// Set a custom time provider (primarily for testing).
/// @param provider Pointer to the new provider, or nullptr to restore default.
/// @note The caller retains ownership of the provider.
void set_time_provider(TimeProvider* provider);

}  // namespace esphome::elero
