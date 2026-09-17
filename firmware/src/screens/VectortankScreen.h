#pragma once
#include "Screen.h"

// ─── Vectortank: Battlezone-style wireframe vector demo ────────────────────
//
// Wireframe boxes + point particles, first-person driving camera, in plain
// C++/float math. Started as a MicroPython app to see whether the badge
// could handle vector rendering at all; a head-to-head benchmark showed
// native C++ rendering ~55x faster than the Python path (once a real
// oled::drawLine() bug — unclamped screen coords wrapping to huge unsigned
// values in u8g2's Bresenham loop — was fixed), so this native
// implementation is the one that ships. Deliberately plain float math with
// no SIMD/esp-dsp: the ESP32-S3's PIE "vector" instructions are int8/int16
// integer-only and don't help float 3D math — the real win here is the
// hardware FPU plus removing MicroPython's interpreter overhead.

class VectortankScreen : public Screen {
 public:
  void onEnter(GUIManager& gui) override;
  void render(oled& d, GUIManager& gui) override;
  void handleInput(const Inputs& inputs, int16_t cursorX, int16_t cursorY,
                   GUIManager& gui) override;
  ScreenId id() const override { return kScreenVectortank; }
  bool showCursor() const override { return false; }

 private:
  uint32_t lastFrameMs_ = 0;
};
