#pragma once
#include "Screen.h"

// ─── Helgrind: Norse-flavoured top-down action-RPG ─────────────────────────
//
// A fixed 8x8 world of hand-authored rooms (see HelgrindWorld.h), each a
// single non-scrolling 16x6 tile screen drawn Zelda/Ultima-style from
// overhead. Rooms connect on every edge; the story is a strictly linear
// chain of seven artifacts (weapons, amulets, scrolls) whose order is
// enforced by what each pickup will let you take, never by locked doors.
// Collecting the last one wakes the warden in the Helgrind Gate room.
//
// Controls reuse the badge's four semantic buttons over the same physical
// pad Vectortank documents: stick = walk (also sets facing). A (down)
// opens the pause menu, B (right) swings/fires the selected weapon, X
// (left) cycles weapons, Y (up) is the context action — talk/buy next to
// an NPC, otherwise re-read the latest scroll. Because Y is the physical
// UP button, this screen suppresses the hold-UP force-sleep the same way
// Vectortank does.
//
// The ambient LED matrix is claimed for the duration of play as a
// one-LED-per-room minimap: the player's room is a steady pixel and the
// room holding the next artifact in the chain blinks at 1 Hz.
//
// Progress (room, health, silver, held artifacts, weapon, looted caches)
// persists in the "badge_helgrind" NVS namespace so a power-off mid-quest
// picks up where it left off; dying respawns at the current room's entry
// point with full health.
//
// This class is only the adapter between the badge (Inputs, millis, oled,
// LED matrix, NVS) and the game itself, which lives in HelgrindGame.{h,cpp}
// with no firmware dependencies so it also builds and runs on the host.

class HelgrindScreen : public Screen {
 public:
  void onEnter(GUIManager& gui) override;
  void onExit(GUIManager& gui) override;
  void render(oled& d, GUIManager& gui) override;
  void handleInput(const Inputs& inputs, int16_t cursorX, int16_t cursorY,
                   GUIManager& gui) override;
  ScreenId id() const override { return kScreenHelgrind; }
  bool showCursor() const override { return false; }
  bool suppressesForceSleep() const override { return true; }

 private:
  uint32_t lastMinimapMs_ = 0;
};
