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
//   - length-prefixed: a leading count byte followed by that many characters,
//     itself possibly null-padded to a fixed size (observed on a GYE21JYMCFFS
//     refrigerator, see issue #21 and PR #25).
// A leading byte is only treated as a length prefix when it is not a printable
// character AND it announces exactly the number of remaining bytes once
// trailing null padding is ignored — everything past the declared length must
// be padding. A compliant null-padded string always starts with a printable
// character, so it can never match, and a declared length that exceeds the
// payload (or leaves non-null bytes unaccounted for) is left untouched.
inline bool is_length_prefixed_string(const std::vector<uint8_t> &data) {
  if (data.empty() || data[0] >= 0x20)
    return false;
  size_t end = data.size();
  while (end > 1 && data[end - 1] == 0x00)
    end--;
  return (size_t)(data[0]) == end - 1;
}

// Decode an ASCII string ERD payload: read exactly the declared count when a
// length prefix is present, otherwise trim trailing nulls (padding in the
// fixed-size convention).
inline std::string decode_ascii_string(const std::vector<uint8_t> &data) {
  if (is_length_prefixed_string(data))
    return std::string(data.begin() + 1, data.begin() + 1 + data[0]);
  size_t end = data.size();
  while (end > 0 && data[end - 1] == 0x00)
    end--;
  return std::string(data.begin(), data.begin() + end);
}

}  // namespace gea
}  // namespace esphome
