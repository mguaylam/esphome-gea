#pragma once
#include <cstdint>
namespace esphome {
struct ESPPreferenceObject {
  template<typename T> bool save(const T *) { return true; }
  template<typename T> bool load(T *) { return false; }
};
struct ESPPreferences {
  template<typename T> ESPPreferenceObject make_preference(uint32_t, bool = false) { return {}; }
};
extern ESPPreferences *global_preferences;
}  // namespace esphome
