// Host tests for GEA2 active address discovery in components/gea/gea.cpp
// (engaged when dest_address is omitted).
//
// The real GEAComponent is compiled against stubbed ESPHome headers
// (tests/host/stubs) and its loop() is driven in virtual time, 1 ms per pass,
// against a modelled single-wire bus.  Each scenario decides what the other
// nodes put on the bus after a probe, then checks whether an address was
// adopted, which one, and what was reported.
//
// The bus is a model, not a recording.  The frame shapes come from field
// reports (an empty read response for an ERD a board does not have, a boot
// announcement, a board polling the Wi-Fi module address), but the scenarios
// check the component's decisions, not the behaviour of a real appliance.
//
// Built and run by the host-test job in .github/workflows/test.yml, from the
// repository root:
//   g++ -std=c++17 -Wall -Wextra -Werror -I tests/host/stubs -I components/gea
//       -o test tests/host/test_gea2_addr_discovery.cpp components/gea/gea.cpp && ./test

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
static std::vector<std::string> g_log;  // "<level> <message>", INFO and above plus CONFIG

namespace esphome {
uint32_t millis() { return g_now; }
void delay(uint32_t) {}
void yield() {}
uint32_t random_uint32() { return 4; }
static ESPPreferences g_prefs;
ESPPreferences *global_preferences = &g_prefs;
}  // namespace esphome

void host_log(char level, const char *, const char *fmt, ...) {
  if (level != 'E' && level != 'W' && level != 'I' && level != 'C')
    return;
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  g_log.push_back(std::string(1, level) + " " + buf);
}

// Number of logged lines at `level` containing `text`.
static int logged(char level, const char *text) {
  int n = 0;
  for (const auto &line : g_log) {
    if (line[0] == level && line.find(text) != std::string::npos)
      n++;
  }
  return n;
}

// ------------------------------------------------------------------ bus model
static const uint8_t US = 0xBB, BROADCAST = 0xFF, WIFI_MODULE = 0xBF;
static const uint8_t MAIN = 0x40, AUX = 0x41;
static uint8_t g_us = US;  // the address the hub under test claims

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
  std::vector<std::vector<uint8_t>> sent;  // framed transmissions (a bare ACK byte is not one)
  std::function<void(uint32_t, int)> on_probe;

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
// Every frame we send comes back on RX once it has left the wire.  A probe
// (broadcast read of ERD 0x0008) also lets the scenario schedule the replies.
void host_uart_write(const uint8_t *data, size_t len) {
  if (len <= 1)
    return;
  std::vector<uint8_t> f(data, data + len);
  uint32_t end = g_now + wire_ms(f.size());
  g_bus->deliver_at(end, f);
  g_bus->sent.push_back(f);
  if (f == frame(BROADCAST, g_us, {0xF0, 0x01, 0x00, 0x08}) && g_bus->on_probe) {
    int n = 0;
    for (const auto &s : g_bus->sent)
      n += s == f;
    g_bus->on_probe(end, n);
  }
}

// ----------------------------------------------------------------- bus frames
// Full read response to the probe: appliance type, one byte.
static std::vector<uint8_t> answer(uint8_t src, uint8_t appliance_type) {
  return frame(US, src, {0xF0, 0x01, 0x00, 0x08, 0x01, appliance_type});
}
// What a board that serves ERDs returns for one it does not have: no ERD.
static std::vector<uint8_t> empty_response(uint8_t src) { return frame(US, src, {0xF0, 0x00}); }
// A read request for the probe ERD sent by another node.  Same command and
// same ERD bytes as a reply to the probe, but nothing was answered.
static std::vector<uint8_t> request_for_probe_erd(uint8_t dest, uint8_t src) {
  return frame(dest, src, {0xF0, 0x01, 0x00, 0x08});
}
// The 6-byte broadcast a secondary board sends at power-up.
static std::vector<uint8_t> boot_announce(uint8_t src) { return frame(BROADCAST, src, {0x01}); }

// ------------------------------------------------------------------ scenarios
struct Outcome {
  bool resolved;
  uint8_t address;
  int probes;
  int polls;  // reads of ERD 0x1234 sent to MAIN once the address is known
};

// Called for each probe: `end` is when it has left the wire, n counts from 1.
using Behaviour = std::function<void(Bus &, uint32_t, int)>;

