#pragma once

#include <string>

namespace esphome::elero_paper {

// Remove a complete UTF-8 code point, including when shortening imported names.
inline void pop_codepoint(std::string &text) {
  if (text.empty())
    return;
  size_t start = text.size() - 1;
  while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80)
    --start;
  text.erase(start);
}

}  // namespace esphome::elero_paper
