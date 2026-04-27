// Host-side unit tests for ERD payload decoding/encoding.
// Covers all decode types, multiplier/offset transforms, encoding roundtrips,
// edge-trigger evaluation, and hex formatting.

#include "../components/gea/decoder.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using esphome::gea::GeaDecodeType;
using esphome::gea::decoder::decode_float;
using esphome::gea::decoder::decode_hex;
using esphome::gea::decoder::encode_bytes;
using esphome::gea::decoder::evaluate_edge;
using esphome::gea::decoder::reverse_scale;
using esphome::gea::decoder::Edge;

static int failures = 0;

#define EXPECT(cond, msg)                                          \
  do {                                                             \
    if (!(cond)) {                                                 \
      std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
      failures++;                                                  \
    }                                                              \
  } while (0)

static bool feq(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// ---------------------------------------------------------------------------
// Unsigned decoders
// ---------------------------------------------------------------------------

static void test_uint8() {
  std::vector<uint8_t> d = {0x00, 0x42, 0xFF};
  EXPECT(decode_float(GeaDecodeType::UINT8, d, 0) == 0.0f, "uint8 @0");
  EXPECT(decode_float(GeaDecodeType::UINT8, d, 1) == 66.0f, "uint8 @1");
  EXPECT(decode_float(GeaDecodeType::UINT8, d, 2) == 255.0f, "uint8 @2 = 0xFF");
}

static void test_uint16_be_le() {
  // 0x1234 in big-endian, 0x3412 in little-endian on the same bytes.
  std::vector<uint8_t> d = {0x12, 0x34};
  EXPECT(decode_float(GeaDecodeType::UINT16_BE, d) == 0x1234, "uint16_be");
  EXPECT(decode_float(GeaDecodeType::UINT16_LE, d) == 0x3412, "uint16_le");
}

static void test_uint32_be_le() {
  std::vector<uint8_t> d = {0xDE, 0xAD, 0xBE, 0xEF};
  EXPECT(decode_float(GeaDecodeType::UINT32_BE, d) == (float) 0xDEADBEEFu, "uint32_be");
  EXPECT(decode_float(GeaDecodeType::UINT32_LE, d) == (float) 0xEFBEADDEu, "uint32_le");
}

static void test_uint32_be_3byte_fallback() {
  // GE sometimes ships 3-byte counters. The decoder should still read them
  // big-endian rather than returning 0.
  std::vector<uint8_t> d = {0x01, 0x00, 0x00};
  EXPECT(decode_float(GeaDecodeType::UINT32_BE, d) == 65536.0f, "uint32_be 3-byte fallback");
}

// ---------------------------------------------------------------------------
// Signed decoders — the ones that historically catch sign-extension bugs.
// ---------------------------------------------------------------------------

static void test_int8() {
  std::vector<uint8_t> d = {0xFF};  // -1 as int8
  EXPECT(decode_float(GeaDecodeType::INT8, d) == -1.0f, "int8 -1");
  std::vector<uint8_t> d2 = {0x80};  // INT8_MIN
  EXPECT(decode_float(GeaDecodeType::INT8, d2) == -128.0f, "int8 -128");
}

static void test_int16_be() {
  std::vector<uint8_t> d = {0xFF, 0xFE};  // -2 as int16_be
  EXPECT(decode_float(GeaDecodeType::INT16_BE, d) == -2.0f, "int16_be -2");
  std::vector<uint8_t> d2 = {0x80, 0x00};  // INT16_MIN
  EXPECT(decode_float(GeaDecodeType::INT16_BE, d2) == -32768.0f, "int16_be INT16_MIN");
}

static void test_int16_le() {
  std::vector<uint8_t> d = {0xFE, 0xFF};  // -2 as int16_le
  EXPECT(decode_float(GeaDecodeType::INT16_LE, d) == -2.0f, "int16_le -2");
}

static void test_int32_be_le() {
  std::vector<uint8_t> be = {0xFF, 0xFF, 0xFF, 0xFD};  // -3 as int32_be
  EXPECT(decode_float(GeaDecodeType::INT32_BE, be) == -3.0f, "int32_be -3");
  std::vector<uint8_t> le = {0xFD, 0xFF, 0xFF, 0xFF};  // -3 as int32_le
  EXPECT(decode_float(GeaDecodeType::INT32_LE, le) == -3.0f, "int32_le -3");
}

// ---------------------------------------------------------------------------
// Boolean decoder
// ---------------------------------------------------------------------------

static void test_bool() {
  std::vector<uint8_t> d = {0b00000100};
  EXPECT(decode_float(GeaDecodeType::BOOL, d, 0, 0x04) == 1.0f, "bool bit set");
  EXPECT(decode_float(GeaDecodeType::BOOL, d, 0, 0x02) == 0.0f, "bool bit clear");
}

static void test_bool_ignores_multiplier_offset() {
  // BOOL is intentionally unscaled — applying multiplier/offset would break
  // existing binary_sensor / boolean configurations.
  std::vector<uint8_t> d = {0x01};
  float val = decode_float(GeaDecodeType::BOOL, d, 0, 0x01,
                           /*multiplier=*/100.0f, /*offset=*/50.0f);
  EXPECT(val == 1.0f, "bool ignores multiplier/offset");
}

// ---------------------------------------------------------------------------
// Multiplier + offset transform — the new behaviour.
// ---------------------------------------------------------------------------

static void test_multiplier_offset_temperature() {
  // GE encodes water temperature × 10 in a uint16_be: raw 752 → 75.2 °C.
  std::vector<uint8_t> d = {0x02, 0xF0};  // 0x02F0 = 752
  float v = decode_float(GeaDecodeType::UINT16_BE, d, 0, 0xFF, 0.1f, 0.0f);
  EXPECT(feq(v, 75.2f), "temperature × 10 with multiplier 0.1");
}

static void test_multiplier_offset_signed() {
  // int16_be value -50 with multiplier 0.5 + offset 100 → 75
  std::vector<uint8_t> d = {0xFF, 0xCE};  // -50
  float v = decode_float(GeaDecodeType::INT16_BE, d, 0, 0xFF, 0.5f, 100.0f);
  EXPECT(feq(v, 75.0f), "signed multiplier + offset");
}

static void test_multiplier_default_passthrough() {
  // multiplier=1.0, offset=0.0 → identity.
  std::vector<uint8_t> d = {0x00, 0x64};  // 100
  EXPECT(decode_float(GeaDecodeType::UINT16_BE, d) == 100.0f, "identity decode");
}

// ---------------------------------------------------------------------------
// reverse_scale — used by GEANumber::control
// ---------------------------------------------------------------------------

static void test_reverse_scale_roundtrip() {
  // user enters 75.2 in HA → must serialise as raw 752 for ×10 encoding.
  EXPECT(reverse_scale(75.2f, 0.1f, 0.0f) == 752, "reverse_scale 75.2/0.1");
}

static void test_reverse_scale_with_offset() {
  // Forward transform: published = raw * 0.5 + 100.
  // User enters 125 in HA → raw must be (125 - 100) / 0.5 = 50.
  EXPECT(reverse_scale(125.0f, 0.5f, 100.0f) == 50, "reverse_scale 125 with offset");
}

static void test_reverse_scale_zero_multiplier() {
  // Defensive: don't divide by zero — return 0 instead.
  EXPECT(reverse_scale(42.0f, 0.0f, 0.0f) == 0, "reverse_scale guards mult=0");
}

// ---------------------------------------------------------------------------
// encode_bytes — endianness and width selection
// ---------------------------------------------------------------------------

static void test_encode_uint16_be() {
  auto out = encode_bytes(GeaDecodeType::UINT16_BE, 0, 0x1234);
  EXPECT(out == std::vector<uint8_t>({0x12, 0x34}), "encode uint16_be");
}

static void test_encode_uint16_le() {
  auto out = encode_bytes(GeaDecodeType::UINT16_LE, 0, 0x1234);
  EXPECT(out == std::vector<uint8_t>({0x34, 0x12}), "encode uint16_le");
}

static void test_encode_uint32_be() {
  auto out = encode_bytes(GeaDecodeType::UINT32_BE, 0, 0xDEADBEEF);
  EXPECT(out == std::vector<uint8_t>({0xDE, 0xAD, 0xBE, 0xEF}), "encode uint32_be");
}

static void test_encode_uint8() {
  auto out = encode_bytes(GeaDecodeType::UINT8, 0, 0x42);
  EXPECT(out == std::vector<uint8_t>({0x42}), "encode uint8");
}

static void test_encode_explicit_data_size() {
  // Override default width: serialize a uint8 value into 2 bytes BE.
  auto out = encode_bytes(GeaDecodeType::UINT8, 2, 0x42);
  EXPECT(out == std::vector<uint8_t>({0x00, 0x42}), "encode explicit data_size=2");
}

// ---------------------------------------------------------------------------
// encode → decode roundtrip
// ---------------------------------------------------------------------------

static void test_encode_decode_roundtrip_be() {
  for (uint16_t v : {(uint16_t) 0u, (uint16_t) 1u, (uint16_t) 0x1234u, (uint16_t) 0xFFFFu}) {
    auto bytes = encode_bytes(GeaDecodeType::UINT16_BE, 0, v);
    EXPECT(decode_float(GeaDecodeType::UINT16_BE, bytes) == (float) v, "uint16_be roundtrip");
  }
}

static void test_encode_decode_roundtrip_le() {
  for (uint32_t v : {(uint32_t) 0u, (uint32_t) 1u, (uint32_t) 0xCAFEBABEu}) {
    auto bytes = encode_bytes(GeaDecodeType::UINT32_LE, 0, v);
    EXPECT(decode_float(GeaDecodeType::UINT32_LE, bytes) == (float) v, "uint32_le roundtrip");
  }
}

// ---------------------------------------------------------------------------
// byte_offset handling
// ---------------------------------------------------------------------------

static void test_byte_offset() {
  // ERD with header bytes followed by a uint16_be value at offset 2.
  std::vector<uint8_t> d = {0xAA, 0xBB, 0x12, 0x34};
  EXPECT(decode_float(GeaDecodeType::UINT16_BE, d, 2) == 0x1234, "byte_offset selection");
}

static void test_byte_offset_out_of_bounds() {
  std::vector<uint8_t> d = {0x01};
  EXPECT(decode_float(GeaDecodeType::UINT16_BE, d, 5) == 0.0f, "byte_offset OOB returns 0");
}

static void test_short_payload_returns_zero() {
  // Only 1 byte but caller asked for uint16 → must return 0, not segfault.
  std::vector<uint8_t> d = {0x42};
  EXPECT(decode_float(GeaDecodeType::UINT16_BE, d) == 0.0f, "short payload returns 0");
}

// ---------------------------------------------------------------------------
// hex formatting
// ---------------------------------------------------------------------------

static void test_decode_hex() {
  EXPECT(decode_hex({}) == "0x", "empty hex");
  EXPECT(decode_hex({0x00}) == "0x00", "hex single zero byte");
  EXPECT(decode_hex({0xDE, 0xAD, 0xBE, 0xEF}) == "0xDEADBEEF", "hex multi-byte");
  EXPECT(decode_hex({0x0A}) == "0x0A", "hex zero-padding");
}

// ---------------------------------------------------------------------------
// Edge-trigger evaluation
// ---------------------------------------------------------------------------

static void test_edge_rising() {
  std::vector<uint8_t> old_d = {0x00};
  std::vector<uint8_t> new_d = {0x01};
  EXPECT(evaluate_edge(Edge::EDGE_RISING, old_d, new_d, 0, 0x01),
         "rising 0→1");
  EXPECT(!evaluate_edge(Edge::EDGE_RISING, new_d, old_d, 0, 0x01),
         "rising NOT on 1→0");
  EXPECT(!evaluate_edge(Edge::EDGE_RISING, new_d, new_d, 0, 0x01),
         "rising NOT on 1→1");
}

static void test_edge_falling() {
  std::vector<uint8_t> old_d = {0x01};
  std::vector<uint8_t> new_d = {0x00};
  EXPECT(evaluate_edge(Edge::EDGE_FALLING, old_d, new_d, 0, 0x01),
         "falling 1→0");
  EXPECT(!evaluate_edge(Edge::EDGE_FALLING, new_d, old_d, 0, 0x01),
         "falling NOT on 0→1");
}

static void test_edge_any() {
  std::vector<uint8_t> old_d = {0x00};
  std::vector<uint8_t> new_d = {0x01};
  EXPECT(evaluate_edge(Edge::EDGE_ANY, old_d, new_d, 0, 0x01), "any 0→1");
  EXPECT(evaluate_edge(Edge::EDGE_ANY, new_d, old_d, 0, 0x01), "any 1→0");
  EXPECT(!evaluate_edge(Edge::EDGE_ANY, old_d, old_d, 0, 0x01), "any NOT on 0→0");
}

static void test_edge_bitmask_isolation() {
  // Other bits flipping but the target bit stays unchanged → no trigger.
  std::vector<uint8_t> old_d = {0b00000001};
  std::vector<uint8_t> new_d = {0b11111101};  // bit 1 changed but our mask is bit 0 (0x01)
  EXPECT(!evaluate_edge(Edge::EDGE_ANY, old_d, new_d, 0, 0x01),
         "any ignores bits outside mask");
}

static void test_edge_byte_offset() {
  // Watch byte 1 of a 2-byte ERD.
  std::vector<uint8_t> old_d = {0xFF, 0x00};
  std::vector<uint8_t> new_d = {0xFF, 0x20};
  EXPECT(evaluate_edge(Edge::EDGE_RISING, old_d, new_d, 1, 0x20),
         "rising on byte_offset=1");
}

static void test_edge_short_data() {
  // byte_offset >= data.size() must not crash.
  std::vector<uint8_t> old_d = {0x00};
  std::vector<uint8_t> new_d = {0x01};
  EXPECT(!evaluate_edge(Edge::EDGE_RISING, old_d, new_d, 5, 0x01),
         "edge OOB returns false");
}

// ---------------------------------------------------------------------------

int main() {
  test_uint8();
  test_uint16_be_le();
  test_uint32_be_le();
  test_uint32_be_3byte_fallback();
  test_int8();
  test_int16_be();
  test_int16_le();
  test_int32_be_le();
  test_bool();
  test_bool_ignores_multiplier_offset();
  test_multiplier_offset_temperature();
  test_multiplier_offset_signed();
  test_multiplier_default_passthrough();
  test_reverse_scale_roundtrip();
  test_reverse_scale_with_offset();
  test_reverse_scale_zero_multiplier();
  test_encode_uint16_be();
  test_encode_uint16_le();
  test_encode_uint32_be();
  test_encode_uint8();
  test_encode_explicit_data_size();
  test_encode_decode_roundtrip_be();
  test_encode_decode_roundtrip_le();
  test_byte_offset();
  test_byte_offset_out_of_bounds();
  test_short_payload_returns_zero();
  test_decode_hex();
  test_edge_rising();
  test_edge_falling();
  test_edge_any();
  test_edge_bitmask_isolation();
  test_edge_byte_offset();
  test_edge_short_data();

  if (failures == 0) {
    std::printf("test_decoder: all tests passed.\n");
    return 0;
  }
  std::printf("test_decoder: %d test(s) failed.\n", failures);
  return 1;
}
