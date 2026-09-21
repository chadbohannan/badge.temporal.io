#pragma once
#include "Screen.h"
#include "JoyRamp.h"

// ─── PACKIT: flat-board Tetris-style block-packing game ────────────────────
//
// The 7 classic tetrominoes fall down a flat 9x8 grid viewed face-on, each
// cell rendered as a real perspective cube at a fixed depth (see
// PackitGame.h/.cpp). No save/persistence, no LED matrix use.
//
// Controls: the analog stick's horizontal axis moves the piece left/right,
// converted from continuous joystick reads to the game core's discrete
// one-cell-per-press moves via the same JoyRamp helper GridMenuScreen uses
// for cursor navigation; its vertical axis only matters while the pause
// menu is open, where it moves the menu selection. X rotates the piece
// CCW, B rotates it CW, Y (held) soft-drops at 2x fall rate. A opens the
// in-game pause menu (Resume/New Game/Exit) and also backs out of it;
// because Y is the physical UP button, this screen suppresses the
// hold-UP force-sleep the same way Helgrind and Vectortank do.
//
// This class is only the adapter between the badge (Inputs, millis, oled)
// and the game itself, which lives in PackitGame.{h,cpp} with no firmware
// dependencies beyond u8g2's C API, so it also builds and runs on the host
// (firmware/host/packit/).

class PackitScreen : public Screen {
 public:
  void onEnter(GUIManager& gui) override;
  void render(oled& d, GUIManager& gui) override;
  void handleInput(const Inputs& inputs, int16_t cursorX, int16_t cursorY,
                   GUIManager& gui) override;
  ScreenId id() const override { return kScreenPackit; }
  bool showCursor() const override { return false; }
  bool suppressesForceSleep() const override { return true; }

 private:
  JoyRamp joyRamp_;
};
