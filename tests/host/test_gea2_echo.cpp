// Host tests for the GEA2 TX echo settlement and collision handling in
// components/gea/gea.cpp.
//
// The real GEAComponent is compiled against stubbed ESPHome headers
// (tests/host/stubs) and its loop() is driven in virtual time, 1 ms per pass,
// against a modelled single-wire bus.  Each scenario plays one ERD read and
// checks how it went: frames sent, collisions counted, retries, time to the
// decoded response, and what was logged.
//
// The bus is a model, not a recording: a scenario decides which bytes reach RX
// and when.  It checks the component's decisions, not the electrical behaviour
// of a real bus.
//
// Built and run by the host-test job in .github/workflows/test.yml, from the
// repository root:
//   g++ -std=c++17 -Wall -Wextra -Werror -I tests/host/stubs -I components/gea
//       -o test tests/host/test_gea2_echo.cpp components/gea/gea.cpp && ./test

#include "gea.h"
#include "esphome/core/hal.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

using esphome::gea::GEAComponent;
using esphome::gea::Protocol;

// ---------------------------------------------------------------- clock / log
static uint32_t g_now = 0;
static int32_t g_read_ok_at = -1;
static int g_echo_missing_warnings = 0;
static int g_debug_frame_starts = 0;

namespace esphome {
uint32_t millis() { return g_now; }
void delay(uint32_t) {}
void yield() {}
// Deterministic backoff: the scenarios assert on counts and time bounds.
static uint32_t g_rng = 12345;
uint32_t random_uint32() {
  g_rng = g_rng * 1664525u + 1013904223u;
  return g_rng >> 8;
}
static ESPPreferences g_prefs;
ESPPreferences *global_preferences = &g_prefs;
}  // namespace esphome

void host_log(char level, const char *, const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (level == 'W' && strstr(buf, "TX echo missing") != nullptr)
    g_echo_missing_warnings++;
  if (level == 'D' && strstr(buf, "frame started") != nullptr)
    g_debug_frame_starts++;
  if (g_read_ok_at < 0 && strstr(buf, "Read ERD 0x1234 OK") != nullptr)
    g_read_ok_at = (int32_t) g_now;
}

// ------------------------------------------------------------------ bus model
static const uint8_t US = 0xBB, APPLIANCE = 0xC0, OTHER_A = 0xA5, OTHER_B = 0x80;

static uint16_t crc16(const std::vector<uint8_t> &d) {
  uint16_t crc = 0x1021;
  for (uint8_t b : d) {
    crc ^= (uint16_t) b << 8;
    for (int j = 0; j < 8; j++)
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
  }
  return crc;
}

// Wire image of a frame: STX, escaped [DEST][LEN][SRC][payload][CRC], ETX.
static std::vector<uint8_t> frame(uint8_t dest, uint8_t src, const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> inner = {dest, (uint8_t) (7 + payload.size()), src};
  inner.insert(inner.end(), payload.begin(), payload.end());
  uint16_t crc = crc16(inner);
  inner.push_back(crc >> 8);
  inner.push_back(crc & 0xFF);
  std::vector<uint8_t> out = {0xE2};
  for (uint8_t b : inner) {
    if (b >= 0xE0 && b <= 0xE3)
      out.push_back(0xE0);
    out.push_back(b);
  }
  out.push_back(0xE3);
  return out;
}

// Milliseconds a run of bytes occupies the wire at 19200 baud (0.52 ms/byte).
static uint32_t wire_ms(size_t n) { return (uint32_t) ((n * 52 + 99) / 100); }

struct Bus {
  std::multimap<uint32_t, std::vector<uint8_t>> scheduled;  // RX deliveries, by time
  std::deque<uint8_t> rx;
  int frames_sent = 0;  // framed transmissions (a bare ACK byte is not one)
  std::function<void(const std::vector<uint8_t> &, int)> on_frame;

  void deliver_at(uint32_t t, std::vector<uint8_t> bytes) { scheduled.emplace(t, std::move(bytes)); }
  void tick() {
    while (!scheduled.empty() && scheduled.begin()->first <= g_now) {
      for (uint8_t b : scheduled.begin()->second)
        rx.push_back(b);
      scheduled.erase(scheduled.begin());
    }
  }
};
static Bus *g_bus = nullptr;

size_t host_uart_available() { return g_bus->rx.size(); }
bool host_uart_read(uint8_t *b) {
  if (g_bus->rx.empty())
    return false;
  *b = g_bus->rx.front();
  g_bus->rx.pop_front();
  return true;
}
void host_uart_write(const uint8_t *data, size_t len) {
  if (len <= 1)
    return;
  g_bus->frames_sent++;
  if (g_bus->on_frame)
    g_bus->on_frame(std::vector<uint8_t>(data, data + len), g_bus->frames_sent);
}

// Our frame comes back on RX once it has left the wire.  Returns that time.
static uint32_t echo_returns(Bus &bus, const std::vector<uint8_t> &f) {
  uint32_t end = g_now + wire_ms(f.size());
  bus.deliver_at(end, f);
  return end;
}

