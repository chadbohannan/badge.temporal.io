#pragma once
// Host IS31FL3731 driver: the five methods LEDmatrix.cpp calls, over an
// in-memory 8-frame PWM buffer that mirrors the chip's frame registers. The
// harness reads the frame that displayFrame() selected to draw the panel.
#include <stdint.h>
#include "Wire.h"

#define ISSI_ADDR_DEFAULT 0x74

class Adafruit_IS31FL3731 {
 public:
  static constexpr int kFrames = 8;
  static constexpr int kWidth = 16;   // the chip's native 16x9 matrix, as in the real library
  static constexpr int kHeight = 9;

  Adafruit_IS31FL3731(uint8_t x = 16, uint8_t y = 9) { (void)x; (void)y; }
  bool begin(uint8_t addr = ISSI_ADDR_DEFAULT, TwoWire* wire = &Wire);
  void setFrame(uint8_t frame);
  void displayFrame(uint8_t frame);
  void clear();
  void drawPixel(int16_t x, int16_t y, uint16_t color);

  // Harness side.
  // 16x9 PWM values of one frame, index x + y * 16.
  static const uint8_t* framePixels(uint8_t frame);
  static uint8_t shownFrame();
  static uint32_t writeCount();

 private:
  uint8_t writeFrame_ = 0;
};
