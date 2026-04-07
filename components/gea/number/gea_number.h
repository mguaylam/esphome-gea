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

 protected:
  // Called when the user changes the value in HA
  void control(float value) override {
    auto val = (uint32_t) value;
    std::vector<uint8_t> data;
    encode_to_bytes(val, data);
    if (parent_)
      parent_->write_erd(get_write_erd(), data);
    publish_state(value);
  }
};

}  // namespace gea
}  // namespace esphome
