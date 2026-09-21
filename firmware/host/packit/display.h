#pragma once
// The badge's two displays as the host sees them: a buffer-only u8g2
// SSD1306 the game draws into, and the 8x8 ambient LED matrix showing the
// 2-digit score. Same role as firmware/host/helgrind/display.h.
#include <cstdint>
#include <string>
#include <vector>

#include "PackitGame.h"

namespace display {

constexpr int kOledW = 128, kOledH = 64;
constexpr int kOledScale = 6;  // snapshot / window magnification
constexpr int kLedScale = 16;  // pixels per LED in snapshots / the window

// One byte per pixel, 0 or 255, row-major kOledW x kOledH.
using OledFrame = std::vector<uint8_t>;
// One brightness per LED, row-major packit::kMatrixCols x kMatrixRows.
using Matrix = uint8_t[packit::kMatrixCols * packit::kMatrixRows];

// Set up the shared u8g2 instance. Call once before anything else.
void init();

// Render the game's current state into a frame / the LED matrix.
OledFrame renderOled(uint32_t now);
void renderMatrix(Matrix out);

// Write <prefix>-oled.png (scaled kOledScale) and <prefix>-matrix.png
// (kLedScale per LED, brightness as grey, 1px gap between cells).
void snapshot(const std::string& prefix, uint32_t now);

}  // namespace display
