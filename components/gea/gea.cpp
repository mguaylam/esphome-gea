#include "gea.h"
#ifdef GEA_ERD_LOOKUP
#include "erd_table.h"
#endif
#ifdef GEA_GEA2_DISCOVERY
#include "gea2_discovery_table.h"
#endif
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace gea {

static const char *const TAG = "gea";

#ifdef GEA_ERD_LOOKUP
static std::string decode_erd_value(const std::vector<uint8_t> &data, const char *type_str);
#endif

// =============================================================================
// GEAEntity helpers
// =============================================================================

float GEAEntity::decode_as_float(const std::vector<uint8_t> &data) const {
  if (data.size() <= (size_t)byte_offset_)
    return 0.0f;
  const uint8_t *d = data.data() + byte_offset_;
  size_t rem = data.size() - byte_offset_;
  float raw = 0.0f;

  switch (decode_) {
    case GeaDecodeType::UINT8: raw = (rem >= 1) ? (float)d[0] : 0.0f; break;
    case GeaDecodeType::UINT16_BE: raw = (rem >= 2) ? (float)((uint16_t)d[0] << 8 | d[1]) : 0.0f; break;
    case GeaDecodeType::UINT16_LE: raw = (rem >= 2) ? (float)((uint16_t)d[1] << 8 | d[0]) : 0.0f; break;
    case GeaDecodeType::UINT32_BE:
      if (rem >= 4)
        raw = (float)((uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3]);
      else if (rem == 3)
        raw = (float)((uint32_t)d[0] << 16 | (uint32_t)d[1] << 8 | d[2]);
      break;
    case GeaDecodeType::UINT32_LE:
      raw = (rem >= 4) ? (float)((uint32_t)d[3] << 24 | (uint32_t)d[2] << 16 | (uint32_t)d[1] << 8 | d[0]) : 0.0f;
      break;
    case GeaDecodeType::INT8: raw = (rem >= 1) ? (float)(int8_t)d[0] : 0.0f; break;
    case GeaDecodeType::INT16_BE: raw = (rem >= 2) ? (float)(int16_t)((uint16_t)d[0] << 8 | d[1]) : 0.0f; break;
    case GeaDecodeType::INT16_LE: raw = (rem >= 2) ? (float)(int16_t)((uint16_t)d[1] << 8 | d[0]) : 0.0f; break;
    case GeaDecodeType::INT32_BE:
      raw = (rem >= 4) ? (float)(int32_t)((uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3])
                       : 0.0f;
      break;
    case GeaDecodeType::INT32_LE:
      raw = (rem >= 4) ? (float)(int32_t)((uint32_t)d[3] << 24 | (uint32_t)d[2] << 16 | (uint32_t)d[1] << 8 | d[0])
                       : 0.0f;
      break;
    case GeaDecodeType::BOOL:
      // BOOL is unscaled — masked bit, returned as 0 or 1.
      return (rem >= 1) ? ((d[0] & bitmask_) ? 1.0f : 0.0f) : 0.0f;
    default: return 0.0f;
  }
  return raw * multiplier_ + offset_;
}

std::string GEAEntity::decode_as_hex(const std::vector<uint8_t> &data) const {
  if (data.empty())
    return "0x";
  std::string result = "0x";
  char hex[3];
  for (uint8_t b : data) {
    snprintf(hex, sizeof(hex), "%02X", b);
    result += hex;
  }
  return result;
}

void GEAEntity::encode_to_bytes(uint32_t val, std::vector<uint8_t> &out) const {
  uint8_t n = data_size_;
  if (n == 0) {
    switch (decode_) {
      case GeaDecodeType::UINT16_BE:
      case GeaDecodeType::INT16_BE:
      case GeaDecodeType::UINT16_LE:
      case GeaDecodeType::INT16_LE: n = 2; break;
      case GeaDecodeType::UINT32_BE:
      case GeaDecodeType::INT32_BE:
      case GeaDecodeType::UINT32_LE:
      case GeaDecodeType::INT32_LE: n = 4; break;
      default: n = 1; break;
    }
  }
  bool le = false;
  switch (decode_) {
    case GeaDecodeType::UINT16_LE:
    case GeaDecodeType::INT16_LE:
    case GeaDecodeType::UINT32_LE:
    case GeaDecodeType::INT32_LE: le = true; break;
    default: break;
  }
  if (le) {
    for (uint8_t i = 0; i < n; i++)
      out.push_back((val >> (i * 8)) & 0xFF);
  } else {
    for (int i = (int)n - 1; i >= 0; i--)
      out.push_back((val >> (i * 8)) & 0xFF);
  }
}

// =============================================================================
// GEAComponent — protocol utilities
// =============================================================================

// CRC-16/CCITT: polynomial 0x1021, seed 0x1021
// Covers [DEST][LEN][SRC][PAYLOAD...] (everything between STX and CRC bytes).
// CRC-16/CCITT, polynomial 0x1021, seed 0x1021, MSB-first output.
uint16_t GEAComponent::crc16_(const uint8_t *data, size_t len) {
  uint16_t crc = GEA_CRC_SEED;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int j = 0; j < 8; j++) {
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
    }
  }
  return crc;
}

// Escape control bytes {0xE0, 0xE1, 0xE2, 0xE3}: prefix with GEA_ESC (0xE0),
// then send the original byte unchanged.  The receiver's state machine
// distinguishes escaped data from real control bytes.
//   0xE0 → 0xE0 0xE0
//   0xE1 → 0xE0 0xE1
//   0xE2 → 0xE0 0xE2
//   0xE3 → 0xE0 0xE3
std::vector<uint8_t> GEAComponent::escape_(const std::vector<uint8_t> &raw) {
  std::vector<uint8_t> out;
  out.reserve(raw.size() + 4);
  for (uint8_t b : raw) {
    if (b >= 0xE0 && b <= 0xE3) {
      out.push_back(GEA_ESC);
      out.push_back(b);  // receiver state machine handles unescape
    } else {
      out.push_back(b);
    }
  }
  return out;
}

// =============================================================================
// GEAComponent — TX helpers
// =============================================================================

// Build and transmit a framed GEA3 packet.
//
// Wire format (pre-escape):
//   [STX=0xE2] [DEST] [LEN] [SRC] [payload...] [CRC_MSB] [CRC_LSB] [ETX=0xE3]
//
// LEN = total logical packet length (1+1+1+1+payload+2+1 = 7+payload).
// CRC is computed over [DEST][LEN][SRC][payload...] before escaping.
// Everything between STX and ETX is then escape-processed.
void GEAComponent::send_packet_(uint8_t dest, const std::vector<uint8_t> &payload) {
  uint8_t len = (uint8_t)(7 + payload.size());

  // Build the inner portion (subject to CRC and escaping)
  std::vector<uint8_t> inner;
  inner.reserve(4 + payload.size() + 2);
  inner.push_back(dest);
  inner.push_back(len);
  inner.push_back(src_addr_);
  inner.insert(inner.end(), payload.begin(), payload.end());

  uint16_t crc = crc16_(inner.data(), inner.size());
  inner.push_back(crc >> 8);    // CRC MSB (sent first, per GEA3 spec)
  inner.push_back(crc & 0xFF);  // CRC LSB

  auto escaped = escape_(inner);

  // Assemble the exact wire image (STX + escaped body + ETX) in one buffer so
  // GEA2 can verify the bus echo against it byte-for-byte.
  std::vector<uint8_t> frame;
  frame.reserve(escaped.size() + 2);
  frame.push_back(GEA_STX);
  frame.insert(frame.end(), escaped.begin(), escaped.end());
  frame.push_back(GEA_ETX);
  write_array(frame.data(), frame.size());

  if (protocol_ == Protocol::GEA2) {
    // Single-wire bus: everything we send comes back on RX.  Keep the wire
    // image so loop() can match the echo as it arrives — a mismatch means
    // another node drove the line at the same time (collision).
    gea2_echo_buf_ = std::move(frame);
    gea2_echo_idx_ = 0;
    gea2_echo_at_ms_ = millis();
  }
}

