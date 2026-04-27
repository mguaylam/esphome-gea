#include "gea.h"
#ifdef GEA_ERD_LOOKUP
#include "erd_table.h"
#endif
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

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

  write_byte(GEA_STX);
  write_array(escaped.data(), escaped.size());
  write_byte(GEA_ETX);
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
  uint16_t pending_erd = ((uint16_t) pending_.body[1] << 8) | pending_.body[2];
  return pending_erd == erd;
}

// =============================================================================
// GEAComponent — ESPHome lifecycle
// =============================================================================

void GEAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GEA component (protocol=%s)...",
                protocol_ == Protocol::GEA2 ? "GEA2" : "GEA3");
  rx_buf_.reserve(64);
  if (protocol_ == Protocol::GEA2) {
    // GEA2 has no subscribe-all and no spontaneous publications. The Python
    // schema enforces dest_address is set, so auto_detect_ is irrelevant here.
    auto_detect_ = false;
    ESP_LOGI(TAG, "GEA2 mode — values will be polled (interval=%u ms)", poll_interval_ms_);
    last_poll_ms_ = millis();
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
  if (auto_detect_) {
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

void GEAComponent::log_erds() const {
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
  }
}
#endif  // GEA_ERD_LOOKUP

void GEAComponent::loop() {
  // Drain all available RX bytes through the state machine.
  uint8_t byte;
  while (available()) {
    if (!read_byte(&byte))
      break;
    rx_byte_count_++;
    process_rx_byte_(byte);
  }

  // Log RX stats every 10 s so we can confirm the UART is receiving at all.
  uint32_t now_stats = millis();
  if (now_stats - last_stats_ms_ >= 10000) {
    last_stats_ms_ = now_stats;
    ESP_LOGD(TAG, "RX stats: %u bytes total, bus %s", rx_byte_count_,
             is_bus_connected() ? "CONNECTED" : "no valid packets yet");
  }

  if (protocol_ == Protocol::GEA2) {
    // GEA2: no subscriptions/publications. Fire one read at a time on a
    // round-robin schedule. The request queue serializes everything, so a
    // user-initiated write naturally interleaves between polls.
    uint32_t now_poll = millis();
    if (now_poll - last_poll_ms_ >= poll_interval_ms_) {
      poll_next_();
      last_poll_ms_ = now_poll;
    }
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
  // matching on the response is unambiguous.
  uint32_t now_req = millis();
  if (pending_active_) {
    if (now_req - pending_.sent_at_ms >= REQUEST_TIMEOUT_MS) {
      if (pending_.retries_left > 0) {
        pending_.retries_left--;
        tx_retries_++;
        ESP_LOGD(TAG, "Request cmd=0x%02X id=0x%02X timed out, retrying (%u left)", pending_.cmd, pending_.req_id,
                 pending_.retries_left);
        transmit_pending_();
      } else {
        dropped_requests_++;
        ESP_LOGW(TAG, "Request cmd=0x%02X id=0x%02X exhausted retries, dropping", pending_.cmd, pending_.req_id);
        finish_pending_();
      }
    }
  }
  if (!pending_active_ && !request_queue_.empty()) {
    pending_ = std::move(request_queue_.front());
    request_queue_.pop_front();
    pending_active_ = true;
    transmit_pending_();
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
    body.push_back((uint8_t) data.size());
    body.insert(body.end(), data.begin(), data.end());
    cmd = CMD_GEA2_WRITE;
  } else {
    body.reserve(3 + data.size());
    body.push_back((uint8_t)(erd >> 8));
    body.push_back((uint8_t)(erd & 0xFF));
    body.push_back((uint8_t) data.size());
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
      uint16_t erd = (uint16_t) pkt[5] << 8 | pkt[6];
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
      uint16_t erd = (uint16_t) pkt[5] << 8 | pkt[6];
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

}  // namespace gea
}  // namespace esphome
