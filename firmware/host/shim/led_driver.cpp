// Host IS31FL3731: the frame registers LEDmatrix.cpp drives, kept in memory.
// The real chip holds 8 frames of 16x9 PWM values and shows one of them.

#include "Adafruit_IS31FL3731.h"
#include <string.h>

namespace {
constexpr int kPixels = Adafruit_IS31FL3731::kWidth * Adafruit_IS31FL3731::kHeight;
uint8_t gFrames[Adafruit_IS31FL3731::kFrames][kPixels];
uint8_t gShown = 0;
uint8_t gWriteFrame = 0;  // the real library tracks one write frame per driver object
uint32_t gWrites = 0;
}  // namespace

bool Adafruit_IS31FL3731::begin(uint8_t, TwoWire*) {
  memset(gFrames, 0, sizeof gFrames);
  gWriteFrame = 0;
  gShown = 0;
  gWrites++;
  return true;
}

void Adafruit_IS31FL3731::setFrame(uint8_t frame) { gWriteFrame = frame & 7; }

void Adafruit_IS31FL3731::displayFrame(uint8_t frame) {
  gShown = frame > 7 ? 0 : frame;
  gWrites++;
}

void Adafruit_IS31FL3731::clear() {
  memset(gFrames[gWriteFrame], 0, kPixels);
  gWrites++;
}

void Adafruit_IS31FL3731::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
  if (color > 255) color = 255;
  gFrames[gWriteFrame][x + y * kWidth] = (uint8_t)color;
  gWrites++;
}

const uint8_t* Adafruit_IS31FL3731::framePixels(uint8_t frame) { return gFrames[frame & 7]; }
uint8_t Adafruit_IS31FL3731::shownFrame() { return gShown; }
uint32_t Adafruit_IS31FL3731::writeCount() { return gWrites; }