uint8_t GEAComponent::next_req_id_() {
  uint8_t id = req_id_++;
  if (req_id_ == 0)
    req_id_ = 1;  // keep non-zero
  return id;
}

// Send a bare ACK byte (no framing — GEA3 ACK is a single 0xE1 on the wire).
void GEAComponent::send_ack_() {
  write_byte(GEA_ACK);
}

// Send a framed publication acknowledgement: [CMD_PUB_ACK][context][request_id].
// The context and request_id must echo the values from the publication header.
void GEAComponent::send_pub_ack_(uint8_t context, uint8_t request_id) {
  std::vector<uint8_t> payload = {CMD_PUB_ACK, context, request_id};
  send_packet_(dest_addr_, payload);
}

// Trigger ERD discovery: the appliance responds with a publication (0xA6) for
// every ERD it supports.  We log each one and also route values to any
// user-configured entities that match.
//
// type: 0x00 = add subscription, 0x01 = retain (keep-alive).
// Skips if a subscribe-all is already in flight or queued, since pacing is
// handled by the outer heartbeat in loop().
void GEAComponent::send_subscribe_all_(uint8_t type) {
  if (has_inflight_cmd_(CMD_SUB_ALL_REQUEST)) {
    ESP_LOGV(TAG, "Subscribe-all already in flight — skipping");
    return;
  }
  std::vector<uint8_t> body = {type};
  enqueue_request_(CMD_SUB_ALL_REQUEST, std::move(body));
  ESP_LOGD(TAG, "Queued subscribe-all request (type=%u)", type);
}

// =============================================================================
// GEAComponent — request queue and retry machinery
// =============================================================================

void GEAComponent::enqueue_request_(uint8_t cmd, std::vector<uint8_t> body) {
  PendingRequest req;
  req.cmd = cmd;
  req.req_id = next_req_id_();
  req.dest = dest_addr_;
  req.body = std::move(body);
  req.retries_left = REQUEST_MAX_RETRIES;
  req.sent_at_ms = 0;
  request_queue_.push_back(std::move(req));
}

bool GEAComponent::has_inflight_cmd_(uint8_t cmd) const {
  if (pending_active_ && pending_.cmd == cmd)
    return true;
  for (const auto &r : request_queue_) {
    if (r.cmd == cmd)
      return true;
  }
  return false;
}

// Build and send the currently-pending request on the wire.  Called on first
// send and on every retry — the same req_id is reused (GEA3) so a late
// response from a prior attempt still matches. GEA2 has no req_id field.
void GEAComponent::transmit_pending_() {
  std::vector<uint8_t> payload;
  payload.reserve(2 + pending_.body.size());
  payload.push_back(pending_.cmd);
  if (protocol_ == Protocol::GEA3)
    payload.push_back(pending_.req_id);
  payload.insert(payload.end(), pending_.body.begin(), pending_.body.end());
  send_packet_(pending_.dest, payload);
  pending_.sent_at_ms = millis();
}

void GEAComponent::finish_pending_() {
  pending_active_ = false;
  pending_.body.clear();
  pending_.is_discovery = false;
}

// True when a GEA2 transmission may start: no frame is mid-reception, the
// echo of our previous transmission has fully returned, and the line has been
// silent for a few byte-times.  Immediate protocol responses (ACK) are exempt
// — the peer expects them right after its frame.  GEA3 runs on a dedicated
// full-duplex link, so there is no one to yield to.
bool GEAComponent::gea2_bus_clear_() const {
  if (protocol_ != Protocol::GEA2)
    return true;
  if (rx_state_ != RxState::IDLE || !gea2_echo_buf_.empty())
    return false;
  return last_rx_byte_ms_ == 0 || millis() - last_rx_byte_ms_ >= GEA2_TX_IDLE_MS;
}

// Match an incoming byte against the wire image of our last GEA2 transmission.
// Returns true when the byte was our own echo (consumed, not fed to the
// parser).  A mismatch means another node transmitted while we did — a
// collision.  Both frames are garbled on the wire, so schedule a fast retry of
// the pending request after a short random backoff (the randomness breaks
// lockstep with the other node's own retry) instead of waiting out the full
// request timeout.
bool GEAComponent::consume_gea2_echo_byte_(uint8_t byte) {
  if (gea2_echo_buf_.empty())
    return false;
  if (byte == gea2_echo_buf_[gea2_echo_idx_]) {
    gea2_echo_idx_++;
    if (gea2_echo_idx_ >= gea2_echo_buf_.size()) {
      ESP_LOGV(TAG, "GEA2 TX echo verified (%zu bytes)", gea2_echo_buf_.size());
      gea2_echo_buf_.clear();
      gea2_echo_idx_ = 0;
    }
    return true;
  }
  tx_collisions_++;
  ESP_LOGD(TAG, "GEA2 collision: echo byte %zu read 0x%02X, sent 0x%02X — scheduling fast retry", gea2_echo_idx_, byte,
           gea2_echo_buf_[gea2_echo_idx_]);
  gea2_echo_buf_.clear();
  gea2_echo_idx_ = 0;
  if (pending_active_) {
    // Backdate the send timestamp so the regular timeout machinery fires
    // after the backoff rather than after the full REQUEST_TIMEOUT_MS.
    uint32_t backoff = GEA2_COLLISION_BACKOFF_MIN_MS + (random_uint32() % GEA2_COLLISION_BACKOFF_SPAN_MS);
    pending_.sent_at_ms = millis() - REQUEST_TIMEOUT_MS + backoff;
  }
  return false;  // garbled byte — let the parser see it and resync on STX
}

// Returns true if an incoming response's command and request ID match the
// request currently in flight.  Used by response handlers to filter stray or
// duplicated packets.
bool GEAComponent::response_matches_pending_(uint8_t response_cmd, uint8_t req_id) const {
  if (!pending_active_)
    return false;
  if (pending_.req_id != req_id)
    return false;
  // Request→response pairing: READ(0xA0)→0xA1, WRITE(0xA2)→0xA3, SUB(0xA4)→0xA5.
  return response_cmd == (pending_.cmd | 0x01);
}

// GEA2 has no request_id field; matching is by command code (request and
// response share the same code in GEA2) and ERD address. Single-in-flight
// makes this unambiguous.
bool GEAComponent::gea2_response_matches_pending_(uint8_t response_cmd, uint16_t erd) const {
  if (!pending_active_)
    return false;
  if (pending_.cmd != response_cmd)
    return false;
  // GEA2 body layout: [count=1][erd_h][erd_l][...]
  if (pending_.body.size() < 3)
    return false;
  uint16_t pending_erd = ((uint16_t)pending_.body[1] << 8) | pending_.body[2];
  return pending_erd == erd;
}

// =============================================================================
// GEAComponent — ESPHome lifecycle
// =============================================================================

void GEAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GEA component (protocol=%s)...", protocol_ == Protocol::GEA2 ? "GEA2" : "GEA3");
  rx_buf_.reserve(64);
  if (protocol_ == Protocol::GEA2) {
    // GEA2 has no subscribe-all and no spontaneous publications, so the GEA3
    // passive auto-detect can't apply here.
    auto_detect_ = false;
    if (!dest_configured_) {
      // No dest_address in YAML — actively probe the bus to find the appliance
      // address before any polling. See drive_gea2_addr_discovery_().
      gea2_addr_discovery_ = true;
      dest_addr_ = GEA_BROADCAST_ADDR;
      ESP_LOGI(TAG, "GEA2 mode — dest_address not set; probing the bus to discover the appliance address");
    } else {
      ESP_LOGI(TAG, "GEA2 mode — values will be polled (interval=%u ms)", poll_interval_ms_);
      last_poll_ms_ = millis();
#ifdef GEA_GEA2_DISCOVERY
      if (gea2_discovery_)
        discovery_init_();
#endif
    }
  } else {
    if (auto_detect_) {
      ESP_LOGI(TAG, "Address auto-detect enabled — sending subscribe-all to broadcast");
      dest_addr_ = GEA_BROADCAST_ADDR;
    }
    send_subscribe_all_(0x00);  // type=add
    sub_retry_ms_ = millis();
  }
}

void GEAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "GEA Component:");
  ESP_LOGCONFIG(TAG, "  Protocol: %s", protocol_ == Protocol::GEA2 ? "GEA2" : "GEA3");
  if (gea2_addr_discovery_) {
    ESP_LOGCONFIG(TAG, "  Dest address: discovering on the bus%s",
                  addr_discovery_halted_ ? " (halted — set dest_address)" : "…");
  } else if (auto_detect_) {
    ESP_LOGCONFIG(TAG, "  Dest address: auto-detect (current: 0x%02X)", dest_addr_);
  } else {
    ESP_LOGCONFIG(TAG, "  Dest address: 0x%02X", dest_addr_);
  }
  ESP_LOGCONFIG(TAG, "  Src address:  0x%02X", src_addr_);
  if (protocol_ == Protocol::GEA2) {
    ESP_LOGCONFIG(TAG, "  Poll interval: %u ms", poll_interval_ms_);
  }
  ESP_LOGCONFIG(TAG, "  Registered entities: %zu", entities_.size());
  for (auto *e : entities_) {
    ESP_LOGCONFIG(TAG, "    ERD 0x%04X", e->get_erd());
  }
#ifdef GEA_GEA2_DISCOVERY
  if (gea2_discovery_) {
    if (discovery_state_ == DiscoveryState::SCANNING) {
      ESP_LOGCONFIG(TAG, "  GEA2 discovery: SCANNING (%zu / %zu, %zu found)", discovery_index_,
                    GEA2_DISCOVERY_TABLE_SIZE, discovery_found_erds_.size());
    } else {
      ESP_LOGCONFIG(TAG, "  GEA2 discovery: DONE — %zu ERDs responded", discovery_found_erds_.size());
      log_discovery_erds_();
    }
    return;
  }
#endif
  if (!discovered_erds_.empty()) {
    ESP_LOGCONFIG(TAG, "  Discovered ERDs (%zu):", discovered_erds_.size());
    for (auto &kv : discovered_erds_) {
      std::string raw;
      char buf[3];
      for (uint8_t b : kv.second) {
        snprintf(buf, sizeof(buf), "%02X", b);
        raw += buf;
      }
#ifdef GEA_ERD_LOOKUP
      const ErdTableEntry *info = erd_lookup(kv.first);
      if (info == nullptr) {
        ESP_LOGCONFIG(TAG, "    0x%04X  (unknown)  raw=%s", kv.first, raw.c_str());
      } else {
        std::string val = decode_erd_value(kv.second, info->type);
        if (val.empty()) {
          ESP_LOGCONFIG(TAG, "    0x%04X  %-40s  [%s]  raw=%s", kv.first, info->name, info->type, raw.c_str());
        } else {
          ESP_LOGCONFIG(TAG, "    0x%04X  %-40s  [%s]  raw=%s  val=%s", kv.first, info->name, info->type, raw.c_str(),
                        val.c_str());
        }
      }
#else
      ESP_LOGCONFIG(TAG, "    0x%04X  (%zuB)  %s", kv.first, kv.second.size(), raw.c_str());
#endif
    }
  }
}

// Decode raw ERD bytes according to a GE type string (e.g. "u8", "u16/u8", "string").
// Returns a human-readable string, or "" for unknown/raw types.
#ifdef GEA_ERD_LOOKUP
static std::string decode_erd_value(const std::vector<uint8_t> &data, const char *type_str) {
  if (type_str == nullptr || type_str[0] == '\0')
    return "";

  std::string result;
  std::string ts(type_str);
  size_t offset = 0;
  size_t pos = 0;

  while (pos <= ts.size()) {
    size_t slash = ts.find('/', pos);
    std::string field = ts.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
    pos = (slash == std::string::npos) ? ts.size() + 1 : slash + 1;

    if (!result.empty())
      result += "/";

    char buf[32];
    if (field == "u8" || field == "enum") {
      if (offset < data.size()) {
        snprintf(buf, sizeof(buf), "%u", data[offset++]);
        result += buf;
      }
    } else if (field == "i8") {
      if (offset < data.size()) {
        snprintf(buf, sizeof(buf), "%d", (int8_t)data[offset++]);
        result += buf;
      }
    } else if (field == "bool") {
      if (offset < data.size())
        result += data[offset++] ? "true" : "false";
    } else if (field == "u16") {
      if (offset + 2 <= data.size()) {
        uint16_t v = ((uint16_t)data[offset] << 8) | data[offset + 1];
        offset += 2;
        snprintf(buf, sizeof(buf), "%u", v);
        result += buf;
      }
    } else if (field == "i16") {
      if (offset + 2 <= data.size()) {
        int16_t v = (int16_t)(((uint16_t)data[offset] << 8) | data[offset + 1]);
        offset += 2;
        snprintf(buf, sizeof(buf), "%d", v);
        result += buf;
      }
    } else if (field == "u32") {
      if (offset + 4 <= data.size()) {
        uint32_t v = ((uint32_t)data[offset] << 24) | ((uint32_t)data[offset + 1] << 16) |
                     ((uint32_t)data[offset + 2] << 8) | data[offset + 3];
        offset += 4;
        snprintf(buf, sizeof(buf), "%u", v);
        result += buf;
      }
    } else if (field == "i32") {
      if (offset + 4 <= data.size()) {
        int32_t v = (int32_t)(((uint32_t)data[offset] << 24) | ((uint32_t)data[offset + 1] << 16) |
                              ((uint32_t)data[offset + 2] << 8) | data[offset + 3]);
        offset += 4;
        snprintf(buf, sizeof(buf), "%d", v);
        result += buf;
      }
    } else if (field == "string") {
      result += '"';
      while (offset < data.size() && data[offset] != 0)
        result += (char)data[offset++];
      result += '"';
    } else {
      // raw or unknown — skip remainder
      result += "raw";
      break;
    }
  }
  return result;
}
#endif  // GEA_ERD_LOOKUP

// Log the appliance address and how it was determined. The address is also
// printed once at boot, but that scrolls away before a network client connects —
// wire this to api: on_client_connected to see it on demand.
void GEAComponent::log_address() const {
  if (gea2_addr_discovery_) {
    ESP_LOGI(TAG, "Appliance address: unresolved — %s",
             addr_discovery_halted_ ? "discovery halted, set dest_address manually" : "discovering on the bus");
  } else if (dest_configured_) {
    ESP_LOGI(TAG, "Appliance address: 0x%02X (configured)", dest_addr_);
  } else if (auto_detect_) {
    ESP_LOGI(TAG, "Appliance address: auto-detecting (no packet received yet)");
  } else {
    ESP_LOGI(TAG, "Appliance address: 0x%02X (%s)", dest_addr_,
             protocol_ == Protocol::GEA2 ? "auto-discovered" : "auto-detected");
  }
}

