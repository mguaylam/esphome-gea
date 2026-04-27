#pragma once

// Pure-C++ GEA3 protocol primitives — no ESPHome dependencies.
// Kept separate from gea.h so the algorithms can be unit-tested host-side.

#include <cstdint>
#include <cstddef>
#include <vector>

namespace esphome {
namespace gea {
namespace protocol {

static constexpr uint8_t STX = 0xE2;
static constexpr uint8_t ETX = 0xE3;
static constexpr uint8_t ESC = 0xE0;
static constexpr uint8_t ACK = 0xE1;
static constexpr uint16_t CRC_SEED = 0x1021;

// CRC-16/CCITT, polynomial 0x1021, seed 0x1021, MSB-first output.
inline uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = CRC_SEED;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t) data[i] << 8;
    for (int j = 0; j < 8; j++) {
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
    }
  }
  return crc;
}

// Escape control bytes (0xE0–0xE3) by prefixing with ESC (0xE0).
// The original byte is sent unchanged after the escape.
inline std::vector<uint8_t> escape(const std::vector<uint8_t> &raw) {
  std::vector<uint8_t> out;
  out.reserve(raw.size() + 4);
  for (uint8_t b : raw) {
    if (b >= 0xE0 && b <= 0xE3) {
      out.push_back(ESC);
      out.push_back(b);
    } else {
      out.push_back(b);
    }
  }
  return out;
}

// Reverse of escape(). Walks through `in` and unescapes any ESC-prefixed byte.
// Returns the unescaped buffer; bytes that are themselves STX/ETX in the
// stream act as delimiters and are NOT included.
inline std::vector<uint8_t> unescape(const std::vector<uint8_t> &in) {
  std::vector<uint8_t> out;
  out.reserve(in.size());
  bool esc = false;
  for (uint8_t b : in) {
    if (esc) {
      out.push_back(b);
      esc = false;
    } else if (b == ESC) {
      esc = true;
    } else {
      out.push_back(b);
    }
  }
  return out;
}

}  // namespace protocol
}  // namespace gea
}  // namespace esphome
