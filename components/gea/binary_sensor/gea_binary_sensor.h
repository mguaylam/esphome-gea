#pragma once

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "../gea.h"

namespace esphome {
namespace gea {

class GEABinarySensor : public binary_sensor::BinarySensor, public GEAEntity, public Component {
 public:
  void on_erd_data(const std::vector<uint8_t> &data) override {
    if (data.size() <= (size_t) byte_offset_)
      return;
    bool state = (data[byte_offset_] & bitmask_) != 0;
    publish_state(state);
  }
};

}  // namespace gea
}  // namespace esphome