void GEAComponent::log_erds() const {
#ifdef GEA_GEA2_DISCOVERY
  if (gea2_discovery_) {
    if (discovery_state_ == DiscoveryState::SCANNING) {
      ESP_LOGI(TAG, "GEA2 discovery in progress: %zu / %zu scanned, %zu found so far", discovery_index_,
               GEA2_DISCOVERY_TABLE_SIZE, discovery_found_erds_.size());
    } else {
      ESP_LOGI(TAG, "GEA2 discovered ERDs (%zu) — add these to your YAML:", discovery_found_erds_.size());
      log_discovery_erds_();
    }
    return;
  }
#endif
  if (discovered_erds_.empty()) {
    ESP_LOGI(TAG, "No ERDs discovered yet");
    return;
  }
  ESP_LOGI(TAG, "Discovered ERDs (%zu):", discovered_erds_.size());
  for (auto &kv : discovered_erds_) {
    std::string raw;
    char buf[3];
    for (uint8_t b : kv.second) {
      snprintf(buf, sizeof(buf), "%02X", b);
      raw += buf;
    }
#ifdef GEA_ERD_LOOKUP
    const ErdTableEntry *info = erd_lookup(kv.first);
    if (info == nullptr) {
      ESP_LOGI(TAG, "  0x%04X  (unknown)  raw=%s", kv.first, raw.c_str());
    } else {
      std::string val = decode_erd_value(kv.second, info->type);
      if (val.empty()) {
        ESP_LOGI(TAG, "  0x%04X  %-40s  [%-20s]  %s  raw=%s", kv.first, info->name, info->type, info->ops, raw.c_str());
      } else {
        ESP_LOGI(TAG, "  0x%04X  %-40s  [%-20s]  %s  raw=%s  val=%s", kv.first, info->name, info->type, info->ops,
                 raw.c_str(), val.c_str());
      }
    }
#else
    ESP_LOGI(TAG, "  0x%04X  raw=%s", kv.first, raw.c_str());
#endif
  }
}

void GEAComponent::loop() {
  // Drain all available RX bytes.  On GEA2 the echo of our own transmission
  // is verified and consumed first; everything else goes through the frame
  // parser.  Every byte — echo or not — marks the line as busy for the
  // bus-idle TX gate.
  uint8_t byte;
  while (available()) {
    if (!read_byte(&byte))
      break;
    rx_byte_count_++;
    last_rx_byte_ms_ = millis();
    if (consume_gea2_echo_byte_(byte))
      continue;
    process_rx_byte_(byte);
  }

  // An echo that never completes (neither matched nor mismatched) means our
  // TX never reached the wire or RX is mute — don't let it wedge the bus-clear
  // gate forever.
  if (!gea2_echo_buf_.empty() && millis() - gea2_echo_at_ms_ >= GEA2_ECHO_TIMEOUT_MS) {
    ESP_LOGW(TAG, "GEA2 TX echo missing (%zu/%zu bytes seen) — check the TX/RX wiring", gea2_echo_idx_,
             gea2_echo_buf_.size());
    gea2_echo_buf_.clear();
    gea2_echo_idx_ = 0;
  }

  // Log RX stats every 10 s so we can confirm the UART is receiving at all.
  uint32_t now_stats = millis();
  if (now_stats - last_stats_ms_ >= 10000) {
    last_stats_ms_ = now_stats;
    if (protocol_ == Protocol::GEA2) {
      ESP_LOGD(TAG, "RX stats: %u bytes total, %u TX collisions, bus %s", rx_byte_count_, tx_collisions_,
               is_bus_connected() ? "CONNECTED" : "no valid packets yet");
    } else {
      ESP_LOGD(TAG, "RX stats: %u bytes total, bus %s", rx_byte_count_,
               is_bus_connected() ? "CONNECTED" : "no valid packets yet");
    }
  }

  if (protocol_ == Protocol::GEA2) {
    if (gea2_addr_discovery_) {
      // Resolve the appliance address first; skip polling until it's known.
      drive_gea2_addr_discovery_();
      return;
    }
#ifdef GEA_GEA2_DISCOVERY
    if (gea2_discovery_ && discovery_state_ == DiscoveryState::SCANNING) {
      // During discovery: enqueue one read at a time with no inter-read delay.
      // Only fire when the queue and pending slot are both clear. Gate scan
      // progress on bus liveness — while the bus is quiet, probe instead of
      // advancing, so a dead bus never burns through the table or persists an
      // empty result.
      if (!pending_active_ && request_queue_.empty()) {
        if (is_bus_connected())
          discovery_enqueue_next_();
        else
          discovery_probe_bus_();
      }
    } else {
#endif
      // Normal round-robin polling of declared entities.
      uint32_t now_poll = millis();
      if (now_poll - last_poll_ms_ >= poll_interval_ms_) {
        poll_next_();
        last_poll_ms_ = now_poll;
      }
#ifdef GEA_GEA2_DISCOVERY
    }
#endif
  } else {
    // GEA3 subscription state machine:
    //   SUBSCRIBING: send subscribe-all (type=add) every 1 s until acknowledged.
    //   SUBSCRIBED:  send subscribe-all (type=retain) every 30 s as keep-alive.
    //
    // Primary reconnection trigger is the 0xA8 "subscription host startup"
    // packet the appliance emits on boot (see process_packet_).  As a fallback
    // for cases where that packet is missed or the bus stays connected via
    // other traffic, we also resubscribe on a bus silent→active transition.
    bool connected = is_bus_connected();
    uint32_t now_sub = millis();
    if (sub_state_ == SubState::SUBSCRIBED && !was_connected_ && connected) {
      ESP_LOGI(TAG, "Bus reconnected — resubscribing");
      sub_state_ = SubState::SUBSCRIBING;
      sub_retry_ms_ = now_sub - 1000;  // trigger immediate retry on next pass
    }
    was_connected_ = connected;

    if (sub_state_ == SubState::SUBSCRIBING) {
      if (now_sub - sub_retry_ms_ >= 1000) {
        send_subscribe_all_(0x00);
        sub_retry_ms_ = now_sub;
      }
    } else {
      if (now_sub - sub_retry_ms_ >= 30000) {
        ESP_LOGV(TAG, "Subscription keep-alive (retain)");
        send_subscribe_all_(0x01);
        sub_retry_ms_ = now_sub;
      }
    }
  }

  // Request queue: retry pending on timeout, or promote the next queued
  // request when idle.  Only one request is in flight at a time so request_id
  // matching on the response is unambiguous.  On GEA2 both transmit paths
  // additionally wait for the bus to be clear (collision avoidance); a busy
  // line just defers to a later loop() pass.
  uint32_t now_req = millis();
  if (pending_active_) {
    if (now_req - pending_.sent_at_ms >= REQUEST_TIMEOUT_MS) {
      if (pending_.retries_left > 0) {
        if (gea2_bus_clear_()) {
          pending_.retries_left--;
          tx_retries_++;
          ESP_LOGD(TAG, "Request cmd=0x%02X id=0x%02X timed out, retrying (%u left)", pending_.cmd, pending_.req_id,
                   pending_.retries_left);
          transmit_pending_();
        }
      } else {
        if (pending_.is_discovery) {
#ifdef GEA_GEA2_DISCOVERY
          discovery_on_timeout_();
#endif
        } else {
          dropped_requests_++;
          ESP_LOGW(TAG, "Request cmd=0x%02X id=0x%02X exhausted retries, dropping", pending_.cmd, pending_.req_id);
        }
        finish_pending_();
      }
    }
  }
  if (!pending_active_ && !request_queue_.empty() && gea2_bus_clear_()) {
    pending_ = std::move(request_queue_.front());
    request_queue_.pop_front();
    pending_active_ = true;
    transmit_pending_();
  }

  // While a GEA2 exchange is in flight, ask the scheduler to run loop()
  // continuously (see high_freq_ in gea.h).  start()/stop() are idempotent.
  if (protocol_ == Protocol::GEA2) {
    if (pending_active_ || !gea2_echo_buf_.empty()) {
      high_freq_.start();
    } else {
      high_freq_.stop();
    }
  }
}

