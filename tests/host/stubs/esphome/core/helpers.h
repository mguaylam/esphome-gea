#pragma once
#include <cstdint>
namespace esphome {
uint32_t random_uint32();
class HighFrequencyLoopRequester {
 public:
  void start() {}
  void stop() {}
};
}  // namespace esphome
