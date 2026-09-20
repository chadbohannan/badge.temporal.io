#pragma once
// The two badge displays as the host sees them: a buffer-only u8g2 SSD1306
// the game draws into, and the 8x8 minimap the game fills. Both the script
// runner and the SDL window read frames through here, and snapshots are
// written from the same unpacked bitmaps the window shows.
#include <cstdint>
#include <string>
#include <vector>

#include "HelgrindGame.h"

namespace display {

constexpr int kOledW = 128, kOledH = 64;
constexpr int kOledScale = 4;   // snapshot / window magnification
constexpr int kLedScale = 32;   // pixels per LED in snapshots / the window

// One byte per pixel, 0 or 255, row-major kOledW x kOledH.
using OledFrame = std::vector<uint8_t>;
// One brightness per room, indexed row * 8 + col as the world is.
using Minimap = uint8_t[helgrind::kRoomCount];

// Set up the shared u8g2 instance. Call once before anything else.
void init();
u8g2_t* u8g2();

// Render the game's current state into a frame / the minimap.
OledFrame renderOled(uint32_t now);
void renderMinimap(Minimap out, uint32_t now);

// Unpack any u8g2 page buffer (vertical bytes, LSB = top) to one byte per
// pixel, so other buffers (the SDL help panel) can share the blit path.
OledFrame unpack(const u8g2_t* u, int w, int h);

// Write <prefix>-oled.png (scaled kOledScale) and <prefix>-matrix.png
// (kLedScale per LED, brightness as grey, 1px gap between cells).
void snapshot(const std::string& prefix, uint32_t now);

}  // namespace display
