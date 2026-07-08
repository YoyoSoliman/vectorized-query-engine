#ifndef STRING_VIEW_H
#define STRING_VIEW_H

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

struct StringView {
  uint32_t length;

  union {
    // If length <= 12: store characters inline directly in the remaining 12
    // bytes
    char inline_chars[12];

    // If length > 12: store a 4-byte content prefix along with the heap memory
    // offset
    struct {
      char prefix[4];
      uint32_t offset;
    } heap;
  } content;

  // Helper to extract the actual string out of the customized handle/arena
  // layout
  std::string_view MapToString(const std::vector<char> &arena) const {
    if (length <= 12) {
      return std::string_view(content.inline_chars, length);
    } else {
      return std::string_view(&arena[content.heap.offset], length);
    }
  }
};

#endif // STRING_VIEW_H
