#pragma once
#include "Screen.h"

// ─── Helgrind placeholder screen ────────────────────────────────────────────
//
// Stand-in tile for the badge's next game, replacing the DOOM app slot
// (removed for being unplayable on the OLED's 1bpp display — no gradient
// support meant DoomGeneric's dithered output read as noise). "Helgrind"
// is only a working title; this screen exists so the menu entry and its
// icon have somewhere to land while the real game is built.

class HelgrindScreen : public Screen {
 public:
  void render(oled& d, GUIManager& gui) override;
  void handleInput(const Inputs& inputs, int16_t cursorX, int16_t cursorY,
                   GUIManager& gui) override;
  ScreenId id() const override { return kScreenHelgrind; }
  bool showCursor() const override { return false; }
};