// =============================================================================
// GEAComponent — entity registry & write
// =============================================================================

void GEAComponent::register_entity(GEAEntity *entity) {
  entity->set_parent(this);
  entities_.push_back(entity);
}

// Body layouts (sit after the [CMD] byte on the wire; transmit_pending_ also
// inserts [REQ_ID] for GEA3 only):
//   GEA3 read:  body = [erd_h][erd_l]
//   GEA3 write: body = [erd_h][erd_l][size][data...]
//   GEA2 read:  body = [count=1][erd_h][erd_l]
//   GEA2 write: body = [count=1][erd_h][erd_l][size][data...]
void GEAComponent::read_erd(uint16_t erd) {
  std::vector<uint8_t> body;
  uint8_t cmd;
  if (protocol_ == Protocol::GEA2) {
    body = {0x01, (uint8_t)(erd >> 8), (uint8_t)(erd & 0xFF)};
    cmd = CMD_GEA2_READ;
  } else {
    body = {(uint8_t)(erd >> 8), (uint8_t)(erd & 0xFF)};
    cmd = CMD_READ_REQUEST;
  }
  enqueue_request_(cmd, std::move(body));
  ESP_LOGD(TAG, "Queued read ERD 0x%04X", erd);
}

void GEAComponent::write_erd(uint16_t erd, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> body;
  uint8_t cmd;
  if (protocol_ == Protocol::GEA2) {
    body.reserve(4 + data.size());
    body.push_back(0x01);  // erd_count
    body.push_back((uint8_t)(erd >> 8));
    body.push_back((uint8_t)(erd & 0xFF));
    body.push_back((uint8_t)data.size());
    body.insert(body.end(), data.begin(), data.end());
    cmd = CMD_GEA2_WRITE;
  } else {
    body.reserve(3 + data.size());
    body.push_back((uint8_t)(erd >> 8));
    body.push_back((uint8_t)(erd & 0xFF));
    body.push_back((uint8_t)data.size());
    body.insert(body.end(), data.begin(), data.end());
    cmd = CMD_WRITE_REQUEST;
  }
  enqueue_request_(cmd, std::move(body));
  ESP_LOGD(TAG, "Queued write ERD 0x%04X (%zu bytes)", erd, data.size());
}

// =============================================================================
// GEAComponent — GEA2 polling
// =============================================================================

// Build the round-robin poll list once entities have all registered. Called
// lazily on the first poll. Includes ERDs from registered entities and from
// on_erd_change triggers, deduplicated.
void GEAComponent::build_poll_list_() {
  poll_erds_.clear();
  for (auto *e : entities_) {
    uint16_t erd = e->get_erd();
    bool seen = false;
    for (uint16_t existing : poll_erds_) {
      if (existing == erd) {
        seen = true;
        break;
      }
    }
    if (!seen)
      poll_erds_.push_back(erd);
  }
  for (auto *t : erd_change_triggers_) {
    uint16_t erd = t->get_erd();
    bool seen = false;
    for (uint16_t existing : poll_erds_) {
      if (existing == erd) {
        seen = true;
        break;
      }
    }
    if (!seen)
      poll_erds_.push_back(erd);
  }
  poll_list_built_ = true;
  ESP_LOGI(TAG, "GEA2 poll list built: %zu unique ERDs", poll_erds_.size());
}

// Enqueue a read for the next ERD in the round-robin. Skips silently if the
// queue is non-empty (a write or prior poll is still in flight) so that polls
// don't pile up behind user-initiated traffic.
void GEAComponent::poll_next_() {
  if (!poll_list_built_)
    build_poll_list_();
  if (poll_erds_.empty())
    return;
  if (pending_active_ || !request_queue_.empty())
    return;
  uint16_t erd = poll_erds_[poll_index_];
  poll_index_ = (poll_index_ + 1) % poll_erds_.size();
  read_erd(erd);
}

// =============================================================================
// GEAComponent — GEA2 active address discovery
// =============================================================================

// Broadcast a GEA2 read of a universal identity ERD. Whichever node answers
// reveals its address in the SRC field of its response (captured in
// process_packet_ via record_addr_candidate_).
void GEAComponent::send_gea2_addr_probe_() {
  std::vector<uint8_t> payload = {CMD_GEA2_READ, 0x01, (uint8_t)(GEA2_ADDR_PROBE_ERD >> 8),
                                  (uint8_t)(GEA2_ADDR_PROBE_ERD & 0xFF)};
  send_packet_(GEA_BROADCAST_ADDR, payload);
  ESP_LOGD(TAG, "GEA2 address probe: broadcast read of ERD 0x%04X", GEA2_ADDR_PROBE_ERD);
}

// Record a distinct responder address seen during the current probe window.
void GEAComponent::record_addr_candidate_(uint8_t src) {
  for (uint8_t a : addr_candidates_) {
    if (a == src)
      return;
  }
  addr_candidates_.push_back(src);
  ESP_LOGD(TAG, "GEA2 address probe: response from 0x%02X", src);
}

// Drive the probe/collect/evaluate cycle. Called from loop() while
// gea2_addr_discovery_ is set; returns the component to normal operation once a
// single appliance is found, or halts on an ambiguous (multi-responder) bus.
void GEAComponent::drive_gea2_addr_discovery_() {
  if (addr_discovery_halted_)
    return;  // ambiguous result — idle until the user sets dest_address and reflashes

  uint32_t now = millis();

  if (!addr_probe_inflight_) {
    // Wait out the cooldown between rounds, then start a fresh probe.
    if (now - addr_probe_at_ms_ < GEA2_ADDR_PROBE_COOLDOWN_MS)
      return;
    if (!gea2_bus_clear_())
      return;  // yield to ongoing traffic; probe on a later pass
    addr_candidates_.clear();
    send_gea2_addr_probe_();
    addr_probe_inflight_ = true;
    addr_probe_at_ms_ = now;
    return;
  }

  // Probe is out — keep collecting responders until the window closes.
  if (now - addr_probe_at_ms_ < GEA2_ADDR_PROBE_WINDOW_MS)
    return;

  addr_probe_inflight_ = false;
  addr_probe_at_ms_ = now;  // start the cooldown before the next round
  addr_probe_attempts_++;

  if (addr_candidates_.size() == 1) {
    dest_addr_ = addr_candidates_[0];
    gea2_addr_discovery_ = false;
    ESP_LOGI(TAG, "GEA2 address discovery: appliance found at 0x%02X (after %u probe(s))", dest_addr_,
             (unsigned)addr_probe_attempts_);
    ESP_LOGI(TAG, "  Pin it with 'dest_address: 0x%02X' in YAML to skip discovery on future boots.", dest_addr_);
    last_poll_ms_ = millis();
#ifdef GEA_GEA2_DISCOVERY
    if (gea2_discovery_)
      discovery_init_();
#endif
  } else if (addr_candidates_.empty()) {
    bool loud = addr_probe_attempts_ <= GEA2_ADDR_PROBE_LOUD_ATTEMPTS || (addr_probe_attempts_ % 30 == 0);
    if (loud)
      ESP_LOGW(TAG,
               "GEA2 address discovery: no appliance answered the broadcast probe (attempt %u). Retrying… "
               "If this persists, check power/wiring or set 'dest_address' manually.",
               (unsigned)addr_probe_attempts_);
  } else {
    std::string list;
    char buf[8];
    for (uint8_t a : addr_candidates_) {
      snprintf(buf, sizeof(buf), "0x%02X ", a);
      list += buf;
    }
    ESP_LOGE(TAG,
             "GEA2 address discovery: %zu nodes answered (%s)— ambiguous on a multi-node bus. "
             "Set 'dest_address' to the one you want and reflash.",
             addr_candidates_.size(), list.c_str());
    addr_discovery_halted_ = true;
  }
}

