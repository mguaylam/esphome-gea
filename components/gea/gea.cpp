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
  if (data.size() <= (size_t) byte_offset_)
    return 0.0f;
  const uint8_t *d = data.data() + byte_offset_;
  size_t rem = data.size() - byte_offset_;

  switch (decode_) {
    case GeaDecodeType::UINT8:
      return (rem >= 1) ? (float) d[0] : 0.0f;

    case GeaDecodeType::UINT16_BE:
      return (rem >= 2) ? (float) ((uint16_t) d[0] << 8 | d[1]) : 0.0f;

    case GeaDecodeType::UINT16_LE:
      return (rem >= 2) ? (float) ((uint16_t) d[1] << 8 | d[0]) : 0.0f;

    case GeaDecodeType::UINT32_BE:
      if (rem >= 4)
        return (float) ((uint32_t) d[0] << 24 | (uint32_t) d[1] << 16 | (uint32_t) d[2] << 8 | d[3]);
      if (rem == 3)
        return (float) ((uint32_t) d[0] << 16 | (uint32_t) d[1] << 8 | d[2]);
      return 0.0f;

    case GeaDecodeType::UINT32_LE:
      return (rem >= 4)
          ? (float) ((uint32_t) d[3] << 24 | (uint32_t) d[2] << 16 | (uint32_t) d[1] << 8 | d[0])
          : 0.0f;

    case GeaDecodeType::INT8:
      return (rem >= 1) ? (float) (int8_t) d[0] : 0.0f;

    case GeaDecodeType::INT16_BE:
      return (rem >= 2) ? (float) (int16_t) ((uint16_t) d[0] << 8 | d[1]) : 0.0f;

    case GeaDecodeType::INT16_LE:
      return (rem >= 2) ? (float) (int16_t) ((uint16_t) d[1] << 8 | d[0]) : 0.0f;

    case GeaDecodeType::INT32_BE:
      return (rem >= 4)
          ? (float) (int32_t) ((uint32_t) d[0] << 24 | (uint32_t) d[1] << 16 | (uint32_t) d[2] << 8 | d[3])
          : 0.0f;

    case GeaDecodeType::INT32_LE:
      return (rem >= 4)
          ? (float) (int32_t) ((uint32_t) d[3] << 24 | (uint32_t) d[2] << 16 | (uint32_t) d[1] << 8 | d[0])
          : 0.0f;

    case GeaDecodeType::BOOL:
      return (rem >= 1) ? ((d[0] & bitmask_) ? 1.0f : 0.0f) : 0.0f;

    default:
      return 0.0f;
  }
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
      case GeaDecodeType::INT16_LE:
        n = 2;
        break;
      case GeaDecodeType::UINT32_BE:
      case GeaDecodeType::INT32_BE:
      case GeaDecodeType::UINT32_LE:
      case GeaDecodeType::INT32_LE:
        n = 4;
        break;
      default:
        n = 1;
        break;
    }
  }
  bool le = false;
  switch (decode_) {
    case GeaDecodeType::UINT16_LE:
    case GeaDecodeType::INT16_LE:
    case GeaDecodeType::UINT32_LE:
    case GeaDecodeType::INT32_LE:
      le = true;
      break;
    default:
      break;
  }
  if (le) {
    for (uint8_t i = 0; i < n; i++)
      out.push_back((val >> (i * 8)) & 0xFF);
  } else {
    for (int i = n - 1; i >= 0; i--)
      out.push_back((val >> (i * 8)) & 0xFF);
  }
}

// =============================================================================
// GEAComponent — protocol utilities
// =============================================================================

// CRC-16/CCITT: polynomial 0x1021, seed 0x1021
// Covers [DEST][LEN][SRC][PAYLOAD...] (everything between STX and CRC bytes).
uint16_t GEAComponent::crc16_(const uint8_t *data, size_t len) {
  uint16_t crc = GEA_CRC_SEED;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t) data[i] << 8;
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
      out.push_back(b);  // no transform — receiver uses state machine to distinguish
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
  uint8_t len = (uint8_t) (7 + payload.size());

  // Build the inner portion (subject to CRC and escaping)
  std::vector<uint8_t> inner;
  inner.reserve(4 + payload.size() + 2);
  inner.push_back(dest);
  inner.push_back(len);
  inner.push_back(src_addr_);
  inner.insert(inner.end(), payload.begin(), payload.end());

  uint16_t crc = crc16_(inner.data(), inner.size());
  inner.push_back(crc >> 8);     // CRC MSB (sent first, per GEA3 spec)
  inner.push_back(crc & 0xFF);   // CRC LSB

  auto escaped = escape_(inner);

  write_byte(GEA_STX);
  write_array(escaped.data(), escaped.size());
  write_byte(GEA_ETX);

  req_id_++;
  if (req_id_ == 0)
    req_id_ = 1;  // keep non-zero
}

