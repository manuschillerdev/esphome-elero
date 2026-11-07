#pragma once

#include <array>
#include "elero_strings.h"

namespace esphome::elero {

/// Transport-neutral metadata for the explicit cover actions. Packet selection
/// and command admission remain in DeviceRegistry.
struct CoverActionInfo {
  const char *action;
  const char *name;
};

inline constexpr std::array<CoverActionInfo, 4> ELERO_COVER_ACTIONS{{
    {action::STEP_UP, "Short press up"},
    {action::STEP_DOWN, "Short press down"},
    {action::FULL_UP, "Full up"},
    {action::FULL_DOWN, "Full down"},
}};

}  // namespace esphome::elero
