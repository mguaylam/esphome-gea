#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/optional.h"
#include "esphome/core/automation.h"
#include "esphome/components/uart/uart.h"
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <cstdint>
#include <cstdio>

namespace esphome {
namespace gea {

class ErdChangeTrigger;  // defined below GEAComponent

// ---------------------------------------------------------------------------
// GEA3 protocol framing constants
// ---------------------------------------------------------------------------
static constexpr uint8_t GEA_STX = 0xE2;  // Start of frame
static constexpr uint8_t GEA_ETX = 0xE3;  // End of frame
static constexpr uint8_t GEA_ESC = 0xE0;  // Escape prefix
static constexpr uint8_t GEA_ACK = 0xE1;  // Acknowledgement (single-byte)
static constexpr uint16_t GEA_CRC_SEED = 0x1021;
static constexpr uint8_t GEA_BROADCAST_ADDR = 0xFF;

// ---------------------------------------------------------------------------
// GEA3 ERD API command bytes
// ---------------------------------------------------------------------------
static constexpr uint8_t CMD_READ_REQUEST = 0xA0;
static constexpr uint8_t CMD_READ_RESPONSE = 0xA1;
static constexpr uint8_t CMD_WRITE_REQUEST = 0xA2;
static constexpr uint8_t CMD_WRITE_RESPONSE = 0xA3;
static constexpr uint8_t CMD_SUB_ALL_REQUEST = 0xA4;  // subscribe-all (triggers discovery)
static constexpr uint8_t CMD_SUB_ALL_RESPONSE = 0xA5;
static constexpr uint8_t CMD_PUBLICATION = 0xA6;       // appliance broadcasts all ERD values
static constexpr uint8_t CMD_PUB_ACK = 0xA7;           // publication acknowledgement
static constexpr uint8_t CMD_SUB_HOST_STARTUP = 0xA8;  // appliance announces it just came online

// ---------------------------------------------------------------------------
// Decode types for ERD data interpretation
// ---------------------------------------------------------------------------
enum GeaDecodeType {
  UINT8,
  UINT16_BE,
  UINT16_LE,
  UINT32_BE,
  UINT32_LE,
  INT8,
  INT16_BE,
  INT16_LE,
  INT32_BE,
  INT32_LE,
  BOOL,
  RAW,
  ASCII,
};

class GEAComponent;

// ---------------------------------------------------------------------------
// GEAEntity — base class for all GEA sensor/binary_sensor/select/text_sensor
// ---------------------------------------------------------------------------
class GEAEntity {
 public:
  void set_erd(uint16_t erd) { erd_ = erd; }
  void set_write_erd(uint16_t erd) { write_erd_ = erd; }
  void set_decode(GeaDecodeType decode) { decode_ = decode; }
  void set_bitmask(uint8_t bitmask) { bitmask_ = bitmask; }
  void set_byte_offset(uint8_t offset) { byte_offset_ = offset; }
  void set_data_size(uint8_t size) { data_size_ = size; }
  void set_multiplier(float m) { multiplier_ = m; }
  void set_offset(float o) { offset_ = o; }
  void set_parent(GEAComponent *parent) { parent_ = parent; }

  uint16_t get_erd() const { return erd_; }
  // Returns write_erd if explicitly set, otherwise falls back to erd.
  uint16_t get_write_erd() const { return write_erd_.value_or(erd_); }

  // Called by GEAComponent when a matching ERD value arrives
  virtual void on_erd_data(const std::vector<uint8_t> &data) = 0;

 protected:
  uint16_t erd_{0};
  optional<uint16_t> write_erd_;
  GeaDecodeType decode_{GeaDecodeType::RAW};
  uint8_t bitmask_{0xFF};
  uint8_t byte_offset_{0};
  uint8_t data_size_{0};  // 0 = auto from decode type
  float multiplier_{1.0f};
  float offset_{0.0f};
  GEAComponent *parent_{nullptr};

  // Decode the ERD byte vector into a numeric float value, applying
  // multiplier/offset (output = raw * multiplier + offset).
  float decode_as_float(const std::vector<uint8_t> &data) const;

  // Decode the ERD byte vector into a hex string like "0x0100"
  std::string decode_as_hex(const std::vector<uint8_t> &data) const;

  // Encode a uint32 value into bytes using the configured decode type and data_size.
  // Used by writable entities (select, number) to convert a value back to ERD bytes.
  void encode_to_bytes(uint32_t val, std::vector<uint8_t> &out) const;

