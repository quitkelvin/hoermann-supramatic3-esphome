#pragma once
#include "esphome/core/component.h"
namespace esphome {
namespace text_sensor {
class TextSensor {
 public:
  void publish_state(const std::string &s) { state = s; count++; }
  std::string state;
  int count = 0;
};
}  // namespace text_sensor
}  // namespace esphome
