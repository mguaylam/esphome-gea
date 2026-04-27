// Host-side unit tests for the GEA3 protocol primitives.
// Build: make -C tests   →   runs test binary on success.

#include "../components/gea/protocol.h"

#include <cassert>
#include <cstdio>
#include <vector>

using esphome::gea::protocol::crc16;
using esphome::gea::protocol::escape;
using esphome::gea::protocol::unescape;

static int failures = 0;

#define EXPECT(cond, msg)                                          \
  do {                                                             \
    if (!(cond)) {                                                 \
      std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
      failures++;                                                  \
    }                                                              \
  } while (0)

static void test_crc16_seed_only() {
  // CRC of zero-length input is the seed itself.
  EXPECT(crc16(nullptr, 0) == 0x1021, "crc16 empty");
}

static void test_crc16_known_vector() {
  // Known good frame captured from a real GE PFQ97HSPVDS:
  // DEST=0xC0 LEN=0x08 SRC=0xBB CMD=0xA4 REQ_ID=0x01 TYPE=0x00
  // CRC over [DEST,LEN,SRC,CMD,REQ_ID,TYPE].
  const uint8_t frame[] = {0xC0, 0x08, 0xBB, 0xA4, 0x01, 0x00};
  uint16_t crc = crc16(frame, sizeof(frame));
  // Recomputed independently from the same algorithm — locks behaviour.
  EXPECT(crc == 0xB9DF, "crc16 known vector");
}

static void test_crc16_changes_on_bit_flip() {
  const uint8_t a[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t b[] = {0x01, 0x02, 0x03, 0x05};
  EXPECT(crc16(a, 4) != crc16(b, 4), "crc16 bit-flip detection");
}

static void test_escape_passthrough() {
  std::vector<uint8_t> in = {0x00, 0x55, 0xAA, 0xDF};
  auto out = escape(in);
  EXPECT(out == in, "escape passthrough for non-control bytes");
}

static void test_escape_control_bytes() {
  // Each control byte 0xE0..0xE3 should be prefixed with ESC (0xE0).
  std::vector<uint8_t> in = {0xE0, 0xE1, 0xE2, 0xE3};
  auto out = escape(in);
  std::vector<uint8_t> expected = {0xE0, 0xE0, 0xE0, 0xE1, 0xE0, 0xE2, 0xE0, 0xE3};
  EXPECT(out == expected, "escape control bytes prefixed with 0xE0");
}

static void test_escape_unescape_roundtrip() {
  std::vector<uint8_t> in = {0x01, 0xE2, 0x02, 0xE0, 0x03, 0xE3, 0x04};
  auto out = unescape(escape(in));
  EXPECT(out == in, "escape/unescape roundtrip");
}

static void test_unescape_no_esc() {
  std::vector<uint8_t> in = {0x10, 0x20, 0x30};
  auto out = unescape(in);
  EXPECT(out == in, "unescape passthrough");
}

int main() {
  test_crc16_seed_only();
  test_crc16_known_vector();
  test_crc16_changes_on_bit_flip();
  test_escape_passthrough();
  test_escape_control_bytes();
  test_escape_unescape_roundtrip();
  test_unescape_no_esc();

  if (failures == 0) {
    std::printf("All tests passed.\n");
    return 0;
  }
  std::printf("%d test(s) failed.\n", failures);
  return 1;
}
