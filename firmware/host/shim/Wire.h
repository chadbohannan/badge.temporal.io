#pragma once
#include <stdint.h>
#include <stddef.h>
#include "Stream.h"

// Inert I2C bus. U8g2's U8x8lib.cpp includes it; the harness no-ops the byte
// callbacks, so nothing here moves data.
class TwoWire : public Stream {
 public:
  bool begin(int sda = -1, int scl = -1, uint32_t freq = 0) { (void)sda; (void)scl; (void)freq; return true; }
  bool end() { return true; }
  void setTimeOut(uint16_t) {}
  bool setClock(uint32_t) { return true; }
  void beginTransmission(uint8_t) {}
  uint8_t endTransmission(bool = true) { return 0; }
  size_t requestFrom(uint8_t, size_t n, bool = true) { (void)n; return 0; }
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t n) override { return n; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};
extern TwoWire Wire;
