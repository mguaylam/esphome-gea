#pragma once
#include <cstdint>
#include "esphome/core/hal.h"
namespace esphome {
namespace setup_priority { static const float BUS = 1000.0f; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
};
}  // namespace esphome
