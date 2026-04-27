#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "../gea.h"

namespace esphome {
namespace gea {

class GEASwitch : public switch_::Switch, public GEAEntity, public Component {
 public:
  void set_on_value(uint8_t v) { on_value_ = v; }
  void set_off_value(uint8_t v) { off_value_ = v; }

  // Receive ERD data from the bus — update switch state
  void on_erd_data(const std::vector<uint8_t> &data) override {
    if (data.size() <= (size_t)byte_offset_)
      return;
    publish_state(data[byte_offset_] == on_value_);
  }

  void dump_config() override {
    static const char *const TAG = "switch.gea";
    LOG_SWITCH("", "GEA Switch", this);
    dump_erd_config(TAG);
    ESP_LOGCONFIG(TAG, "  Payload on/off: 0x%02X / 0x%02X", on_value_, off_value_);
  }

 protected:
  // Called when the user toggles the switch in HA
  void write_state(bool state) override {
    std::vector<uint8_t> data = {state ? on_value_ : off_value_};
    if (parent_)
      parent_->write_erd(get_write_erd(), data);
    publish_state(state);
  }

 private:
  uint8_t on_value_{0x01};
  uint8_t off_value_{0x00};
};

}  // namespace gea
}  // namespace esphome
