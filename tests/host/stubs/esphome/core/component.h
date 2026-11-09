// Minimal host stubs of the ESPHome API used by uapbridge/uapbridge_esp (tests only)
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include "esphome/core/log.h"
namespace esphome {
extern uint32_t fake_millis;
inline uint32_t millis() { return fake_millis; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
};
template<typename T> class CallbackManager;
template<typename... Ts> class CallbackManager<void(Ts...)> {
 public:
  void add(std::function<void(Ts...)> &&cb) { cbs_.push_back(std::move(cb)); }
  void call(Ts... args) { for (auto &cb : cbs_) cb(args...); }
 private:
  std::vector<std::function<void(Ts...)>> cbs_;
};
class HighFrequencyLoopRequester {
 public:
  void start() {}
  void stop() {}
};
namespace gpio { enum Flags { FLAG_OUTPUT = 1 }; }
class InternalGPIOPin {
 public:
  void setup() {}
  void pin_mode(gpio::Flags) {}
  void digital_write(bool) {}
};
}  // namespace esphome