// =============================================================================
// GEAComponent — RX state machine
// =============================================================================

// Byte-by-byte processing.  rx_buf_ accumulates the unescaped inner frame:
//   [DEST][LEN][SRC][PAYLOAD...][CRC_LO][CRC_HI]
// (STX and ETX are not stored.)
void GEAComponent::process_rx_byte_(uint8_t byte) {
  switch (rx_state_) {
    case RxState::IDLE:
      if (byte == GEA_STX) {
        rx_buf_.clear();
        rx_state_ = RxState::IN_PACKET;
        ESP_LOGD(TAG, "RX: STX — frame started");
      } else if (byte != GEA_ACK) {
        ESP_LOGV(TAG, "RX: unexpected byte 0x%02X while idle", byte);
      }
      break;

    case RxState::IN_PACKET:
      if (byte == GEA_STX) {
        // Unexpected re-sync: restart frame.
        rx_buf_.clear();
      } else if (byte == GEA_ESC) {
        rx_state_ = RxState::ESCAPE;
      } else if (byte == GEA_ETX) {
        // End of frame — validate and dispatch.
        process_packet_(rx_buf_);
        rx_state_ = RxState::IDLE;
        rx_buf_.clear();
      } else {
        rx_buf_.push_back(byte);
      }
      break;

    case RxState::ESCAPE:
      // Unescape: byte after ESC is the original byte as-is.
      rx_buf_.push_back(byte);
      rx_state_ = RxState::IN_PACKET;
      break;
  }
}

// =============================================================================
// GEAComponent — packet validation and dispatch
// =============================================================================

// pkt = [DEST][LEN][SRC][PAYLOAD...][CRC_MSB][CRC_LSB]  (already unescaped)
void GEAComponent::process_packet_(const std::vector<uint8_t> &pkt) {
  // Minimum viable packet: DEST + LEN + SRC + CMD + CRC_LO + CRC_HI = 6 bytes.
  if (pkt.size() < 6) {
    ESP_LOGV(TAG, "Short packet (%zu bytes), discarding", pkt.size());
    return;
  }

  uint8_t dest = pkt[0];
  uint8_t src = pkt[2];

  ESP_LOGV(TAG, "RX frame: dest=0x%02X src=0x%02X len=%zu", dest, src, pkt.size());

  // On the half-duplex GEA2 bus, everything we transmit is echoed back on RX.
  // The echo is normally consumed byte-by-byte upstream (consume_gea2_echo_byte_,
  // which doubles as collision detection), so this SRC check is a fallback for
  // echo bytes that slip through — e.g. after an echo timeout.  A frame whose
  // SRC is our own address is never a genuine inbound message, so drop it
  // before the generic dest filter: otherwise broadcast probes (dest=0xFF)
  // re-enter as phantom packets, and the echo masquerades as foreign traffic.
  if (src == src_addr_) {
    ESP_LOGV(TAG, "RX: self-echo (TX loopback) from 0x%02X, dropping", src);
    return;
  }

  // Filter packets not addressed to us (or broadcast).
  if (dest != src_addr_ && dest != GEA_BROADCAST_ADDR) {
    ESP_LOGD(TAG, "Ignoring packet for 0x%02X (we are 0x%02X)", dest, src_addr_);
    return;
  }

  // Validate CRC: computed over everything except the last 2 (CRC) bytes.
  // GEA3 wire order is CRC MSB first, then LSB (big-endian).
  size_t crc_offset = pkt.size() - 2;
  uint16_t rx_crc = ((uint16_t)pkt[crc_offset] << 8) | (uint16_t)pkt[crc_offset + 1];
  uint16_t calc_crc = crc16_(pkt.data(), crc_offset);

  if (rx_crc != calc_crc) {
    crc_errors_++;
    ESP_LOGW(TAG, "CRC mismatch from 0x%02X: got 0x%04X, expected 0x%04X (packet len=%zu)", src, rx_crc, calc_crc,
             pkt.size());
    return;
  }

  ESP_LOGD(TAG, "Valid packet: src=0x%02X cmd=0x%02X len=%zu", src, pkt.size() >= 4 ? pkt[3] : 0, pkt.size());

  // Packet is valid — acknowledge it and record receive time (used by is_bus_connected()).
  send_ack_();
  last_rx_ms_ = millis();

  // GEA2 active address discovery: any valid frame from a real node (the
  // self-echo and broadcast cases are already excluded above) is a candidate
  // appliance address. drive_gea2_addr_discovery_() evaluates the collected set.
  if (gea2_addr_discovery_)
    record_addr_candidate_(src);

  // Auto-detect: lock onto the source address of the first valid packet.
  if (auto_detect_ && src != GEA_BROADCAST_ADDR && src != src_addr_) {
    dest_addr_ = src;
    auto_detect_ = false;
    ESP_LOGI(TAG, "Auto-detected appliance address: 0x%02X", dest_addr_);
  }

  // pkt[2] = SRC (appliance address), pkt[3] = first payload byte = command.
  if (pkt.size() < 4)
    return;
  uint8_t cmd = pkt[3];

  switch (cmd) {
    case CMD_READ_RESPONSE: {
      // [CMD][req_id][result][ERD_H][ERD_L][size][data...]
      // Offsets into pkt (after DEST/LEN/SRC): 3=CMD, 4=req_id, 5=result,
      //                                          6=ERD_H, 7=ERD_L, 8=size, 9..
      if (pkt.size() < 9)
        break;
      uint8_t resp_req_id = pkt[4];
      if (!response_matches_pending_(cmd, resp_req_id)) {
        ESP_LOGV(TAG, "Unmatched read response (req_id=0x%02X)", resp_req_id);
        break;
      }
      uint8_t result = pkt[5];
      uint16_t erd = (uint16_t)pkt[6] << 8 | pkt[7];
      if (result != 0x00) {
        ESP_LOGW(TAG, "Read ERD 0x%04X failed, result=0x%02X", erd, result);
        finish_pending_();
        break;
      }
      uint8_t size = pkt[8];
      if (pkt.size() < (size_t)(9 + size)) {
        finish_pending_();
        break;
      }
      std::vector<uint8_t> data(pkt.begin() + 9, pkt.begin() + 9 + size);
      {
        std::string hex;
        char buf[3];
        for (uint8_t b : data) {
          snprintf(buf, sizeof(buf), "%02X", b);
          hex += buf;
        }
        ESP_LOGI(TAG, "Read ERD 0x%04X OK (%zu B): %s", erd, data.size(), hex.c_str());
      }
      log_discovery_(erd, data);
      dispatch_erd_(erd, data);
      finish_pending_();
      break;
    }

    case CMD_WRITE_RESPONSE: {
      // [CMD][req_id][result][ERD_H][ERD_L]
      if (pkt.size() < 8)
        break;
      uint8_t resp_req_id = pkt[4];
      if (!response_matches_pending_(cmd, resp_req_id)) {
        ESP_LOGV(TAG, "Unmatched write response (req_id=0x%02X)", resp_req_id);
        break;
      }
      uint8_t result = pkt[5];
      uint16_t erd = (uint16_t)pkt[6] << 8 | pkt[7];
      if (result != 0x00) {
        ESP_LOGW(TAG, "Write ERD 0x%04X failed, result=0x%02X", erd, result);
      } else {
        ESP_LOGD(TAG, "Write ERD 0x%04X OK", erd);
      }
      finish_pending_();
      break;
    }

    case CMD_SUB_HOST_STARTUP: {
      // Appliance announces it just came online. Payload is the single CMD byte
      // (total wire size = 3 header + 1 payload + 2 CRC = 6). Any prior
      // subscription is gone; drop back to SUBSCRIBING to re-add immediately.
      if (pkt.size() != 6)
        break;
      if (sub_state_ == SubState::SUBSCRIBED) {
        ESP_LOGI(TAG, "Appliance startup (0xA8) — resubscribing");
        sub_state_ = SubState::SUBSCRIBING;
        sub_retry_ms_ = millis() - 1000;  // trigger immediate retry next loop
      }
      break;
    }

    case CMD_SUB_ALL_RESPONSE: {
      // [CMD][req_id][result]
      if (pkt.size() < 6)
        break;
      uint8_t resp_req_id = pkt[4];
      if (!response_matches_pending_(cmd, resp_req_id)) {
        ESP_LOGV(TAG, "Unmatched subscribe-all response (req_id=0x%02X)", resp_req_id);
        break;
      }
      uint8_t result = pkt[5];
      if (result == 0x00) {
        if (sub_state_ == SubState::SUBSCRIBING) {
          ESP_LOGI(TAG, "Subscribed — waiting for publications");
          sub_state_ = SubState::SUBSCRIBED;
          sub_retry_ms_ = millis();
        }
      } else {
        ESP_LOGW(TAG, "Subscribe-all rejected, result=0x%02X", result);
      }
      finish_pending_();
      break;
    }

    case CMD_GEA2_READ: {
      // GEA2 read response (request and response share the same code 0xF0).
      // Layout (offsets into pkt after DEST/LEN/SRC):
      //   3=cmd, 4=erd_count, 5=erd_h, 6=erd_l, 7=size, 8..= data, then CRC
      // Minimum size: 3 (header) + 4 (cmd, count, erd_h, erd_l) + 1 (size) + 2 (CRC)
      if (protocol_ != Protocol::GEA2)
        break;
      if (pkt.size() < 10)
        break;
      uint8_t erd_count = pkt[4];
      if (erd_count != 1) {
        ESP_LOGW(TAG, "GEA2 read response with erd_count=%u not supported", erd_count);
        break;
      }
      uint16_t erd = (uint16_t)pkt[5] << 8 | pkt[6];
      uint8_t size = pkt[7];
      if (pkt.size() < (size_t)(10 + size))
        break;
      if (!gea2_response_matches_pending_(cmd, erd)) {
        ESP_LOGV(TAG, "Unmatched GEA2 read response for ERD 0x%04X", erd);
        break;
      }
      std::vector<uint8_t> data(pkt.begin() + 8, pkt.begin() + 8 + size);
      {
        std::string hex;
        char buf[3];
        for (uint8_t b : data) {
          snprintf(buf, sizeof(buf), "%02X", b);
          hex += buf;
        }
        ESP_LOGI(TAG, "Read ERD 0x%04X OK (%zu B): %s", erd, data.size(), hex.c_str());
      }
#ifdef GEA_GEA2_DISCOVERY
      if (pending_.is_discovery) {
        discovery_on_response_(erd);
        finish_pending_();
        break;
      }
#endif
      log_discovery_(erd, data);
      dispatch_erd_(erd, data);
      finish_pending_();
      break;
    }

    case CMD_GEA2_WRITE: {
      // GEA2 write response: [cmd=0xF1][erd_count=1][erd_h][erd_l] (no result code).
      // Receiving the echoed write response means the appliance accepted it.
      // Min size: 3 (header) + 4 (cmd, count, erd_h, erd_l) + 2 (CRC) = 9.
      if (protocol_ != Protocol::GEA2)
        break;
      if (pkt.size() < 9)
        break;
      uint8_t erd_count = pkt[4];
      if (erd_count != 1)
        break;
      uint16_t erd = (uint16_t)pkt[5] << 8 | pkt[6];
      if (!gea2_response_matches_pending_(cmd, erd)) {
        ESP_LOGV(TAG, "Unmatched GEA2 write response for ERD 0x%04X", erd);
        break;
      }
      ESP_LOGD(TAG, "Write ERD 0x%04X OK", erd);
      finish_pending_();
      // Re-read the ERD so entity state reflects the committed value without
      // waiting for the next poll cycle.
      read_erd(erd);
      break;
    }

    case CMD_PUBLICATION: {
      // [CMD=0xA6][context][request_id][erd_count][ERD_H][ERD_L][size][data...]...
      // Offsets: 3=CMD, 4=context, 5=request_id, 6=erd_count, 7+=ERD entries
      if (pkt.size() < 9)
        break;
      uint8_t context = pkt[4];
      uint8_t pub_req_id = pkt[5];
      uint8_t erd_count = pkt[6];
      size_t offset = 7;
      for (uint8_t i = 0; i < erd_count; i++) {
        if (offset + 3 > pkt.size() - 2)  // need ERD_H+ERD_L+size before CRC
          break;
        uint16_t erd = (uint16_t)pkt[offset] << 8 | pkt[offset + 1];
        uint8_t size = pkt[offset + 2];
        offset += 3;
        if (offset + size > pkt.size() - 2)
          break;
        std::vector<uint8_t> data(pkt.begin() + offset, pkt.begin() + offset + size);
        offset += size;
        log_discovery_(erd, data);
        dispatch_erd_(erd, data);
      }
      send_pub_ack_(context, pub_req_id);
      break;
    }

    default: ESP_LOGV(TAG, "Unknown command 0x%02X, ignoring", cmd); break;
  }
}