static Outcome play(const Behaviour &behave, uint32_t duration, uint8_t our_address = US) {
  g_now = 0;
  g_log.clear();
  g_us = our_address;
  Bus bus;
  g_bus = &bus;
  bus.on_probe = [&](uint32_t end, int n) { behave(bus, end, n); };

  GEAComponent hub;
  hub.set_protocol(Protocol::GEA2);
  hub.set_src_address(our_address);
  hub.setup();

  bool read_queued = false;
  for (g_now = 0; g_now < duration; g_now++) {
    bus.tick();
    hub.loop();
    if (hub.is_address_resolved() && !read_queued) {
      hub.read_erd(0x1234);
      read_queued = true;
    }
  }
  hub.dump_config();
  g_bus = nullptr;

  int probes = 0, polls = 0;
  for (const auto &f : bus.sent) {
    probes += f == frame(BROADCAST, our_address, {0xF0, 0x01, 0x00, 0x08});
    polls += f == frame(MAIN, our_address, {0xF0, 0x01, 0x12, 0x34});
  }
  return {hub.is_address_resolved(), hub.get_dest_address(), probes, polls};
}

static int failures = 0;
static const char *g_scenario = "";

static void check(bool cond, const char *what) {
  if (!cond) {
    fprintf(stderr, "FAIL: %s: %s\n", g_scenario, what);
    failures++;
  }
}

static Outcome scenario(const char *name, const Behaviour &behave, uint32_t duration = 4000, uint8_t our_address = US) {
  g_scenario = name;
  Outcome o = play(behave, duration, our_address);
  if (o.resolved) {
    printf("%-58s adopted 0x%02X | probes %2d\n", name, o.address, o.probes);
  } else {
    printf("%-58s unresolved   | probes %2d\n", name, o.probes);
  }
  return o;
}