// The appliance's answer to a read of ERD 0x1234: ACK, then the response frame.
static void appliance_answers(Bus &bus, uint32_t request_end) {
  bus.deliver_at(request_end + 2, {0xE1});
  auto resp = frame(US, APPLIANCE, {0xF0, 0x01, 0x12, 0x34, 0x02, 0xAB, 0xCD});
  bus.deliver_at(request_end + 12 + wire_ms(resp.size()), resp);
}

// What two overlapping transmissions leave of ours: the dominant bits of the
// other frame win from byte 2 on.  `keep_etx` leaves the frame end readable.
static std::vector<uint8_t> garbled(const std::vector<uint8_t> &f, bool keep_etx) {
  auto g = f;
  for (size_t i = 2; i + (keep_etx ? 2 : 0) < g.size(); i++)
    g[i] &= 0x5A;
  return g;
}

static std::vector<uint8_t> other_nodes_frame() { return frame(OTHER_A, OTHER_B, {0xF0, 0x01, 0x20, 0x07}); }

// ------------------------------------------------------------------ scenarios
struct Outcome {
  int32_t answered_ms;  // time from the read being queued to its decoded response; -1 if never
  int frames_sent;
  uint32_t collisions;
  uint32_t retries;
  uint32_t dropped;
  uint32_t crc_errors;
  int echo_missing_warnings;
  int debug_frame_starts;
};

// Called for each framed transmission (n = 1 for the first attempt).
using Behaviour = std::function<void(Bus &, const std::vector<uint8_t> &, int)>;

static Outcome play(const Behaviour &behave) {
  g_now = 0;
  g_read_ok_at = -1;
  g_echo_missing_warnings = 0;
  g_debug_frame_starts = 0;
  Bus bus;
  g_bus = &bus;
  bus.on_frame = [&](const std::vector<uint8_t> &f, int n) { behave(bus, f, n); };

  GEAComponent hub;
  hub.set_protocol(Protocol::GEA2);
  hub.set_src_address(US);
  hub.set_dest_address(APPLIANCE);
  hub.setup();

  // Long enough for the 10 s stats report, which carries the echo warning.
  const uint32_t start = 100, duration = 12000;
  for (g_now = 0; g_now < start + duration; g_now++) {
    bus.tick();
    if (g_now == start)
      hub.read_erd(0x1234);
    hub.loop();
  }
  g_bus = nullptr;

  return {g_read_ok_at < 0 ? -1 : g_read_ok_at - (int32_t) start,
          bus.frames_sent,
          hub.get_tx_collisions(),
          hub.get_tx_retries(),
          hub.get_dropped_requests(),
          hub.get_crc_errors(),
          g_echo_missing_warnings,
          g_debug_frame_starts};
}

static int failures = 0;
static const char *g_scenario = "";

static void check(bool cond, const char *what) {
  if (!cond) {
    fprintf(stderr, "FAIL: %s: %s\n", g_scenario, what);
    failures++;
  }
}

static Outcome scenario(const char *name, const Behaviour &behave) {
  g_scenario = name;
  Outcome o = play(behave);
  printf("%-52s answered %4d ms | frames %2d | collisions %u | retries %2u | dropped %u | echo warnings %d\n", name,
         o.answered_ms, o.frames_sent, o.collisions, o.retries, o.dropped, o.echo_missing_warnings);
  return o;
}

// The regular request timeout.  A collision that is detected is retried well
// inside it; one that goes unnoticed waits it out.
static const int32_t REQUEST_TIMEOUT_MS = 250;

