#pragma once
#include <stdint.h>
#include <stddef.h>
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
#define MSBFIRST 1
#define LSBFIRST 0
class SPISettings {
 public:
  SPISettings(uint32_t = 1000000, uint8_t = MSBFIRST, uint8_t = SPI_MODE0) {}
};
class SPIClass {
 public:
  void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) {}
  void end() {}
  void beginTransaction(SPISettings) {}
  void endTransaction() {}
  uint8_t transfer(uint8_t v) { return v; }
};
extern SPIClass SPI;
