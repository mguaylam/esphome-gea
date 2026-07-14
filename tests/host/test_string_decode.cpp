// Host unit tests for the ASCII string ERD decoder (components/gea/string_decode.h).
// Built and run by the host-test job in .github/workflows/test.yml:
//   g++ -std=c++17 -Wall -Wextra -Werror -o test tests/host/test_string_decode.cpp && ./test

#include "../../components/gea/string_decode.h"

#include <cstdio>
#include <cstdlib>

using esphome::gea::decode_ascii_string;
using esphome::gea::is_length_prefixed_string;

static int failures = 0;

static void check(bool cond, const char *what) {
  if (!cond) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

int main() {
  // Length-prefixed model number as observed on a GYE21JYMCFFS (issue #21):
  // 0x0C = 12 = length of "GYE21JYMCFFS".
  std::vector<uint8_t> prefixed = {0x0C, 'G', 'Y', 'E', '2', '1', 'J', 'Y', 'M', 'C', 'F', 'F', 'S'};
  check(is_length_prefixed_string(prefixed), "prefixed model number detected");
  check(decode_ascii_string(prefixed) == "GYE21JYMCFFS", "prefixed model number decoded");

  // Length-prefixed serial number: 0x08 = 8 = length of "LD383631".
  std::vector<uint8_t> prefixed_serial = {0x08, 'L', 'D', '3', '8', '3', '6', '3', '1'};
  check(decode_ascii_string(prefixed_serial) == "LD383631", "prefixed serial number decoded");

  // Length-prefixed AND null-padded to a fixed size, as reported on the
  // GYE21JYMCFFS in PR #25: 19 bytes = prefix + 12 chars + 6 nulls.
  std::vector<uint8_t> prefixed_padded = prefixed;
  prefixed_padded.resize(19, 0x00);
  check(is_length_prefixed_string(prefixed_padded), "prefixed+padded model number detected");
  check(decode_ascii_string(prefixed_padded) == "GYE21JYMCFFS", "prefixed+padded model number decoded");

  // Spec-compliant fixed-size string: null-padded to 32 bytes, no prefix.
  std::vector<uint8_t> padded(32, 0x00);
  const char *model = "PDP715SYV0FS";
  for (size_t i = 0; model[i]; i++)
    padded[i] = (uint8_t) model[i];
  check(!is_length_prefixed_string(padded), "null-padded string not misdetected");
  check(decode_ascii_string(padded) == "PDP715SYV0FS", "null-padded string decoded");

  // Fixed-position multi-field string (Cycle Names 0x301C): starts with a
  // printable character, must pass through unchanged.
  std::vector<uint8_t> cycle_names;
  for (const char *p = "AutoSense Rinse     "; *p; p++)
    cycle_names.push_back((uint8_t) *p);
  check(!is_length_prefixed_string(cycle_names), "fixed-position string not misdetected");
  check(decode_ascii_string(cycle_names) == "AutoSense Rinse     ", "fixed-position string unchanged");

  // Empty string, both conventions.
  std::vector<uint8_t> empty_prefixed = {0x00};
  check(decode_ascii_string(empty_prefixed).empty(), "prefixed empty string decoded as empty");
  std::vector<uint8_t> empty_padded(32, 0x00);
  check(decode_ascii_string(empty_padded).empty(), "null-padded empty string decoded as empty");
  check(decode_ascii_string({}).empty(), "empty payload decoded as empty");

  // Malformed: declared length exceeds the payload — must be left untouched
  // (falls back to the trailing-null trim).
  std::vector<uint8_t> truncated = {0x0C, 'G', 'Y', 'E'};
  check(!is_length_prefixed_string(truncated), "over-long declared length not treated as prefix");
  check(decode_ascii_string(truncated) == std::string("\x0C") + "GYE", "over-long declared length passed through");

  // A control byte that does not match the remaining length is data, not a
  // prefix — a plain `data[0] <= size - 1` test would silently truncate this
  // to "A".
  std::vector<uint8_t> stray_control = {0x01, 'A', 'B'};
  check(!is_length_prefixed_string(stray_control), "non-matching control byte not treated as prefix");
  check(decode_ascii_string(stray_control) == std::string("\x01") + "AB", "non-matching control byte passed through");

  if (failures) {
    fprintf(stderr, "%d test(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  printf("All string_decode tests passed\n");
  return EXIT_SUCCESS;
}
