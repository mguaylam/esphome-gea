#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstdio>

namespace esphome {
namespace gea {

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
};

class GEAComponent;

// ---------------------------------------------------------------------------
// GEAEntity — base class for all GEA sensor/binary_sensor/select/text_sensor
// ---------------------------------------------------------------------------
class GEAEntity {
 public:
  void set_erd(uint16_t erd) { erd_ = erd; }
  void set_decode(GeaDecodeType decode) { decode_ = decode; }
  void set_bitmask(uint8_t bitmask) { bitmask_ = bitmask; }
  void set_byte_offset(uint8_t offset) { byte_offset_ = offset; }
  void set_parent(GEAComponent *parent) { parent_ = parent; }

  uint16_t get_erd() const { return erd_; }

  // Called by GEAComponent when a matching ERD value arrives
  virtual void on_erd_data(const std::vector<uint8_t> &data) = 0;

 protected:
  uint16_t erd_{0};
  GeaDecodeType decode_{GeaDecodeType::RAW};
  uint8_t bitmask_{0xFF};
  uint8_t byte_offset_{0};
  GEAComponent *parent_{nullptr};

  // Decode the ERD byte vector into a numeric float value
  float decode_as_float(const std::vector<uint8_t> &data) const;

  // Decode the ERD byte vector into a hex string like "0x0100"
  std::string decode_as_hex(const std::vector<uint8_t> &data) const;
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
  // How often to re-send subscribe_all to recover from appliance power-cycles.
  void set_resubscribe_interval(uint32_t ms) { resubscribe_interval_ = ms; }

  // ---- Child entity registration ------------------------------------------
  void register_entity(GEAEntity *entity);

  // ---- Called by writable entities (select, number, etc.) -----------------
  void write_erd(uint16_t erd, const std::vector<uint8_t> &data);

  // ---- Status — usable in YAML lambdas (e.g. for a GEA-connected LED) -----
  // Returns true if a valid packet has been received within the last 30 s.
  bool is_bus_connected() const {
    return last_rx_ms_ != 0 && (millis() - last_rx_ms_) < 30000;
  }

 protected:
  // TX helpers
  void send_packet_(uint8_t dest, const std::vector<uint8_t> &payload);
  void send_ack_();
  void send_subscribe_all_();
  void send_pub_ack_();

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
  // Periodic re-subscribe keeps entity state fresh if the appliance power-cycles.
  uint32_t resubscribe_interval_{60000};
  uint32_t last_subscribe_ms_{0};

  // RX state machine
  enum class RxState { IDLE, IN_PACKET, ESCAPE };
  RxState rx_state_{RxState::IDLE};
  std::vector<uint8_t> rx_buf_;

  // Entity registry
  std::vector<GEAEntity *> entities_;

  // Rolling request ID (incremented on every sent request)
  uint8_t req_id_{1};

  // Timestamp of the last successfully received packet (ms since boot, 0 = none).
  uint32_t last_rx_ms_{0};
};

}  // namespace gea
}  // namespace esphome
