#pragma once

#include <cstdint>
#include <string>

namespace esphome::elero {

enum class OperationStatus : uint8_t { APPLIED, QUEUED, REJECTED, FAILED };

// APPLIED means a configuration change has been synced to flash. QUEUED means
// accepted for transmission, never an acknowledgement from a motor.
struct OperationResult {
  OperationStatus status;
  std::string message;
  uint32_t operation_id{0};

  [[nodiscard]] bool ok() const { return status == OperationStatus::APPLIED || status == OperationStatus::QUEUED; }
};

enum class TransmissionStatus : uint8_t { QUEUED, TRANSMITTED, FAILED, CANCELLED };

// IDs are hub-local and valid until reboot. Callbacks run on the main loop;
// observers must copy any information retained after the callback returns.
struct ChannelCommandResult {
  uint32_t operation_id{0};
  uint32_t remote{0};
  uint8_t channel{0};
  uint8_t command{0};
  TransmissionStatus status{TransmissionStatus::QUEUED};
};

}  // namespace esphome::elero