int main() {
  // Echo returns, appliance answers.
  Outcome o = scenario("clean exchange", [](Bus &bus, const std::vector<uint8_t> &f, int) {
    appliance_answers(bus, echo_returns(bus, f));
  });
  check(o.answered_ms >= 0, "read answered");
  check(o.frames_sent == 1, "one frame sent");
  check(o.collisions == 0 && o.retries == 0, "no collision, no retry");
  check(o.echo_missing_warnings == 0, "no echo warning");
  check(o.crc_errors == 0, "no CRC error");
  // The echo's frame start stays out of the DEBUG log; the reply's is there.
  check(o.debug_frame_starts == 1, "only the reply's frame start logged at DEBUG");

  // An intact frame between two other nodes ended just before we transmitted
  // and is read ahead of our echo.  Nothing overlapped on the wire.
  o = scenario("another node's frame read before our echo", [](Bus &bus, const std::vector<uint8_t> &f, int n) {
    if (n == 1)
      bus.deliver_at(g_now, other_nodes_frame());
    appliance_answers(bus, echo_returns(bus, f));
  });
  check(o.answered_ms >= 0, "read answered");
  check(o.frames_sent == 1, "one frame sent");
  check(o.collisions == 0 && o.retries == 0, "no collision, no retry");
  check(o.echo_missing_warnings == 0, "no echo warning");

  // A bare ACK byte from another exchange is read ahead of our echo.
  o = scenario("another node's ACK read before our echo", [](Bus &bus, const std::vector<uint8_t> &f, int n) {
    if (n == 1)
      bus.deliver_at(g_now, {0xE1});
    appliance_answers(bus, echo_returns(bus, f));
  });
  check(o.answered_ms >= 0, "read answered");
  check(o.frames_sent == 1, "one frame sent");
  check(o.collisions == 0 && o.retries == 0, "no collision, no retry");

  // Our STX lands on a frame another node had started and then gave up.  The
  // parser restarts on it and our echo follows intact: the transmission got
  // through, so the dropped fragment must not be read as a collision.
  o = scenario("our frame restarts the parser, echo intact", [](Bus &bus, const std::vector<uint8_t> &f, int n) {
    if (n == 1)
      bus.deliver_at(g_now, {0xE2, OTHER_A, 0x0B, OTHER_B});
    appliance_answers(bus, echo_returns(bus, f));
  });
  check(o.answered_ms >= 0, "read answered");
  check(o.frames_sent == 1, "one frame sent");
  check(o.collisions == 0 && o.retries == 0, "no collision, no retry");
  check(o.echo_missing_warnings == 0, "no echo warning");

  // Another node talks over our first attempt: the echo returns garbled
  // between STX and ETX and the appliance does not answer.  The retry is clean.
  o = scenario("collision, frame end intact", [](Bus &bus, const std::vector<uint8_t> &f, int n) {
    if (n == 1) {
      bus.deliver_at(g_now + wire_ms(f.size()), garbled(f, true));
      return;
    }
    appliance_answers(bus, echo_returns(bus, f));
  });
  check(o.collisions == 1, "one collision counted");
  check(o.frames_sent == 2 && o.retries == 1, "one retry");
  check(o.answered_ms >= 0 && o.answered_ms < 100, "retried right after the damaged frame");
  check(o.echo_missing_warnings == 0, "no echo warning");
  check(o.crc_errors == 0, "the wreckage is not reported as a CRC error");

  // As above, but the overlap also wipes out our ETX: the garbled echo never
  // terminates and is given up after the truncated-frame timeout.
  o = scenario("collision, ETX destroyed", [](Bus &bus, const std::vector<uint8_t> &f, int n) {
    if (n == 1) {
      bus.deliver_at(g_now + wire_ms(f.size()), garbled(f, false));
      return;
    }
    appliance_answers(bus, echo_returns(bus, f));
  });
  check(o.collisions == 1, "one collision counted");
  check(o.frames_sent == 2 && o.retries == 1, "one retry");
  check(o.answered_ms >= 0 && o.answered_ms < REQUEST_TIMEOUT_MS, "retried before the request timeout");
  check(o.echo_missing_warnings == 0, "no echo warning");

  // As above, and another node's intact frame follows inside the truncated-
  // frame window: the parser restarts on its STX, so that timeout never fires.
  // The echo window settles it as a collision, not as a missing echo.
  o = scenario("collision, ETX destroyed, another frame follows", [](Bus &bus, const std::vector<uint8_t> &f, int n) {
    if (n == 1) {
      uint32_t end = g_now + wire_ms(f.size());
      bus.deliver_at(end, garbled(f, false));
      auto other = other_nodes_frame();
      bus.deliver_at(end + 15 + wire_ms(other.size()), other);
      return;
    }
    appliance_answers(bus, echo_returns(bus, f));
  });
  check(o.collisions == 1, "one collision counted");
  check(o.frames_sent == 2 && o.retries == 1, "one retry");
  check(o.answered_ms >= 0 && o.answered_ms < REQUEST_TIMEOUT_MS, "retried before the request timeout");
  check(o.echo_missing_warnings == 0, "not reported as a missing echo");

  // RX never hears our own transmission; the appliance answers normally.
  // Unsupported wiring: the reply still decodes, and the echo is reported.
  o = scenario("no echo returned", [](Bus &bus, const std::vector<uint8_t> &f, int) {
    appliance_answers(bus, g_now + wire_ms(f.size()));
  });
  check(o.answered_ms >= 0, "read answered");
  check(o.frames_sent == 1, "one frame sent");
  check(o.collisions == 0 && o.retries == 0, "no collision, no retry");
  check(o.echo_missing_warnings == 1, "missing echo reported once with the stats");

  // Echo returns but nothing answers (appliance off): plain timeouts.
  o = scenario("echo returns, appliance silent",
               [](Bus &bus, const std::vector<uint8_t> &f, int) { echo_returns(bus, f); });
  check(o.answered_ms < 0, "read never answered");
  check(o.frames_sent == 11 && o.retries == 10, "all retries used");
  check(o.dropped == 1, "request dropped");
  check(o.collisions == 0, "no collision");
  check(o.echo_missing_warnings == 0, "no echo warning");

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all GEA2 echo checks passed\n");
  return 0;
}
