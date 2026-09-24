#pragma once
#include <cstdint>

namespace esphome::elero_paper {
enum class EditField : uint8_t {
  DEVICE_NAME = 0,
  DEVICE_ADDRESS = 1,
  DEVICE_TYPE = 2,
  DEVICE_ENABLED = 3,
  DEVICE_REMOTE = 4,
  DEVICE_CHANNEL = 5,
  OPEN_DURATION = 6,
  CLOSE_DURATION = 7,
  DEVICE_TILT = 8,
  DIM_DURATION = 9,
  GROUP_NAME = 20,
  HUB_NAME = 21,
  SEARCH = 22,
  REMOTE_FILTER = 23,
  LEARN_SOURCE = 30,
  LEARN_CHANNEL = 31,
  LEARN_TIMEOUT = 32,
  RAW_SOURCE = 40,
  RAW_DESTINATION = 41,
  RAW_CHANNEL = 42,
  RAW_COMMAND = 43,
  RAW_TYPE = 44,
  RAW_TYPE2 = 45,
  RAW_HOP = 46,
  RAW_PAYLOAD1 = 47,
  RAW_PAYLOAD2 = 48,
  PACKET_ADDRESS = 50,
  PACKET_CHANNEL = 51,
  PACKET_TYPE = 52,
  PACKET_COMMAND = 53,
  PACKET_STATE = 54,
  REMOTE_CHANNEL = 60,
};

enum class FieldFormat : uint8_t { TEXT, DECIMAL, HEXADECIMAL };
struct FieldSpec {
  FieldFormat format;
  uint32_t minimum;
  uint32_t maximum;
  bool optional{false};
};

// Validation policy is independent of screen layout and editor dispatch.
constexpr FieldSpec field_spec(EditField field) {
  switch (field) {
    case EditField::DEVICE_ADDRESS:
    case EditField::DEVICE_REMOTE:
    case EditField::LEARN_SOURCE:
    case EditField::RAW_SOURCE:
      return {FieldFormat::HEXADECIMAL, 1, 0xFFFFFF};
    case EditField::RAW_DESTINATION:
      return {FieldFormat::HEXADECIMAL, 0, 0xFFFFFF};
    case EditField::REMOTE_FILTER:
    case EditField::PACKET_ADDRESS:
      return {FieldFormat::HEXADECIMAL, 0, 0xFFFFFF, true};
    case EditField::DEVICE_CHANNEL:
    case EditField::LEARN_CHANNEL:
    case EditField::REMOTE_CHANNEL:
      return {FieldFormat::DECIMAL, 1, 255};
    case EditField::RAW_CHANNEL:
      return {FieldFormat::DECIMAL, 0, 255};
    case EditField::PACKET_CHANNEL:
      return {FieldFormat::DECIMAL, 0, 255, true};
    case EditField::RAW_COMMAND:
    case EditField::RAW_TYPE:
    case EditField::RAW_TYPE2:
    case EditField::RAW_HOP:
    case EditField::RAW_PAYLOAD1:
    case EditField::RAW_PAYLOAD2:
      return {FieldFormat::HEXADECIMAL, 0, 255};
    case EditField::PACKET_TYPE:
    case EditField::PACKET_COMMAND:
    case EditField::PACKET_STATE:
      return {FieldFormat::HEXADECIMAL, 0, 255, true};
    case EditField::OPEN_DURATION:
    case EditField::CLOSE_DURATION:
    case EditField::DIM_DURATION:
      return {FieldFormat::DECIMAL, 0, 300000};
    case EditField::LEARN_TIMEOUT:
      return {FieldFormat::DECIMAL, 5, 3600};
    case EditField::DEVICE_NAME:
    case EditField::DEVICE_TYPE:
    case EditField::DEVICE_ENABLED:
    case EditField::DEVICE_TILT:
    case EditField::GROUP_NAME:
    case EditField::HUB_NAME:
    case EditField::SEARCH:
      return {FieldFormat::TEXT, 0, 23, true};
  }
  return {FieldFormat::TEXT, 0, 0};
}
}  // namespace esphome::elero_paper
