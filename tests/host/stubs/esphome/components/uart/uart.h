#pragma once
#include "esphome/core/component.h"
namespace esphome {
namespace uart {
enum UARTParityOptions { UART_CONFIG_PARITY_NONE = 0 };
class UARTComponent {
 public:
  void set_baud_rate(uint32_t) {}
  void set_data_bits(uint8_t) {}
  void set_parity(UARTParityOptions) {}
  void set_stop_bits(uint8_t) {}
  void load_settings(bool) {}
};
extern std::deque<uint8_t> fake_rx;
extern std::vector<std::vector<uint8_t>> fake_tx;
class UARTDevice {
 public:
  int available() { return (int) fake_rx.size(); }
  bool read_byte(uint8_t *b) { if (fake_rx.empty()) return false; *b = fake_rx.front(); fake_rx.pop_front(); return true; }
  void write_byte(uint8_t) {}   // sync break
  void write_array(const uint8_t *d, size_t n) { fake_tx.emplace_back(d, d + n); }
  void flush() {}
 protected:
  UARTComponent fake_parent_;
  UARTComponent *parent_ = &fake_parent_;
};
}  // namespace uart
}  // namespace esphome
