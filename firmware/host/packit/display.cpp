#include "display.h"

#include "png.h"

using namespace packit;

namespace display {
namespace {

u8g2_t gU8g2;

}  // namespace

void init() {
  // Same geometry and font engine as the badge, with the byte/gpio
  // callbacks pointed at u8g2's built-in no-ops.
  u8g2_Setup_ssd1306_128x64_noname_f(&gU8g2, U8G2_R0, u8x8_byte_empty, u8x8_dummy_cb);
}

OledFrame renderOled(uint32_t now) {
  u8g2_ClearBuffer(&gU8g2);
  gameDraw(&gU8g2, now);
  const uint8_t* buf = u8g2_GetBufferPtr(&gU8g2);
  OledFrame f(kOledW * kOledH);
  for (int y = 0; y < kOledH; y++) {
    for (int x = 0; x < kOledW; x++) {
      f[y * kOledW + x] = ((buf[(y / 8) * kOledW + x] >> (y & 7)) & 1) ? 255 : 0;
    }
  }
  return f;
}

void renderMatrix(Matrix out) { gameMatrix(out); }

void snapshot(const std::string& prefix, uint32_t now) {
  OledFrame f = renderOled(now);
  const int w = kOledW * kOledScale, h = kOledH * kOledScale;
  std::vector<uint8_t> img(w * h);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) img[y * w + x] = f[(y / kOledScale) * kOledW + x / kOledScale];
  }
  png::write(prefix + "-oled.png", w, h, img);

  Matrix px;
  renderMatrix(px);
  const int mw = kMatrixCols * kLedScale, mh = kMatrixRows * kLedScale;
  std::vector<uint8_t> m(mw * mh, 0);
  for (int row = 0; row < kMatrixRows; row++) {
    for (int col = 0; col < kMatrixCols; col++) {
      int cx = col * kLedScale, cy = row * kLedScale;
      uint8_t v = px[row * kMatrixCols + col];
      for (int dy = 1; dy < kLedScale - 1; dy++) {
        for (int dx = 1; dx < kLedScale - 1; dx++) m[(cy + dy) * mw + cx + dx] = v;
      }
    }
  }
  png::write(prefix + "-matrix.png", mw, mh, m);
}

}  // namespace display