// Route ERD data to all registered entities with a matching ERD address.
void GEAComponent::dispatch_erd_(uint16_t erd, const std::vector<uint8_t> &data) {
  for (auto *entity : entities_) {
    if (entity->get_erd() == erd) {
      entity->on_erd_data(data);
    }
  }
}

// Track discovered ERDs — log only on first appearance, update silently thereafter.
// Also evaluates any on_erd_change triggers for this ERD (rising/falling/any edges
// on masked bytes). First publication of each ERD after boot is treated as a silent
// baseline — no triggers fire — so a mid-cycle reboot doesn't re-notify bits that
// are already set.
void GEAComponent::log_discovery_(uint16_t erd, const std::vector<uint8_t> &data) {
  // Bound the map: appliances rarely publish more than ~200 ERDs, but a buggy
  // device or noisy bus could grow this unbounded. Cap to avoid OOM on RAM-tight
  // targets (ESP8266 has ~50 KB heap).
  static constexpr size_t MAX_DISCOVERED_ERDS = 512;

  auto it = discovered_erds_.find(erd);
  bool is_new = (it == discovered_erds_.end());
  std::vector<uint8_t> old_data;
  if (!is_new)
    old_data = it->second;  // capture previous value before overwriting

  if (is_new && discovered_erds_.size() >= MAX_DISCOVERED_ERDS) {
    ESP_LOGW(TAG, "discovered_erds_ cap (%zu) reached — skipping ERD 0x%04X", MAX_DISCOVERED_ERDS, erd);
    return;
  }
  discovered_erds_[erd] = data;

  if (is_new) {
    std::string hex = "0x";
    char buf[3];
    for (uint8_t b : data) {
      snprintf(buf, sizeof(buf), "%02X", b);
      hex += buf;
    }
    ESP_LOGI(TAG, "New ERD 0x%04X (%zu B): %s  [total discovered: %zu]", erd, data.size(), hex.c_str(),
             discovered_erds_.size());
    return;  // baseline only — no triggers on first contact
  }

  // Evaluate registered on_erd_change triggers for this ERD.
  for (auto *trig : erd_change_triggers_) {
    if (trig->get_erd() == erd && trig->evaluate(old_data, data)) {
      trig->trigger();
    }
  }
}

// =============================================================================
// GEAComponent — GEA2 ERD discovery (compiled only when GEA_GEA2_DISCOVERY set)
// =============================================================================

#ifdef GEA_GEA2_DISCOVERY

static constexpr uint32_t FNV_DISCOVERY_KEY = 0xD15C0;  // arbitrary stable key

