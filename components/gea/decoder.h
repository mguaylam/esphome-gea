#pragma once

// Pure-C++ ERD payload decoding/encoding — no ESPHome dependencies.
// Kept separate from gea.h so the algorithms can be unit-tested host-side.

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace esphome {
namespace gea {

// Mirrors the YAML-facing decode types. Defined here so decoder.h has no
// dependency on gea.h. gea.h re-exports the same enum tag through a using
// declaration.
enum GeaDecodeType {
  UINT8,
  UINT16_BE,
  UINT16_LE,
  UINT32_BE,
  UINT32_LE,
  INT8,
  INT16_BE,
  INT16_LE,
  INT32_BE,
  INT32_LE,
  BOOL,
  RAW,
  ASCII,
};

namespace decoder {

// Edge tag for on_erd_change. Mirrors ErdChangeTrigger::Edge so evaluate_edge
// stays in pure-C++ land.
enum Edge : uint8_t {
  EDGE_RISING = 0,
  EDGE_FALLING = 1,
  EDGE_ANY = 2,
};

// Decode a slice of an ERD payload into a float, applying scaling.
//   output = (raw value) * multiplier + offset
//
// `data`        — full ERD payload
// `decode`      — interpretation of the bytes at byte_offset
// `byte_offset` — start position within `data`
// `bitmask`     — used by BOOL decode only (returns 1.0 if (data[off] & mask) != 0)
// `multiplier`/`offset` — applied for all numeric types EXCEPT BOOL
//
// On any size mismatch, returns 0.0f.
inline float decode_float(GeaDecodeType decode,
                          const std::vector<uint8_t> &data,
                          uint8_t byte_offset = 0,
                          uint8_t bitmask = 0xFF,
                          float multiplier = 1.0f,
                          float offset = 0.0f) {
  if (data.size() <= (size_t) byte_offset)
    return 0.0f;
  const uint8_t *d = data.data() + byte_offset;
  size_t rem = data.size() - byte_offset;
  float raw = 0.0f;

  switch (decode) {
    case UINT8:
      raw = (rem >= 1) ? (float) d[0] : 0.0f;
      break;
    case UINT16_BE:
      raw = (rem >= 2) ? (float) ((uint16_t) d[0] << 8 | d[1]) : 0.0f;
      break;
    case UINT16_LE:
      raw = (rem >= 2) ? (float) ((uint16_t) d[1] << 8 | d[0]) : 0.0f;
      break;
    case UINT32_BE:
      if (rem >= 4)
        raw = (float) ((uint32_t) d[0] << 24 | (uint32_t) d[1] << 16 | (uint32_t) d[2] << 8 | d[3]);
      else if (rem == 3)
        raw = (float) ((uint32_t) d[0] << 16 | (uint32_t) d[1] << 8 | d[2]);
      break;
    case UINT32_LE:
      raw = (rem >= 4)
          ? (float) ((uint32_t) d[3] << 24 | (uint32_t) d[2] << 16 | (uint32_t) d[1] << 8 | d[0])
          : 0.0f;
      break;
    case INT8:
      raw = (rem >= 1) ? (float) (int8_t) d[0] : 0.0f;
      break;
    case INT16_BE:
      raw = (rem >= 2) ? (float) (int16_t) ((uint16_t) d[0] << 8 | d[1]) : 0.0f;
      break;
    case INT16_LE:
      raw = (rem >= 2) ? (float) (int16_t) ((uint16_t) d[1] << 8 | d[0]) : 0.0f;
      break;
    case INT32_BE:
      raw = (rem >= 4)
          ? (float) (int32_t) ((uint32_t) d[0] << 24 | (uint32_t) d[1] << 16 | (uint32_t) d[2] << 8 | d[3])
          : 0.0f;
      break;
    case INT32_LE:
      raw = (rem >= 4)
          ? (float) (int32_t) ((uint32_t) d[3] << 24 | (uint32_t) d[2] << 16 | (uint32_t) d[1] << 8 | d[0])
          : 0.0f;
      break;
    case BOOL:
      // BOOL is unscaled — returns 0 or 1 based on the masked bit(s).
      return (rem >= 1) ? ((d[0] & bitmask) ? 1.0f : 0.0f) : 0.0f;
    default:
      return 0.0f;
  }
  return raw * multiplier + offset;
}

// Encode an integer value to bytes using the same width and endianness rules
// as decode_float() expects on the way back. Used by writable entities.
//   `data_size` — number of bytes to emit. If 0, derived from `decode`.
inline std::vector<uint8_t> encode_bytes(GeaDecodeType decode,
                                         uint8_t data_size,
                                         uint32_t val) {
  std::vector<uint8_t> out;
  uint8_t n = data_size;
  if (n == 0) {
    switch (decode) {
      case UINT16_BE:
      case INT16_BE:
      case UINT16_LE:
      case INT16_LE:
        n = 2;
        break;
      case UINT32_BE:
      case INT32_BE:
      case UINT32_LE:
      case INT32_LE:
        n = 4;
        break;
      default:
        n = 1;
        break;
    }
  }
  bool le = false;
  switch (decode) {
    case UINT16_LE:
    case INT16_LE:
    case UINT32_LE:
    case INT32_LE:
      le = true;
      break;
    default:
      break;
  }
  out.reserve(n);
  if (le) {
    for (uint8_t i = 0; i < n; i++)
      out.push_back((val >> (i * 8)) & 0xFF);
  } else {
    for (int i = (int) n - 1; i >= 0; i--)
      out.push_back((val >> (i * 8)) & 0xFF);
  }
  return out;
}

// Format a byte vector as a hex string prefixed with "0x", e.g. {0x12, 0x34} → "0x1234".
// Empty input returns "0x".
inline std::string decode_hex(const std::vector<uint8_t> &data) {
  if (data.empty())
    return "0x";
  std::string result = "0x";
  static const char *digits = "0123456789ABCDEF";
  for (uint8_t b : data) {
    result += digits[(b >> 4) & 0xF];
    result += digits[b & 0xF];
  }
  return result;
}

// Reverse of decode_float() for the multiplier/offset transform on writable
// entities. Returns the raw integer value to write to the wire.
//   raw = (value - offset) / multiplier   (or 0 if multiplier == 0)
inline uint32_t reverse_scale(float value, float multiplier, float offset) {
  if (multiplier == 0.0f)
    return 0;
  return (uint32_t) ((value - offset) / multiplier);
}

// Evaluate an on_erd_change edge condition between two ERD publications.
// Returns false if either buffer is too short for byte_offset.
inline bool evaluate_edge(Edge edge,
                          const std::vector<uint8_t> &old_data,
                          const std::vector<uint8_t> &new_data,
                          uint8_t byte_offset,
                          uint8_t bitmask) {
  if (byte_offset >= new_data.size() || byte_offset >= old_data.size())
    return false;
  uint8_t old_masked = old_data[byte_offset] & bitmask;
  uint8_t new_masked = new_data[byte_offset] & bitmask;
  switch (edge) {
    case EDGE_RISING:
      return old_masked == 0 && new_masked != 0;
    case EDGE_FALLING:
      return old_masked != 0 && new_masked == 0;
    case EDGE_ANY:
      return old_masked != new_masked;
  }
  return false;
}

}  // namespace decoder
}  // namespace gea
}  // namespace esphome
