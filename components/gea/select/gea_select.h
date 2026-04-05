#pragma once

#include "esphome/core/component.h"
#include "esphome/components/select/select.h"
#include "../gea.h"
#include <map>

namespace esphome {
namespace gea {

class GEASelect : public select::Select, public GEAEntity, public Component {
 public:
  // Called from Python codegen to populate the options map.
  void add_option(uint32_t key, const std::string &label) {
    options_[key] = label;
  }

  // Receive ERD data from the bus: decode the value, look it up in options_.
  void on_erd_data(const std::vector<uint8_t> &data) override {
    if (data.empty())
      return;
    auto key = (uint32_t) decode_as_float(data);
    auto it = options_.find(key);
    if (it != options_.end()) {
      publish_state(it->second);
    } else {
      // Unknown value — publish as decimal string for visibility.
      char buf[16];
      snprintf(buf, sizeof(buf), "0x%02X", (unsigned) key);
      publish_state(std::string(buf));
    }
  }

 protected:
  // Called when the user selects a new option in HA.
  void control(const std::string &value) override {
    for (auto &pair : options_) {
      if (pair.second == value) {
        std::vector<uint8_t> data;
        encode_value_(pair.first, data);
        if (parent_)
          parent_->write_erd(erd_, data);
        publish_state(value);
        return;
      }
    }
    ESP_LOGW("gea.select", "Unknown option %s for ERD 0x%04X", value.c_str(), erd_);
  }

 private:
  // Encode a uint32 key back to bytes using the configured decode type.
  void encode_value_(uint32_t val, std::vector<uint8_t> &out) const {
    switch (decode_) {
      case GeaDecodeType::UINT16_BE:
      case GeaDecodeType::INT16_BE:
        out.push_back((val >> 8) & 0xFF);
        out.push_back(val & 0xFF);
        break;
      case GeaDecodeType::UINT16_LE:
      case GeaDecodeType::INT16_LE:
        out.push_back(val & 0xFF);
        out.push_back((val >> 8) & 0xFF);
        break;
      case GeaDecodeType::UINT32_BE:
      case GeaDecodeType::INT32_BE:
        out.push_back((val >> 24) & 0xFF);
        out.push_back((val >> 16) & 0xFF);
        out.push_back((val >> 8) & 0xFF);
        out.push_back(val & 0xFF);
        break;
      case GeaDecodeType::UINT32_LE:
      case GeaDecodeType::INT32_LE:
        out.push_back(val & 0xFF);
        out.push_back((val >> 8) & 0xFF);
        out.push_back((val >> 16) & 0xFF);
        out.push_back((val >> 24) & 0xFF);
        break;
      default:  // uint8, int8, bool, raw
        out.push_back((uint8_t) val);
        break;
    }
  }

  std::map<uint32_t, std::string> options_;
};

}  // namespace gea
}  // namespace esphome
