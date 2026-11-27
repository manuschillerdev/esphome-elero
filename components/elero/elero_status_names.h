// Generated from RfStateName in asyncapi.yaml by pnpm generate:types.
#pragma once

#include <array>
#include "elero_packet.h"

namespace esphome::elero {
inline constexpr std::array ELERO_STATUS_NAMES{
    "unknown",
    "top",
    "bottom",
    "intermediate",
    "tilt",
    "blocking",
    "overheated",
    "timeout",
    "start_moving_up",
    "start_moving_down",
    "moving_up",
    "moving_down",
    "delete_in_remote",
    "stopped",
    "top_tilt",
    "bottom_tilt",
    "light_off",
    "light_on",
    "scene_set",
    "scene_started",
    "scene_stopped",
    "scene_error",
};

static_assert(packet::state::UNKNOWN == 0x00);
static_assert(packet::state::TOP == 0x01);
static_assert(packet::state::BOTTOM == 0x02);
static_assert(packet::state::INTERMEDIATE == 0x03);
static_assert(packet::state::TILT == 0x04);
static_assert(packet::state::BLOCKING == 0x05);
static_assert(packet::state::OVERHEATED == 0x06);
static_assert(packet::state::TIMEOUT == 0x07);
static_assert(packet::state::START_MOVING_UP == 0x08);
static_assert(packet::state::START_MOVING_DOWN == 0x09);
static_assert(packet::state::MOVING_UP == 0x0a);
static_assert(packet::state::MOVING_DOWN == 0x0b);
static_assert(packet::state::DELETE_IN_REMOTE == 0x0c);
static_assert(packet::state::STOPPED == 0x0d);
static_assert(packet::state::TOP_TILT == 0x0e);
static_assert(packet::state::BOTTOM_TILT == 0x0f);
static_assert(packet::state::LIGHT_OFF == 0x10);
static_assert(packet::state::LIGHT_ON == 0x11);
static_assert(packet::state::SCENE_SET == 0x12);
static_assert(packet::state::SCENE_STARTED == 0x13);
static_assert(packet::state::SCENE_STOPPED == 0x14);
static_assert(packet::state::SCENE_ERROR == 0x15);
}  // namespace esphome::elero
