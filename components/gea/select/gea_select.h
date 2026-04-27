#pragma once

#include "esphome/core/component.h"
#include "esphome/components/select/select.h"
#include "../gea.h"
#include <map>

namespace esphome {
namespace gea {

class GEASelect : public select::Select, public GEAEntity, public Component {
 public:
  // Called from Python codegen to populate the options map.
  void add_option(uint32_t key, const std::string &label) { options_[key] = label; }

  // Receive ERD data from the bus: decode the value, look it up in options_.
  void on_erd_data(const std::vector<uint8_t> &data) override {
    if (data.empty())
      return;
    auto key = (uint32_t)decode_as_float(data);
    auto it = options_.find(key);
    if (it != options_.end()) {
      publish_state(it->second);
    } else {
      ESP_LOGD("select.gea", "Unmapped value 0x%02X for ERD 0x%04X — ignoring", (unsigned)key, erd_);
    }
  }

  void dump_config() override {
    static const char *const TAG = "select.gea";
    LOG_SELECT("", "GEA Select", this);
    dump_erd_config(TAG);
    ESP_LOGCONFIG(TAG, "  Options: %zu", options_.size());
  }

 protected:
  // Called when the user selects a new option in HA.
  void control(const std::string &value) override {
    for (auto &pair : options_) {
      if (pair.second == value) {
        std::vector<uint8_t> data;
        encode_to_bytes(pair.first, data);
        if (parent_)
          parent_->write_erd(get_write_erd(), data);
        publish_state(value);
        return;
      }
    }
    ESP_LOGW("gea.select", "Unknown option %s for ERD 0x%04X", value.c_str(), erd_);
  }

 private:
  std::map<uint32_t, std::string> options_;
};

}  // namespace gea
}  // namespace esphome
