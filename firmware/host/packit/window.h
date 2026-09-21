#pragma once
// SDL2 viewer for PackitGame's core state -- lets the design's occlusion
// approach (farthest-to-nearest opaque layers, wireframe reserved for the
// falling piece) be looked at and played with before any of it is ported
// to real u8g2/OLED drawing code. Host-only: nothing here is shared with
// firmware, and there is no PackitScreen yet for this to stand in for.
#include <string>

namespace window {

// Opens a window and runs until closed. Returns a process exit code.
int play();

}  // namespace window
