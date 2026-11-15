#pragma once
#include <cstddef>
#include <cstdint>
// Host stub: the UART forwards to the bus model owned by the test harness.
size_t host_uart_available();
bool host_uart_read(uint8_t *b);
void host_uart_write(const uint8_t *data, size_t len);
namespace esphome {
namespace uart {
class UARTDevice {
 public:
  size_t available() { return host_uart_available(); }
  bool read_byte(uint8_t *b) { return host_uart_read(b); }
  void write_array(const uint8_t *data, size_t len) { host_uart_write(data, len); }
  void write_byte(uint8_t b) { host_uart_write(&b, 1); }
};
}  // namespace uart
}  // namespace esphome
