/// @file overloaded.h
/// @brief std::visit overloaded pattern helper (C++17).

#pragma once

namespace esphome::elero {

/// Helper for std::visit with multiple lambdas.
/// Usage: std::visit(overloaded{[](Idle&){...}, [](Opening&){...}}, state)
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

}  // namespace esphome::elero
