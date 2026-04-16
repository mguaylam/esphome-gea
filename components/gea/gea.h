#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/automation.h"
#include "esphome/components/uart/uart.h"
#include <string>
#include <vector>
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
static constexpr uint8_t CMD_READ_REQUEST    = 0xA0;
static constexpr uint8_t CMD_READ_RESPONSE   = 0xA1;
static constexpr uint8_t CMD_WRITE_REQUEST   = 0xA2;
static constexpr uint8_t CMD_WRITE_RESPONSE  = 0xA3;
static constexpr uint8_t CMD_SUB_ALL_REQUEST = 0xA4;  // subscribe-all (triggers discovery)
static constexpr uint8_t CMD_SUB_ALL_RESPONSE = 0xA5;
static constexpr uint8_t CMD_PUBLICATION     = 0xA6;  // appliance broadcasts all ERD values
static constexpr uint8_t CMD_PUB_ACK         = 0xA7;  // publication acknowledgement
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
  void set_write_erd(uint16_t erd) { write_erd_ = erd; has_write_erd_ = true; }
  void set_decode(GeaDecodeType decode) { decode_ = decode; }
  void set_bitmask(uint8_t bitmask) { bitmask_ = bitmask; }
  void set_byte_offset(uint8_t offset) { byte_offset_ = offset; }
  void set_data_size(uint8_t size) { data_size_ = size; }
  void set_parent(GEAComponent *parent) { parent_ = parent; }

  uint16_t get_erd() const { return erd_; }
  // Returns write_erd if explicitly set, otherwise falls back to erd.
  uint16_t get_write_erd() const { return has_write_erd_ ? write_erd_ : erd_; }

  // Called by GEAComponent when a matching ERD value arrives
  virtual void on_erd_data(const std::vector<uint8_t> &data) = 0;

 protected:
  uint16_t erd_{0};
  uint16_t write_erd_{0};
  bool has_write_erd_{false};
  GeaDecodeType decode_{GeaDecodeType::RAW};
  uint8_t bitmask_{0xFF};
  uint8_t byte_offset_{0};
  uint8_t data_size_{0};  // 0 = auto from decode type
  GEAComponent *parent_{nullptr};

  // Decode the ERD byte vector into a numeric float value
  float decode_as_float(const std::vector<uint8_t> &data) const;

  // Decode the ERD byte vector into a hex string like "0x0100"
  std::string decode_as_hex(const std::vector<uint8_t> &data) const;

  // Encode a uint32 value into bytes using the configured decode type and data_size.
  // Used by writable entities (select, number) to convert a value back to ERD bytes.
  void encode_to_bytes(uint32_t val, std::vector<uint8_t> &out) const;
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
  void set_dest_address(uint8_t addr) { dest_addr_ = addr; auto_detect_ = false; }
  void set_src_address(uint8_t addr) { src_addr_ = addr; }

  // ---- Child entity registration ------------------------------------------
  void register_entity(GEAEntity *entity);

  // ---- Called by writable entities (select, number, etc.) -----------------
  void write_erd(uint16_t erd, const std::vector<uint8_t> &data);

  // ---- Status — usable in YAML lambdas (e.g. for a GEA-connected LED) -----
  // Returns true if a valid packet has been received within the last 30 s.
  bool is_bus_connected() const {
    return last_rx_ms_ != 0 && (millis() - last_rx_ms_) < 30000;
  }

  // ---- Diagnostics — callable from YAML lambdas ---------------------------
  // Logs all discovered ERDs at INFO level. Useful to call on api: on_client_connected
  // so the list appears each time you open the console.
  void log_erds() const;

  // ---- on_erd_change triggers (registered from Python codegen) ------------
  void register_erd_change_trigger(ErdChangeTrigger *trigger) {
    erd_change_triggers_.push_back(trigger);
  }

 protected:
  // TX helpers
  void send_packet_(uint8_t dest, const std::vector<uint8_t> &payload);
  void send_ack_();
  void send_subscribe_all_(uint8_t type = 0x00);
  void send_pub_ack_(uint8_t context, uint8_t request_id);

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

  // Rolling request ID (incremented on every sent request)
  uint8_t req_id_{1};

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
  enum Edge : uint8_t { RISING = 0, FALLING = 1, ANY = 2 };

  ErdChangeTrigger(GEAComponent *parent, uint16_t erd, uint8_t byte_offset,
                   uint8_t bitmask, Edge edge)
      : erd_(erd), byte_offset_(byte_offset), bitmask_(bitmask), edge_(edge) {
    parent->register_erd_change_trigger(this);
  }

  uint16_t get_erd() const { return erd_; }

  // Returns true if the configured edge condition is met between old and new.
  // Caller must ensure old_data is non-empty (no first-seen evaluation).
  bool evaluate(const std::vector<uint8_t> &old_data,
                const std::vector<uint8_t> &new_data) const {
    if (byte_offset_ >= new_data.size() || byte_offset_ >= old_data.size())
      return false;
    uint8_t old_masked = old_data[byte_offset_] & bitmask_;
    uint8_t new_masked = new_data[byte_offset_] & bitmask_;
    switch (edge_) {
      case RISING:  return old_masked == 0 && new_masked != 0;
      case FALLING: return old_masked != 0 && new_masked == 0;
      case ANY:     return old_masked != new_masked;
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
