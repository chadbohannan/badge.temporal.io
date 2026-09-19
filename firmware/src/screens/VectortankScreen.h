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
//
// Controls are an FPS walker, not a Battlezone-style independent turret:
// the badge has exactly one 2-axis stick and 4 buttons (semantic
// confirm/cancel/etc. are aliases over those same 4, not extra inputs),
// which isn't enough budget for body + turret + aimable arc + fire + back
// as separate controls. So aim direction == move direction, and the fired
// shot's arc is fixed (not player-aimed) — see kShotLift/kGravity below —
// which removes the conflict an aimable arc would have had with the stick's
// forward/back axis.
//
// Button layout: stick = look (X) + walk (Y). Up = fire, down = back.
// Left/right (x/b) = strafe. Fire/back deliberately read the RAW up/down
// edges rather than the confirm/cancel semantic layer: confirm/cancel are
// aliased onto down+right as a pair (Inputs::updateSemanticAliases), and
// right is claimed by strafe here, so going through that layer would either
// fire or pop the screen on every strafe-right depending on the user's
// confirm/cancel-swap setting.

class VectortankScreen : public Screen {
 public:
  void onEnter(GUIManager& gui) override;
  void onExit(GUIManager& gui) override;
  void render(oled& d, GUIManager& gui) override;
  void handleInput(const Inputs& inputs, int16_t cursorX, int16_t cursorY,
                   GUIManager& gui) override;
  ScreenId id() const override { return kScreenVectortank; }
  bool showCursor() const override { return false; }
  // Fire/back read raw up/down button edges (see class comment above), the
  // same physical UP button SleepService's force-deep-sleep long-press
  // watches — without this, holding UP to fire for 5s would also sleep the
  // badge mid-game.
  bool suppressesForceSleep() const override { return true; }

 private:
  uint32_t lastFrameMs_ = 0;
  uint32_t lastFireMs_ = 0;
  uint32_t lastRadarMs_ = 0;
  bool paused_ = false;
  // True once the player's health has hit 0 — a permanent variant of
  // paused_ that back can't dismiss (there's no "resume" from 0 HP), only
  // the pause menu's New Game/Exit items. See the pause-menu overlay
  // comment block in VectortankScreen.cpp.
  bool gameOver_ = false;
  uint8_t pauseCursor_ = 0;
  // Latches the stick's last nav direction while paused so a held tilt
  // doesn't repeat-fire every frame; 0 once the stick returns to neutral.
  int8_t pauseStickDir_ = 0;
};
