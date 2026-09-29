// Reads the two panels the firmware draws to: the SSD1306 frame buffer and the
// IS31FL3731 frame the matrix driver last displayed. Both are read at yield
// points, never in the middle of a draw.

#include "harness.h"
#include "png.h"

#include "Adafruit_IS31FL3731.h"
#include "hardware/oled.h"

#include <sys/stat.h>
#include <cstdio>

extern oled badgeDisplay;

namespace harness {

namespace {
// mkdir -p
void makeDirs(const std::string& path) {
  for (size_t i = 1; i <= path.size(); i++) {
    if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
  }
}
}  // namespace

OledFrame readOled() {
  OledFrame f(kOledW * kOledH, 0);
  // U8g2 page layout: 8 pages of 128 bytes, bit 0 is the top row of the page.
  const uint8_t* buf = badgeDisplay.getBufferPtr();
  if (!buf) return f;
  for (int y = 0; y < kOledH; y++) {
    for (int x = 0; x < kOledW; x++) {
      f[y * kOledW + x] = ((buf[(y / 8) * kOledW + x] >> (y & 7)) & 1) ? 255 : 0;
    }
  }
  return f;
}

std::vector<uint8_t> readMatrix() {
  // LEDmatrix::setPixel maps a logical pixel (x, y) to chip position
  // (hwX, hwY) = (y, 7 - x) once main() has called setFlipped(true), and in that
  // state the logical pixel is what a viewer sees. Undo that mapping here.
  const uint8_t* px = Adafruit_IS31FL3731::framePixels(Adafruit_IS31FL3731::shownFrame());
  std::vector<uint8_t> m(kMatrixN * kMatrixN, 0);
  for (int y = 0; y < kMatrixN; y++) {
    for (int x = 0; x < kMatrixN; x++) {
      m[y * kMatrixN + x] = px[y + (kMatrixN - 1 - x) * Adafruit_IS31FL3731::kWidth];
    }
  }
  return m;
}

std::string oledText(const OledFrame& f) {
  std::string s;
  s.reserve((kOledW + 1) * kOledH);
  for (int y = 0; y < kOledH; y++) {
    for (int x = 0; x < kOledW; x++) s += f[y * kOledW + x] ? '#' : '.';
    s += '\n';
  }
  return s;
}

std::string matrixText(const std::vector<uint8_t>& m) {
  std::string s;
  for (int y = 0; y < kMatrixN; y++) {
    for (int x = 0; x < kMatrixN; x++) {
      const int v = m[y * kMatrixN + x];
      s += v == 0 ? '.' : (char)('0' + (v * 9 + 254) / 255);
    }
    s += '\n';
  }
  return s;
}

bool writeSnapshot(const std::string& prefix, bool* windowMismatch) {
  constexpr int kOledScale = 6, kLedScale = 16;
  makeDirs(gOptions.outDir);
  const std::string base = gOptions.outDir + "/" + prefix;

  const OledFrame f = readOled();
  const int w = kOledW * kOledScale, h = kOledH * kOledScale;
  std::vector<uint8_t> img(w * h);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) img[y * w + x] = f[(y / kOledScale) * kOledW + x / kOledScale];
  }
  bool ok = png::write(base + "-oled.png", w, h, img);

  const std::vector<uint8_t> m = readMatrix();
  const int mw = kMatrixN * kLedScale;
  std::vector<uint8_t> mi(mw * mw, 0);
  for (int row = 0; row < kMatrixN; row++) {
    for (int col = 0; col < kMatrixN; col++) {
      const uint8_t v = m[row * kMatrixN + col];
      for (int dy = 1; dy < kLedScale - 1; dy++) {
        for (int dx = 1; dx < kLedScale - 1; dx++) {
          mi[(row * kLedScale + dy) * mw + col * kLedScale + dx] = v;
        }
      }
    }
  }
  ok = png::write(base + "-matrix.png", mw, mw, mi) && ok;

  if (FILE* t = fopen((base + "-oled.txt").c_str(), "w")) {
    fputs(oledText(f).c_str(), t);
    fclose(t);
  } else {
    ok = false;
  }
  if (FILE* t = fopen((base + "-matrix.txt").c_str(), "w")) {
    fputs(matrixText(m).c_str(), t);
    fclose(t);
  } else {
    ok = false;
  }
  // With a window open, also capture it and check it shows these same frames.
  if (!windowCapture(base)) {
    ok = false;
    if (windowMismatch) *windowMismatch = true;
  }
  return ok;
}

}  // namespace harness
