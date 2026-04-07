#pragma once

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "../gea.h"

namespace esphome {
namespace gea {

class GEAButton : public button::Button, public Component {
 public:
  void set_erd(uint16_t erd) { erd_ = erd; }
  void set_parent(GEAComponent *parent) { parent_ = parent; }
  void add_payload_byte(uint8_t b) { payload_.push_back(b); }

 protected:
  void press_action() override {
    if (parent_)
      parent_->write_erd(erd_, payload_);
  }

  uint16_t erd_{0};
  GEAComponent *parent_{nullptr};
  std::vector<uint8_t> payload_;
};

}  // namespace gea
}  // namespace esphome
