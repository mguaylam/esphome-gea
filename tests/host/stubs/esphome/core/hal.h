#pragma once
#include <cstdint>
namespace esphome {
uint32_t millis();
void delay(uint32_t ms);
void yield();
}  // namespace esphome