int main() {
  // One board returns the appliance type.
  Outcome o = scenario("one node answers", [](Bus &bus, uint32_t end, int) { bus.deliver_at(end + 20, answer(MAIN, 0x06)); });
  check(o.resolved && o.address == MAIN, "the answering node is adopted");
  check(o.probes == 1, "one probe is enough");
  check(o.polls >= 1, "reads go to the adopted address");
  check(logged('I', "appliance found at 0x40 (appliance type 0x06") == 1, "adoption logged with the appliance type");
  check(logged('W', "") == 0, "nothing reported as a problem");

  // The answer only comes on the third probe (appliance still booting).
  o = scenario(
      "answer on the third probe",
      [](Bus &bus, uint32_t end, int n) {
        if (n == 3)
          bus.deliver_at(end + 20, answer(MAIN, 0x06));
      },
      6000);
  check(o.resolved && o.address == MAIN, "adopted once it answers");
  check(o.probes == 3, "three probes sent");
  check(logged('W', "nothing heard on the bus") == 2, "the two silent rounds are reported");

  // The main board answers while a secondary board is busy with other things:
  // a boot announcement, and its own read of the same ERD.
  o = scenario("one node answers, another one talks", [](Bus &bus, uint32_t end, int) {
    bus.deliver_at(end + 20, answer(MAIN, 0x06));
    bus.deliver_at(end + 60, boot_announce(AUX));
    bus.deliver_at(end + 100, request_for_probe_erd(BROADCAST, AUX));
  });
  check(o.resolved && o.address == MAIN, "only the node that answered is adopted");
  check(logged('I', "0x41 — did not reply to the probe") == 1, "the other node is listed");

  // Another node sends read requests for the probe ERD, to everyone and to us,
  // and nobody answers ours.  A request is not an answer.
  o = scenario("another node's request for the probe ERD", [](Bus &bus, uint32_t end, int) {
    bus.deliver_at(end + 20, request_for_probe_erd(BROADCAST, AUX));
    bus.deliver_at(end + 60, request_for_probe_erd(US, AUX));
  });
  check(!o.resolved, "nothing adopted");
  check(logged('W', "no node answered the probe") == 1, "reported once while nothing changes");
  check(logged('W', "0x41 — did not reply to the probe (last frame: cmd 0xF0 to 0xBB)") == 1, "node listed");

  // The control board has no appliance-type ERD and says so with an empty read
  // response; a secondary board announces itself at power-up.
  o = scenario("control board without the probe ERD", [](Bus &bus, uint32_t end, int n) {
    bus.deliver_at(end + 20, empty_response(0x45));
    if (n == 1)
      bus.deliver_at(end + 60, boot_announce(AUX));
  });
  check(!o.resolved, "not adopted on an empty response alone");
  check(logged('W', "0x45 — replied to the probe, but has no ERD 0x0008") == 1, "the refusal is reported");
  check(logged('W', "0x41 — did not reply to the probe (last frame: cmd 0x01 to 0xFF)") == 1, "announce reported");
  check(logged('W', "If 0x45 is the control board, set 'dest_address: 0x45'") == 1, "the candidate is named");
  check(logged('C', "0x45 — replied to the probe") == 1, "dump_config repeats the node list");
  check(o.probes >= 2, "probing continues");

  // Two boards both return the appliance type.
  o = scenario("two nodes answer", [](Bus &bus, uint32_t end, int) {
    bus.deliver_at(end + 20, answer(MAIN, 0x06));
    bus.deliver_at(end + 60, answer(AUX, 0x06));
  });
  check(!o.resolved, "nothing adopted");
  check(logged('W', "2 nodes answered the probe") == 1, "reported once while nothing changes");
  check(logged('W', "0x40 — answered the probe") == 1 && logged('W', "0x41 — answered the probe") == 1, "both listed");
  check(o.probes >= 2, "probing continues");

  // As above, but the second board only answers a later probe.  The first
  // answer has been adopted by then; this pins that behaviour down.
  o = scenario("second node answers a later probe", [](Bus &bus, uint32_t end, int n) {
    bus.deliver_at(end + 20, answer(n == 1 ? MAIN : AUX, 0x06));
  });
  check(o.resolved && o.address == MAIN, "the first round's single answer is adopted");
  check(o.probes == 1, "no probe after adoption");

  // The control board polls the Wi-Fi module address and ignores our probe.
  // The frames are not for us, but they show who is on the bus.
  o = scenario("traffic between other nodes only", [](Bus &bus, uint32_t end, int) {
    bus.deliver_at(end + 20, frame(WIFI_MODULE, MAIN, {0xF0, 0x01, 0x60, 0x03}));
  });
  check(!o.resolved, "nothing adopted");
  check(logged('W', "0x40 — did not reply to the probe (last frame: cmd 0xF0 to 0xBF)") == 1, "node listed");

  // Same bus, but we sit at the Wi-Fi module address ourselves, so the board's
  // requests are addressed to us.  They are requests, not replies to the probe.
  o = scenario(
      "requests addressed to us",
      [](Bus &bus, uint32_t end, int) {
        bus.deliver_at(end + 20, frame(WIFI_MODULE, MAIN, {0xF0, 0x01, 0x00, 0x08}));
        bus.deliver_at(end + 60, frame(WIFI_MODULE, MAIN, {0xF2, 0x01, 0x00, 0x08, 0x00}));
        bus.deliver_at(end + 100, frame(WIFI_MODULE, MAIN, {0x01}));
      },
      4000, WIFI_MODULE);
  check(!o.resolved, "nothing adopted");
  check(logged('W', "0x40 — did not reply to the probe (last frame: cmd 0x01 to 0xBF)") == 1, "node listed");

  // A frame with a bad CRC between two other nodes names nobody.
  o = scenario("damaged frame between other nodes", [](Bus &bus, uint32_t end, int) {
    auto f = frame(WIFI_MODULE, MAIN, {0xF0, 0x01, 0x60, 0x03});
    f[5] ^= 0x10;
    bus.deliver_at(end + 20, f);
  });
  check(!o.resolved, "nothing adopted");
  check(logged('W', "nothing heard on the bus") >= 1 && logged('W', "0x40") == 0, "no node listed");

  // Nothing on the bus at all.  The warning is throttled after the first rounds.
  o = scenario("silent bus", [](Bus &, uint32_t, int) {}, 60000);
  check(!o.resolved, "nothing adopted");
  check(o.probes >= 35, "probing continues");
  check(logged('W', "nothing heard on the bus") == 6, "warned for the first 5 rounds, then every 30th");

  // More nodes than the table holds, then one of them answers: the answer is
  // not lost to the table limit.
  o = scenario("more nodes than the table holds", [](Bus &bus, uint32_t end, int n) {
    if (n == 1) {
      for (uint8_t i = 0; i < 10; i++)
        bus.deliver_at(end + 20 + 20 * i, boot_announce(0x50 + i));
    } else if (n == 2) {
      bus.deliver_at(end + 20, answer(MAIN, 0x06));
    }
  });
  check(o.resolved && o.address == MAIN, "the answering node is adopted");

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all GEA2 address discovery checks passed\n");
  return 0;
}
