#pragma once
namespace esphome {
template<typename... Ts> class Trigger {
 public:
  void trigger(const Ts &...) {}
};
}  // namespace esphome
