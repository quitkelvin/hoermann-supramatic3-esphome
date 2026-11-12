#pragma once
#include "esphome/core/component.h"
namespace esphome {
namespace sensor {
class Sensor {
 public:
  void publish_state(float s) { state = s; count++; }
  float state = -1;
  int count = 0;
};
}  // namespace sensor
}  // namespace esphome