 public:
  // Common ERD info dump shared by every entity's dump_config().
  void dump_erd_config(const char *tag) const {
    if (write_erd_.has_value())
      ESP_LOGCONFIG(tag, "  ERD: 0x%04X (write 0x%04X)", erd_, *write_erd_);
    else
      ESP_LOGCONFIG(tag, "  ERD: 0x%04X", erd_);
    if (byte_offset_ != 0)
      ESP_LOGCONFIG(tag, "  Byte offset: %u", byte_offset_);
    if (bitmask_ != 0xFF)
      ESP_LOGCONFIG(tag, "  Bitmask: 0x%02X", bitmask_);
    if (multiplier_ != 1.0f || offset_ != 0.0f)
      ESP_LOGCONFIG(tag, "  Scaling: y = x * %.4f + %.4f", multiplier_, offset_);
  }
};

// ---------------------------------------------------------------------------
// PendingRequest — a single outgoing ERD-API request awaiting a response.
// The body holds the packet payload AFTER [CMD][REQ_ID] (e.g. for a write:
// [ERD_H][ERD_L][size][data...]).  The expected response command is
// (cmd | 0x01): READ_REQUEST→READ_RESPONSE, WRITE_REQUEST→WRITE_RESPONSE,
// SUB_ALL_REQUEST→SUB_ALL_RESPONSE.
// ---------------------------------------------------------------------------
struct PendingRequest {
  uint8_t cmd;
  uint8_t req_id;
  uint8_t dest;
  std::vector<uint8_t> body;
  uint8_t retries_left;
  uint32_t sent_at_ms;
};

// ---------------------------------------------------------------------------
// GEAComponent — the hub component; owns UART, discovery, and subscription
// ---------------------------------------------------------------------------
class GEAComponent : public uart::UARTDevice, public Component {
 public:
  // ---- ESPHome lifecycle --------------------------------------------------
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  // ---- Configuration setters (called from Python codegen) -----------------
  // dest_address is optional: if not called, auto-detect is used instead.
  void set_dest_address(uint8_t addr) {
    dest_addr_ = addr;
    auto_detect_ = false;
  }
  void set_src_address(uint8_t addr) { src_addr_ = addr; }

  // ---- Child entity registration ------------------------------------------
  void register_entity(GEAEntity *entity);

  // ---- Called by writable entities (select, number, etc.) -----------------
  void write_erd(uint16_t erd, const std::vector<uint8_t> &data);

  // ---- Explicit read — enqueues a single ERD read request -----------------
  void read_erd(uint16_t erd);

  // ---- Status — usable in YAML lambdas (e.g. for a GEA-connected LED) -----
  // Returns true if a valid packet has been received within the last 30 s.
  bool is_bus_connected() const { return last_rx_ms_ != 0 && (millis() - last_rx_ms_) < 30000; }

  // ---- Diagnostics — callable from YAML lambdas ---------------------------
  // Logs all discovered ERDs at INFO level. Useful to call on api: on_client_connected
  // so the list appears each time you open the console.
  void log_erds() const;

  // Counters for bus health diagnostics. Expose via lambda in a template sensor:
  //   sensor:
  //     - platform: template
  //       name: "GEA CRC Errors"
  //       entity_category: diagnostic
  //       lambda: 'return id(gea_hub).get_crc_errors();'
  uint32_t get_rx_bytes() const { return rx_byte_count_; }
  uint32_t get_crc_errors() const { return crc_errors_; }
  uint32_t get_tx_retries() const { return tx_retries_; }
  uint32_t get_dropped_requests() const { return dropped_requests_; }

  // ---- on_erd_change triggers (registered from Python codegen) ------------
  void register_erd_change_trigger(ErdChangeTrigger *trigger) { erd_change_triggers_.push_back(trigger); }

 protected:
  // TX helpers
  void send_packet_(uint8_t dest, const std::vector<uint8_t> &payload);
  void send_ack_();
  void send_subscribe_all_(uint8_t type = 0x00);
  void send_pub_ack_(uint8_t context, uint8_t request_id);

  // Request queue / retry machinery
  uint8_t next_req_id_();
  void enqueue_request_(uint8_t cmd, std::vector<uint8_t> body);
  bool has_inflight_cmd_(uint8_t cmd) const;
  void transmit_pending_();
  void finish_pending_();
  bool response_matches_pending_(uint8_t response_cmd, uint8_t req_id) const;

  // RX state machine
  void process_rx_byte_(uint8_t byte);
  void process_packet_(const std::vector<uint8_t> &pkt);
  void dispatch_erd_(uint16_t erd, const std::vector<uint8_t> &data);
  void log_discovery_(uint16_t erd, const std::vector<uint8_t> &data);

  // Protocol utilities
  static uint16_t crc16_(const uint8_t *data, size_t len);
  static std::vector<uint8_t> escape_(const std::vector<uint8_t> &raw);

