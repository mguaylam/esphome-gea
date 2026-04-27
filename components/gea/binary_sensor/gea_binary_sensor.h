#pragma once

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "../gea.h"

namespace esphome {
namespace gea {

class GEABinarySensor : public binary_sensor::BinarySensor, public GEAEntity, public Component {
 public:
  void set_inverted(bool inv) { inverted_ = inv; }

  void on_erd_data(const std::vector<uint8_t> &data) override {
    if (data.size() <= (size_t)byte_offset_)
      return;
    bool state = (data[byte_offset_] & bitmask_) != 0;
    publish_state(inverted_ ? !state : state);
  }

  void dump_config() override {
    LOG_BINARY_SENSOR("", "GEA Binary Sensor", this);
    dump_erd_config("binary_sensor.gea");
    if (inverted_)
      ESP_LOGCONFIG("binary_sensor.gea", "  Inverted: yes");
  }

 private:
  bool inverted_{false};
};

}  // namespace gea
}  // namespace esphome
