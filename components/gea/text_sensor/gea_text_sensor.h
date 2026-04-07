#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "../gea.h"
#include <map>

namespace esphome {
namespace gea {

class GEATextSensor : public text_sensor::TextSensor, public GEAEntity, public Component {
 public:
  void add_option(uint32_t key, const std::string &label) {
    options_[key] = label;
  }

  void on_erd_data(const std::vector<uint8_t> &data) override {
    if (data.empty())
      return;

    if (decode_ == GeaDecodeType::ASCII) {
      // Trim trailing nulls and publish as a string
      size_t len = data.size();
      while (len > 0 && data[len - 1] == 0x00)
        len--;
      publish_state(std::string(data.begin(), data.begin() + len));
    } else if (!options_.empty()) {
      // Decode as number, look up in options map
      auto key = (uint32_t) decode_as_float(data);
      auto it = options_.find(key);
      if (it != options_.end()) {
        publish_state(it->second);
      } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%02X", (unsigned) key);
        publish_state(std::string(buf));
      }
    } else if (decode_ == GeaDecodeType::RAW) {
      // Publish as hex string, e.g. "0x0100"
      publish_state(decode_as_hex(data));
    } else {
      // Publish as decimal string of the numeric decoded value
      float val = decode_as_float(data);
      char buf[32];
      // Use integer formatting when the value has no fractional part
      if (val == (float)(int32_t)val) {
        snprintf(buf, sizeof(buf), "%d", (int32_t)val);
      } else {
        snprintf(buf, sizeof(buf), "%.2f", val);
      }
      publish_state(std::string(buf));
    }
  }

 private:
  std::map<uint32_t, std::string> options_;
};

}  // namespace gea
}  // namespace esphome
