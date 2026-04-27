#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "../gea.h"

namespace esphome {
namespace gea {

class GEASensor : public sensor::Sensor, public GEAEntity, public Component {
 public:
  void on_erd_data(const std::vector<uint8_t> &data) override {
    if (data.empty())
      return;
    float value = decode_as_float(data);
    publish_state(value);
  }
  void dump_config() override {
    LOG_SENSOR("", "GEA Sensor", this);
    dump_erd_config("sensor.gea");
  }
};

}  // namespace gea
}  // namespace esphome