// djb2 hash over the model string bytes — used to detect appliance swap.
static uint32_t model_hash(const std::vector<uint8_t> &data) {
  uint32_t h = 5381;
  for (uint8_t b : data)
    h = ((h << 5) + h) ^ b;
  return h;
}

void GEAComponent::discovery_init_() {
  discovery_bitmap_.assign(GEA2_DISCOVERY_BITMAP_BYTES, 0);
  discovery_pref_ = global_preferences->make_preference<Gea2DiscoveryPrefs>(FNV_DISCOVERY_KEY, true);
  Gea2DiscoveryPrefs prefs{};
  bool have_prefs = discovery_pref_.load(&prefs);
  if (have_prefs)
    memcpy(discovery_bitmap_.data(), prefs.valid_bitmap, GEA2_DISCOVERY_BITMAP_BYTES);

  if (have_prefs && prefs.scan_index >= GEA2_DISCOVERY_TABLE_SIZE) {
    // Previous scan complete — restore found list from bitmap (info only, not polled).
    for (size_t i = 0; i < GEA2_DISCOVERY_TABLE_SIZE; i++) {
      if (discovery_bitmap_[i / 8] & (1u << (i % 8)))
        discovery_found_erds_.push_back(GEA2_DISCOVERY_TABLE[i].id);
    }
    discovery_state_ = DiscoveryState::DONE;
    ESP_LOGI(TAG, "GEA2 discovery: loaded %zu ERDs from flash — use log_erds() or check logs for the list",
             discovery_found_erds_.size());
    log_discovery_erds_();
  } else {
    // Fresh start or interrupted scan — resume from saved index.
    discovery_index_ = (have_prefs && prefs.scan_index < GEA2_DISCOVERY_TABLE_SIZE) ? prefs.scan_index : 0;
    discovery_state_ = DiscoveryState::SCANNING;
    if (discovery_index_ > 0) {
      for (size_t i = 0; i < discovery_index_; i++) {
        if (discovery_bitmap_[i / 8] & (1u << (i % 8)))
          discovery_found_erds_.push_back(GEA2_DISCOVERY_TABLE[i].id);
      }
      ESP_LOGI(TAG, "GEA2 discovery: resuming at %zu / %zu (%zu found so far)", discovery_index_,
               GEA2_DISCOVERY_TABLE_SIZE, discovery_found_erds_.size());
    } else {
      ESP_LOGI(TAG, "GEA2 discovery: armed for a full scan (%zu ERDs, ~20-30 min) — starts once the bus responds",
               GEA2_DISCOVERY_TABLE_SIZE);
    }
  }
}

void GEAComponent::discovery_enqueue_next_() {
  if (discovery_index_ >= GEA2_DISCOVERY_TABLE_SIZE) {
    discovery_finish_();
    return;
  }
  uint16_t erd = GEA2_DISCOVERY_TABLE[discovery_index_].id;
  // Single attempt only — 250 ms timeout, then move on.
  std::vector<uint8_t> body = {0x01, (uint8_t)(erd >> 8), (uint8_t)(erd & 0xFF)};
  PendingRequest req;
  req.cmd = CMD_GEA2_READ;
  req.req_id = next_req_id_();
  req.dest = dest_addr_;
  req.body = std::move(body);
  req.retries_left = 0;
  req.sent_at_ms = 0;
  req.is_discovery = true;
  request_queue_.push_back(std::move(req));
}

// Liveness probe used while the bus looks quiet: a throttled plain read of a
// universal ERD. It is NOT flagged is_discovery, so its response is handled as
// an ordinary read (refreshing is_bus_connected()) and never touches the scan
// bitmap. Once it answers, the loop resumes advancing the scan.
void GEAComponent::discovery_probe_bus_() {
  uint32_t now = millis();
  if (now - discovery_probe_ms_ < DISCOVERY_PROBE_INTERVAL_MS)
    return;
  discovery_probe_ms_ = now;
  std::vector<uint8_t> body = {0x01, (uint8_t)(GEA2_LIVENESS_ERD >> 8), (uint8_t)(GEA2_LIVENESS_ERD & 0xFF)};
  PendingRequest req;
  req.cmd = CMD_GEA2_READ;
  req.req_id = next_req_id_();
  req.dest = dest_addr_;
  req.body = std::move(body);
  req.retries_left = 0;      // single attempt; the throttle re-issues
  req.is_discovery = false;  // ordinary read — must not mark the scan bitmap
  req.sent_at_ms = 0;
  request_queue_.push_back(std::move(req));
  ESP_LOGD(TAG, "GEA2 discovery: bus quiet — liveness probe of ERD 0x%04X", GEA2_LIVENESS_ERD);
}

void GEAComponent::discovery_on_response_(uint16_t erd) {
  discovery_found_erds_.push_back(erd);
  discovery_bitmap_[discovery_index_ / 8] |= (1u << (discovery_index_ % 8));
  ESP_LOGD(TAG, "Discovery: ERD 0x%04X responded (%zu / %zu)", erd, discovery_index_ + 1, GEA2_DISCOVERY_TABLE_SIZE);
  discovery_advance_();
}

void GEAComponent::discovery_on_timeout_() {
  ESP_LOGV(TAG, "Discovery: ERD 0x%04X no response (%zu / %zu)", GEA2_DISCOVERY_TABLE[discovery_index_].id,
           discovery_index_ + 1, GEA2_DISCOVERY_TABLE_SIZE);
  discovery_advance_();
}

void GEAComponent::discovery_advance_() {
  discovery_index_++;
  if (discovery_index_ % 50 == 0) {
    uint32_t pct = (uint32_t)(discovery_index_ * 100 / GEA2_DISCOVERY_TABLE_SIZE);
    ESP_LOGI(TAG, "Discovery progress: %zu / %zu (%u%%) — %zu ERDs found so far", discovery_index_,
             GEA2_DISCOVERY_TABLE_SIZE, pct, discovery_found_erds_.size());
  }
  if (discovery_index_ % 100 == 0)
    discovery_save_progress_();
  if (discovery_index_ >= GEA2_DISCOVERY_TABLE_SIZE)
    discovery_finish_();
}

void GEAComponent::discovery_save_progress_() {
  Gea2DiscoveryPrefs prefs{};
  prefs.scan_index = (uint32_t)discovery_index_;
  memcpy(prefs.valid_bitmap, discovery_bitmap_.data(), GEA2_DISCOVERY_BITMAP_BYTES);
  discovery_pref_.save(&prefs);
  ESP_LOGD(TAG, "Discovery: saved progress — %zu / %zu scanned, %zu found", discovery_index_, GEA2_DISCOVERY_TABLE_SIZE,
           poll_erds_.size());
}

void GEAComponent::log_discovery_erds_() const {
  if (discovery_found_erds_.empty()) {
    ESP_LOGI(TAG, "  (no ERDs discovered yet)");
    return;
  }
  for (uint16_t erd : discovery_found_erds_) {
#ifdef GEA_ERD_LOOKUP
    const ErdTableEntry *info = erd_lookup(erd);
    if (info)
      ESP_LOGI(TAG, "  0x%04X  # %s", erd, info->name);
    else
      ESP_LOGI(TAG, "  0x%04X  # (undocumented)", erd);
#else
    ESP_LOGI(TAG, "  0x%04X", erd);
#endif
  }
}

void GEAComponent::discovery_finish_() {
  discovery_save_progress_();
  discovery_state_ = DiscoveryState::DONE;
  ESP_LOGI(TAG, "GEA2 discovery complete — %zu ERDs responded out of %zu scanned", discovery_found_erds_.size(),
           GEA2_DISCOVERY_TABLE_SIZE);
  ESP_LOGI(TAG, "Copy the ERDs below into your YAML to build your configuration:");
  log_discovery_erds_();
}

#endif  // GEA_GEA2_DISCOVERY

}  // namespace gea
}  // namespace esphome
