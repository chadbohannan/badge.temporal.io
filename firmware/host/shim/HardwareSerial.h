#pragma once
#include "Stream.h"

// Serial and ESP_LOG* go to stdout.
class HardwareSerial : public Stream {
 public:
  void begin(unsigned long baud = 115200) { (void)baud; }
  void end() {}
  size_t setRxBufferSize(size_t n) { return n; }
  size_t setTxBufferSize(size_t n) { return n; }
  void setTxTimeoutMs(uint32_t) {}
  void setDebugOutput(bool) {}
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  using Print::write;
  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  operator bool() const { return true; }
  int availableForWrite() { return 4096; }
};

extern HardwareSerial Serial;
