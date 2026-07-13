#pragma once

// ASCII string ERD decoding, kept free of ESPHome dependencies so it can be
// exercised by the host unit tests (tests/host/).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace gea {

// GE appliances encode string ERDs (e.g. Model Number 0x0001) in one of two
// conventions:
//   - fixed-size, null-padded (what erd-definitions declares), or
//   - length-prefixed: a leading count byte followed by exactly that many
//     characters (observed on a GYE21JYMCFFS refrigerator, see issue #21).
// A leading byte is only treated as a length prefix when it announces exactly
// the number of remaining bytes AND is not a printable character — a compliant
// null-padded string always starts with a printable character, so it can never
// match. Requiring exact equality also covers the malformed case of a declared
// length exceeding the payload: such data is left untouched.
inline bool is_length_prefixed_string(const std::vector<uint8_t> &data) {
  return !data.empty() && data[0] < 0x20 && (size_t)(data[0]) == data.size() - 1;
}

// Decode an ASCII string ERD payload: strip the length prefix when present,
// then trim trailing nulls (padding in the fixed-size convention).
inline std::string decode_ascii_string(const std::vector<uint8_t> &data) {
  size_t start = is_length_prefixed_string(data) ? 1 : 0;
  size_t end = data.size();
  while (end > start && data[end - 1] == 0x00)
    end--;
  return std::string(data.begin() + start, data.begin() + end);
}

}  // namespace gea
}  // namespace esphome
