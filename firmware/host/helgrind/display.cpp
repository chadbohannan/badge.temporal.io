#include "display.h"

#include <cstring>

#include "png.h"

using namespace helgrind;

namespace display {
namespace {

u8g2_t gU8g2;

}  // namespace

void init() {
  // Same geometry and font engine as the badge, with the byte/gpio
  // callbacks pointed at u8g2's built-in no-ops.
  u8g2_Setup_ssd1306_128x64_noname_f(&gU8g2, U8G2_R0, u8x8_byte_empty, u8x8_dummy_cb);
}

u8g2_t* u8g2() { return &gU8g2; }

OledFrame unpack(const u8g2_t* u, int w, int h) {
  const uint8_t* buf = u8g2_GetBufferPtr(const_cast<u8g2_t*>(u));
  OledFrame f(w * h);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      f[y * w + x] = ((buf[(y / 8) * w + x] >> (y & 7)) & 1) ? 255 : 0;
    }
  }
  return f;
}

OledFrame renderOled(uint32_t now) {
  u8g2_ClearBuffer(&gU8g2);
  gameDraw(&gU8g2, now);
  return unpack(&gU8g2, kOledW, kOledH);
}

void renderMinimap(Minimap out, uint32_t now) { gameMinimap(out, now); }

void snapshot(const std::string& prefix, uint32_t now) {
  OledFrame f = renderOled(now);
  const int w = kOledW * kOledScale, h = kOledH * kOledScale;
  std::vector<uint8_t> img(w * h);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) img[y * w + x] = f[(y / kOledScale) * kOledW + x / kOledScale];
  }
  png::write(prefix + "-oled.png", w, h, img);

  Minimap px;
  renderMinimap(px, now);
  const int mw = kWorldCols * kLedScale, mh = kWorldRows * kLedScale;
  std::vector<uint8_t> m(mw * mh, 0);
  for (int r = 0; r < kRoomCount; r++) {
    int cx = (r % kWorldCols) * kLedScale, cy = (r / kWorldCols) * kLedScale;
    for (int dy = 1; dy < kLedScale - 1; dy++) {
      for (int dx = 1; dx < kLedScale - 1; dx++) m[(cy + dy) * mw + cx + dx] = px[r];
    }
  }
  png::write(prefix + "-matrix.png", mw, mh, m);
}

}  // namespace display