// Send a bare ACK byte (no framing — GEA3 ACK is a single 0xE1 on the wire).
void GEAComponent::send_ack_() { write_byte(GEA_ACK); }

// Send a framed publication acknowledgement: [CMD_PUB_ACK][context][request_id].
// The context and request_id must echo the values from the publication header.
void GEAComponent::send_pub_ack_(uint8_t context, uint8_t request_id) {
  std::vector<uint8_t> payload = {CMD_PUB_ACK, context, request_id};
  send_packet_(dest_addr_, payload);
}

// Trigger ERD discovery: the appliance responds with a publication (0xA6) for
// every ERD it supports.  We log each one and also route values to any
// user-configured entities that match.
void GEAComponent::send_subscribe_all_(uint8_t type) {
  // type: 0x00 = add subscription, 0x01 = retain (keep-alive)
  std::vector<uint8_t> payload = {CMD_SUB_ALL_REQUEST, req_id_, type};
  send_packet_(dest_addr_, payload);
  ESP_LOGD(TAG, "Sent subscribe-all request (type=%u)", type);
}

// =============================================================================
// GEAComponent — ESPHome lifecycle
// =============================================================================

void GEAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GEA3 component...");
  rx_buf_.reserve(64);
  if (auto_detect_) {
    ESP_LOGI(TAG, "Address auto-detect enabled — sending subscribe-all to broadcast");
    dest_addr_ = GEA_BROADCAST_ADDR;
  }
  send_subscribe_all_(0x00);  // type=add
  sub_retry_ms_ = millis();
}

void GEAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "GEA3 Component:");
  if (auto_detect_) {
    ESP_LOGCONFIG(TAG, "  Dest address: auto-detect (current: 0x%02X)", dest_addr_);
  } else {
    ESP_LOGCONFIG(TAG, "  Dest address: 0x%02X", dest_addr_);
  }
  ESP_LOGCONFIG(TAG, "  Src address:  0x%02X", src_addr_);
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
          ESP_LOGCONFIG(TAG, "    0x%04X  %-40s  [%s]  raw=%s",
                        kv.first, info->name, info->type, raw.c_str());
        } else {
          ESP_LOGCONFIG(TAG, "    0x%04X  %-40s  [%s]  raw=%s  val=%s",
                        kv.first, info->name, info->type, raw.c_str(), val.c_str());
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

    if (!result.empty()) result += "/";

    char buf[32];
    if (field == "u8" || field == "enum") {
      if (offset < data.size()) {
        snprintf(buf, sizeof(buf), "%u", data[offset++]);
        result += buf;
      }
    } else if (field == "i8") {
      if (offset < data.size()) {
        snprintf(buf, sizeof(buf), "%d", (int8_t) data[offset++]);
        result += buf;
      }
    } else if (field == "bool") {
      if (offset < data.size())
        result += data[offset++] ? "true" : "false";
    } else if (field == "u16") {
      if (offset + 2 <= data.size()) {
        uint16_t v = ((uint16_t) data[offset] << 8) | data[offset + 1];
        offset += 2;
        snprintf(buf, sizeof(buf), "%u", v);
        result += buf;
      }
    } else if (field == "i16") {
      if (offset + 2 <= data.size()) {
        int16_t v = (int16_t)(((uint16_t) data[offset] << 8) | data[offset + 1]);
        offset += 2;
        snprintf(buf, sizeof(buf), "%d", v);
        result += buf;
      }
    } else if (field == "u32") {
      if (offset + 4 <= data.size()) {
        uint32_t v = ((uint32_t) data[offset] << 24) | ((uint32_t) data[offset + 1] << 16) |
                     ((uint32_t) data[offset + 2] << 8) | data[offset + 3];
        offset += 4;
        snprintf(buf, sizeof(buf), "%u", v);
        result += buf;
      }
    } else if (field == "i32") {
      if (offset + 4 <= data.size()) {
        int32_t v = (int32_t)(((uint32_t) data[offset] << 24) | ((uint32_t) data[offset + 1] << 16) |
                              ((uint32_t) data[offset + 2] << 8) | data[offset + 3]);
        offset += 4;
        snprintf(buf, sizeof(buf), "%d", v);
        result += buf;
      }
    } else if (field == "string") {
      result += '"';
      while (offset < data.size() && data[offset] != 0)
        result += (char) data[offset++];
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
        ESP_LOGI(TAG, "  0x%04X  %-40s  [%-20s]  %s  raw=%s",
                 kv.first, info->name, info->type, info->ops, raw.c_str());
      } else {
        ESP_LOGI(TAG, "  0x%04X  %-40s  [%-20s]  %s  raw=%s  val=%s",
                 kv.first, info->name, info->type, info->ops, raw.c_str(), val.c_str());
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
    ESP_LOGD(TAG, "RX stats: %u bytes total, bus %s",
             rx_byte_count_,
             is_bus_connected() ? "CONNECTED" : "no valid packets yet");
  }

  // Subscription state machine:
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

// =============================================================================
// GEAComponent — entity registry & write
// =============================================================================

void GEAComponent::register_entity(GEAEntity *entity) {
  entity->set_parent(this);
  entities_.push_back(entity);
}

void GEAComponent::write_erd(uint16_t erd, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> payload;
  payload.reserve(4 + data.size());
  payload.push_back(CMD_WRITE_REQUEST);
  payload.push_back(req_id_);
  payload.push_back((uint8_t) (erd >> 8));
  payload.push_back((uint8_t) (erd & 0xFF));
  payload.push_back((uint8_t) data.size());
  payload.insert(payload.end(), data.begin(), data.end());
  send_packet_(dest_addr_, payload);
  ESP_LOGD(TAG, "Write ERD 0x%04X (%zu bytes)", erd, data.size());
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
  uint8_t src  = pkt[2];

  ESP_LOGV(TAG, "RX frame: dest=0x%02X src=0x%02X len=%zu", dest, src, pkt.size());

  // Filter packets not addressed to us (or broadcast).
  if (dest != src_addr_ && dest != GEA_BROADCAST_ADDR) {
    ESP_LOGD(TAG, "Ignoring packet for 0x%02X (we are 0x%02X)", dest, src_addr_);
    return;
  }

  // Validate CRC: computed over everything except the last 2 (CRC) bytes.
  // GEA3 wire order is CRC MSB first, then LSB (big-endian).
  size_t crc_offset = pkt.size() - 2;
  uint16_t rx_crc = ((uint16_t) pkt[crc_offset] << 8) | (uint16_t) pkt[crc_offset + 1];
  uint16_t calc_crc = crc16_(pkt.data(), crc_offset);

  if (rx_crc != calc_crc) {
    ESP_LOGW(TAG, "CRC mismatch from 0x%02X: got 0x%04X, expected 0x%04X (packet len=%zu)",
             src, rx_crc, calc_crc, pkt.size());
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
      uint8_t result = pkt[5];
      if (result != 0x00) {
        uint16_t erd = (uint16_t) pkt[6] << 8 | pkt[7];
        ESP_LOGW(TAG, "Read ERD 0x%04X failed, result=0x%02X", erd, result);
        break;
      }
      uint16_t erd = (uint16_t) pkt[6] << 8 | pkt[7];
      uint8_t size = pkt[8];
      if (pkt.size() < (size_t) (9 + size))
        break;
      std::vector<uint8_t> data(pkt.begin() + 9, pkt.begin() + 9 + size);
      dispatch_erd_(erd, data);
      break;
    }

    case CMD_WRITE_RESPONSE: {
      // [CMD][req_id][result][ERD_H][ERD_L]
      if (pkt.size() < 8)
        break;
      uint8_t result = pkt[5];
      uint16_t erd = (uint16_t) pkt[6] << 8 | pkt[7];
      if (result != 0x00) {
        ESP_LOGW(TAG, "Write ERD 0x%04X failed, result=0x%02X", erd, result);
      } else {
        ESP_LOGD(TAG, "Write ERD 0x%04X OK", erd);
      }
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
        uint16_t erd = (uint16_t) pkt[offset] << 8 | pkt[offset + 1];
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

    default:
      ESP_LOGV(TAG, "Unknown command 0x%02X, ignoring", cmd);
      break;
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
  auto it = discovered_erds_.find(erd);
  bool is_new = (it == discovered_erds_.end());
  std::vector<uint8_t> old_data;
  if (!is_new)
    old_data = it->second;  // capture previous value before overwriting
  discovered_erds_[erd] = data;

  if (is_new) {
    std::string hex = "0x";
    char buf[3];
    for (uint8_t b : data) {
      snprintf(buf, sizeof(buf), "%02X", b);
      hex += buf;
    }
    ESP_LOGI(TAG, "New ERD 0x%04X (%zu B): %s  [total discovered: %zu]",
             erd, data.size(), hex.c_str(), discovered_erds_.size());
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
