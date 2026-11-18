// Stub for unit tests
#pragma once
#include <cstdint>
#include <string>

namespace esphome {

uint32_t fnv1_hash(const std::string &str);
uint32_t fnv1_hash_object_id(const char *name, size_t length);

}  // namespace esphome