  // Configuration
  // When auto_detect_ is true, dest_addr_ starts as broadcast and is updated
  // from the SRC field of the first valid packet received from the appliance.
  bool auto_detect_{true};
  uint8_t dest_addr_{GEA_BROADCAST_ADDR};
  uint8_t src_addr_{0xBB};

  // RX state machine
  enum class RxState { IDLE, IN_PACKET, ESCAPE };
  RxState rx_state_{RxState::IDLE};

  // Subscription state machine:
  //   SUBSCRIBING — retrying subscribe-all until the appliance acknowledges.
  //   SUBSCRIBED  — subscription active; retained periodically.
  enum class SubState { SUBSCRIBING, SUBSCRIBED };
  SubState sub_state_{SubState::SUBSCRIBING};
  std::vector<uint8_t> rx_buf_;

  // Entity registry
  std::vector<GEAEntity *> entities_;

  // Rolling request ID (incremented per user-originated request, not per packet)
  uint8_t req_id_{1};

  // Request machinery — serializes outgoing requests so only one is in flight
  // at a time, retries on timeout, and drops once exhausted.  Unsolicited
  // frames (ACK, publication ACK) bypass the queue.
  static constexpr uint32_t REQUEST_TIMEOUT_MS = 250;
  static constexpr uint8_t REQUEST_MAX_RETRIES = 10;
  std::deque<PendingRequest> request_queue_;
  PendingRequest pending_{};
  bool pending_active_{false};

  // Timestamp of the last successfully received packet (ms since boot, 0 = none).
  uint32_t last_rx_ms_{0};

  // Tracks previous bus state to detect appliance reconnection.
  bool was_connected_{false};

  // Timestamp of the last subscribe-all sent. Used to pace retries (SUBSCRIBING)
  // and keep-alive sends (SUBSCRIBED).
  uint32_t sub_retry_ms_{0};

  // Raw byte counter — reported periodically so we can confirm UART is alive.
  uint32_t rx_byte_count_{0};
  uint32_t last_stats_ms_{0};

  // Diagnostics counters exposed via get_*() accessors.
  uint32_t crc_errors_{0};
  uint32_t tx_retries_{0};
  uint32_t dropped_requests_{0};

  // ERD discovery map: ERD address → most recently received data bytes.
  // Populated on first publication of each ERD; updated silently thereafter.
  std::map<uint16_t, std::vector<uint8_t>> discovered_erds_;

  // User-configured on_erd_change triggers (see gea.on_erd_change in YAML).
  std::vector<ErdChangeTrigger *> erd_change_triggers_;
};

// ---------------------------------------------------------------------------
// ErdChangeTrigger — fires on ERD publication when a specified edge transition
// occurs in data[byte_offset] masked by bitmask.  Self-registers with its
// parent GEAComponent at construction.
//
// Semantics:
//   rising  : (old & mask) == 0  &&  (new & mask) != 0
//   falling : (old & mask) != 0  &&  (new & mask) == 0
//   any     : (old & mask) != (new & mask)
//
// The first publication of an ERD after boot establishes a silent baseline
// (no trigger fires), so reboots mid-cycle don't produce spurious events.
// ---------------------------------------------------------------------------
class ErdChangeTrigger : public Trigger<> {
 public:
  // Note: prefixed names avoid clashing with Arduino.h macros (RISING/FALLING).
  enum Edge : uint8_t { EDGE_RISING = 0, EDGE_FALLING = 1, EDGE_ANY = 2 };

  ErdChangeTrigger(GEAComponent *parent, uint16_t erd, uint8_t byte_offset, uint8_t bitmask, Edge edge)
      : erd_(erd), byte_offset_(byte_offset), bitmask_(bitmask), edge_(edge) {
    parent->register_erd_change_trigger(this);
  }

  uint16_t get_erd() const { return erd_; }

  // Returns true if the configured edge condition is met between old and new.
  // Caller must ensure old_data is non-empty (no first-seen evaluation).
  bool evaluate(const std::vector<uint8_t> &old_data, const std::vector<uint8_t> &new_data) const {
    if (byte_offset_ >= new_data.size() || byte_offset_ >= old_data.size())
      return false;
    uint8_t old_masked = old_data[byte_offset_] & bitmask_;
    uint8_t new_masked = new_data[byte_offset_] & bitmask_;
    switch (edge_) {
      case EDGE_RISING: return old_masked == 0 && new_masked != 0;
      case EDGE_FALLING: return old_masked != 0 && new_masked == 0;
      case EDGE_ANY: return old_masked != new_masked;
    }
    return false;
  }

 protected:
  uint16_t erd_;
  uint8_t byte_offset_;
  uint8_t bitmask_;
  Edge edge_;
};

}  // namespace gea
}  // namespace esphome
