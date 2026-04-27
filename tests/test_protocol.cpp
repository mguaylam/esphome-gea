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

// Build a GEA3 inner frame [DEST,LEN,SRC,PAYLOAD,CRC_HI,CRC_LO] and verify
// that escaping then unescaping reproduces the same buffer, and that the CRC
// recomputed over the unescaped frame matches the embedded CRC.  This is the
// full TX→RX path the firmware exercises on every packet.
static void test_full_frame_roundtrip() {
  uint8_t dest = 0xC0;
  uint8_t src = 0xBB;
  // Payload is intentionally chosen to contain a control byte (0xE2 = STX)
  // so the escape pass has something real to do.
  std::vector<uint8_t> payload = {0xA2, 0x01, 0x21, 0x49, 0x01, 0xE2};
  uint8_t len = (uint8_t) (7 + payload.size());

  std::vector<uint8_t> inner;
  inner.push_back(dest);
  inner.push_back(len);
  inner.push_back(src);
  inner.insert(inner.end(), payload.begin(), payload.end());

  uint16_t crc = crc16(inner.data(), inner.size());
  inner.push_back(crc >> 8);
  inner.push_back(crc & 0xFF);

  // Escape then unescape must return the exact same byte sequence.
  auto wire = escape(inner);
  auto recovered = unescape(wire);
  EXPECT(recovered == inner, "frame escape/unescape roundtrip");

  // CRC over the recovered inner (minus its 2 trailing CRC bytes) must match
  // the CRC that travelled on the wire.
  size_t crc_off = recovered.size() - 2;
  uint16_t rx_crc = ((uint16_t) recovered[crc_off] << 8) | recovered[crc_off + 1];
  uint16_t calc_crc = crc16(recovered.data(), crc_off);
  EXPECT(rx_crc == calc_crc, "frame CRC validates after roundtrip");
}

// CRC validation must reject any single bit flip in the payload — regression
// guard against a CRC implementation that silently degrades to a checksum.
static void test_crc_rejects_corruption() {
  std::vector<uint8_t> frame = {0xC0, 0x08, 0xBB, 0xA4, 0x01, 0x00};
  uint16_t good = crc16(frame.data(), frame.size());

  for (size_t i = 0; i < frame.size(); i++) {
    frame[i] ^= 0x01;  // flip lowest bit
    EXPECT(crc16(frame.data(), frame.size()) != good,
           "single-bit corruption changes CRC");
    frame[i] ^= 0x01;  // restore
  }
}

int main() {
  test_crc16_seed_only();
  test_crc16_known_vector();
  test_crc16_changes_on_bit_flip();
  test_escape_passthrough();
  test_escape_control_bytes();
  test_escape_unescape_roundtrip();
  test_unescape_no_esc();
  test_full_frame_roundtrip();
  test_crc_rejects_corruption();

  if (failures == 0) {
    std::printf("All tests passed.\n");
    return 0;
  }
  std::printf("%d test(s) failed.\n", failures);
  return 1;
}
