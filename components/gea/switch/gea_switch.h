#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "../gea.h"

namespace esphome {
namespace gea {

class GEASwitch : public switch_::Switch, public GEAEntity, public Component {
 public:
  // Receive ERD data from the bus — update switch state from the byte at byte_offset_
  void on_erd_data(const std::vector<uint8_t> &data) override {
    if (data.size() <= (size_t) byte_offset_)
      return;
    publish_state(data[byte_offset_] != 0x00);
  }

 protected:
  // Called when the user toggles the switch in HA
  void write_state(bool state) override {
    std::vector<uint8_t> data = {state ? (uint8_t) 0x01 : (uint8_t) 0x00};
    if (parent_)
      parent_->write_erd(erd_, data);
    publish_state(state);
  }
};

}  // namespace gea
}  // namespace esphome
