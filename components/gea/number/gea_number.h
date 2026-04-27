#pragma once

#include "esphome/core/component.h"
#include "esphome/components/number/number.h"
#include "../gea.h"

namespace esphome {
namespace gea {

class GEANumber : public number::Number, public GEAEntity, public Component {
 public:
  // Receive ERD data from the bus — update displayed value
  void on_erd_data(const std::vector<uint8_t> &data) override {
    if (data.empty())
      return;
    float value = decode_as_float(data);
    publish_state(value);
  }

  void dump_config() override {
    static const char *const TAG = "number.gea";
    LOG_NUMBER("", "GEA Number", this);
    dump_erd_config(TAG);
  }

 protected:
  // Called when the user changes the value in HA
  void control(float value) override {
    // Reverse the multiplier/offset transform applied on read so the wire
    // value matches what the appliance expects.
    float raw = (multiplier_ != 0.0f) ? ((value - offset_) / multiplier_) : 0.0f;
    // Guard against negative-raw → wrap-around when decode type is unsigned.
    if (raw < 0.0f && (decode_ == UINT8 || decode_ == UINT16_BE || decode_ == UINT16_LE || decode_ == UINT32_BE ||
                       decode_ == UINT32_LE)) {
      ESP_LOGW("number.gea", "value %.3f → negative raw on unsigned ERD 0x%04X; clamping to 0", value, get_write_erd());
      raw = 0.0f;
    }
    uint32_t val = (uint32_t)(int32_t)raw;
    std::vector<uint8_t> data;
    encode_to_bytes(val, data);
    if (parent_)
      parent_->write_erd(get_write_erd(), data);
    publish_state(value);
  }
};

}  // namespace gea
}  // namespace esphome
