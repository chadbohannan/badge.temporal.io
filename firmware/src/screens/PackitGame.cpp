#include "PackitGame.h"

#include <algorithm>

namespace packit {

namespace {

// One pixel-level fall tick every kFallIntervalMs, at kCellH px/row this
// is an ~800ms normal row and ~400ms soft-dropped row (kFallIntervalMs *
// kCellH), matching the wiki's stated pacing assumption.
constexpr uint32_t kFallIntervalMs = 100;

// Once a piece first finds its next row blocked, it doesn't lock instantly
// -- it hovers at its current row for this long, still free to be moved
// left/right (or rotated) like any other tick, before actually locking.
// Standard Tetris "lock delay": without it, a piece landing mid-slide (the
// input that moved it arrives the same tick the floor check locks it)
// reads as the game eating the last input on every placement.
constexpr uint32_t kLockDelayMs = 500;

// Every cube is projected with a real pinhole-camera formula, per vertex
// -- screen = principalPoint + focalLen*(world - camPos)/depth -- not an
// approximated "near box, far box scaled by a flat ratio, shifted by a
// flat lean" (an earlier pass here did exactly that, and it showed: no
// perspective change as pieces moved, and a sign bug in one axis). Both
// the near/far size difference and the lean toward the vanishing point
// fall out of the same single division by depth, the way they do for a
// real camera, rather than being computed as two separate hand-tuned
// knobs.
//
// World units: a "world unit" is one cube's edge length (kCubeWorld = 1
// on every axis, a genuinely equal-sided cube). Columns and rows both sit
// exactly one world unit apart (kPitchXWorld == kPitchYWorld == 1) --
// square pitch, matching the header's kCellW == kCellH == 8 -- so cubes
// tile edge-to-edge with no forced stretch and no gap on either axis.
//
// An earlier version here forced kPitchXWorld to 1.75 (kCellW/kCellH)
// so 9 columns of *stretched* cubes would span the full 128px width at
// kCellW=14. Real math (not a hack) then exposed a genuine conflict: the
// same depth ratio that makes one cube's near/far faces legibly differ in
// size also determines how far a face leans per world unit of lateral
// distance from the camera, and a 15.75-world-unit-wide board multiplies
// that into tens of pixels of lean at the edges -- the noisy diagonal
// mess an actual render showed. Square, unstretched cubes (kPitchXWorld
// = 1) roughly halve the board's total world width, which is what
// actually tames it -- not a smaller kCamDistWorld or a bigger camera
// offset, both of which make it worse, not better.
constexpr float kPitchXWorld = 1.0f;
constexpr float kPitchYWorld = 1.0f;
constexpr float kCubeWorld = 1.0f;

// Camera distance to the near (z=0) face plane, in world units --
// controls the near/far size ratio (camDist/(camDist+1)), currently 0.75
// (a 25% size difference, legible on an 8px cube).
constexpr float kCamDistWorld = 3.0f;
// Focal length is fixed by requiring the near face to project at exactly
// kCellH px per world unit (matching the plain grid's own pitch, so z=0
// reproduces ordinary even spacing) -- kFocalLen/kCamDistWorld == kCellH
// by construction.
constexpr float kFocalLen = static_cast<float>(kCellH) * kCamDistWorld;
// The camera sits centered over the board (in world units) rather than
// off to one corner -- centering minimizes the *maximum* distance from
// the camera to any cell, which is what bounds the worst-case lean (see
// the comment above kPitchXWorld). It does mean the single cell nearest
// board-center shows almost no lean, but every cell -- center included --
// still shows the real near/far *size* difference, which doesn't depend
// on lateral position at all, so no cube ever reads as perfectly flat.
constexpr float kCamXWorld = kBoardW * kPitchXWorld / 2.0f;
constexpr float kCamYWorld = kBoardH * kPitchYWorld / 2.0f;
// A camera offset shifts the whole image, so the principal point (screen
// origin) is chosen to cancel that shift back out at the reference depth
// z=0 -- solved so world (0,0) at z=0 lands exactly at pixel
// (kBoardOffsetX, 0), keeping the board's near face aligned to the plain
// grid regardless of where the camera sits.
constexpr float kScreenOffsetX = static_cast<float>(kBoardOffsetX) + kCellH * kCamXWorld;
constexpr float kScreenOffsetY = static_cast<float>(kCellH) * kCamYWorld;

struct Pt {
  int x, y;
};

// Projects one cube's face (z=0 near, z=1 far) to its 4 screen-space
// corners, in order: top-left, top-right, bottom-right, bottom-left.
// Real per-vertex perspective -- each corner's own (worldX, worldY, z) is
// divided by its own depth -- so a cube near the edge of the frame can
// genuinely keystone (a slight trapezoid), the way a real camera would
// render it, rather than only ever translating and uniformly scaling a
// rectangle.
void projectFace(float colF, float rowF, float z, Pt corners[4]) {
  float wx0 = colF * kPitchXWorld + (kPitchXWorld - kCubeWorld) * 0.5f;
  float wx1 = wx0 + kCubeWorld;
  float wy0 = rowF * kPitchYWorld;
  float wy1 = wy0 + kCubeWorld;
  float depth = kCamDistWorld + z;
  float scale = kFocalLen / depth;
  auto project = [&](float wx, float wy) -> Pt {
    return {static_cast<int>(kScreenOffsetX + scale * (wx - kCamXWorld) + 0.5f),
            static_cast<int>(kScreenOffsetY + scale * (wy - kCamYWorld) + 0.5f)};
  };
  corners[0] = project(wx0, wy0);
  corners[1] = project(wx1, wy0);
  corners[2] = project(wx1, wy1);
  corners[3] = project(wx0, wy1);
}

// u8g2_DrawLine's endpoints are unsigned (u8g2_uint_t); a negative
// projected coordinate (a cube leaning off the top/left edge of the
// screen) would otherwise wrap around to a huge unsigned value instead of
// clipping. u8g2_DrawTriangle takes signed int16_t directly and clips
// correctly, so only line calls need this.
u8g2_uint_t clampCoord(int v) { return static_cast<u8g2_uint_t>(std::max(0, v)); }

void strokeLine(u8g2_t* u, const Pt& a, const Pt& b) {
  u8g2_DrawLine(u, clampCoord(a.x), clampCoord(a.y), clampCoord(b.x), clampCoord(b.y));
}

void fillQuad(u8g2_t* u, const Pt c[4]) {
  u8g2_DrawTriangle(u, c[0].x, c[0].y, c[1].x, c[1].y, c[2].x, c[2].y);
  u8g2_DrawTriangle(u, c[0].x, c[0].y, c[2].x, c[2].y, c[3].x, c[3].y);
}

void strokeQuad(u8g2_t* u, const Pt c[4]) {
  strokeLine(u, c[0], c[1]);
  strokeLine(u, c[1], c[2]);
  strokeLine(u, c[2], c[3]);
  strokeLine(u, c[3], c[0]);
}

// Canonical shapes, one 4x4-bounding-box rotation table per piece, right
// off the standard SRS layout (minus wall-kick data -- the design calls
// for none: a rotation that doesn't fit is simply rejected).
constexpr Cell kShapes[kPieceCount][kRotationStates][kCellsPerPiece] = {
    // I
    {{{0, 1}, {1, 1}, {2, 1}, {3, 1}},
     {{2, 0}, {2, 1}, {2, 2}, {2, 3}},
     {{0, 2}, {1, 2}, {2, 2}, {3, 2}},
     {{1, 0}, {1, 1}, {1, 2}, {1, 3}}},
    // O
    {{{1, 0}, {2, 0}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
    // T
    {{{1, 0}, {0, 1}, {1, 1}, {2, 1}},
     {{1, 0}, {1, 1}, {2, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {1, 2}},
     {{1, 0}, {0, 1}, {1, 1}, {1, 2}}},
    // S
    {{{1, 0}, {2, 0}, {0, 1}, {1, 1}},
     {{1, 0}, {1, 1}, {2, 1}, {2, 2}},
     {{1, 1}, {2, 1}, {0, 2}, {1, 2}},
     {{0, 0}, {0, 1}, {1, 1}, {1, 2}}},
    // Z
    {{{0, 0}, {1, 0}, {1, 1}, {2, 1}},
     {{2, 0}, {1, 1}, {2, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {1, 2}, {2, 2}},
     {{1, 0}, {0, 1}, {1, 1}, {0, 2}}},
    // L
    {{{2, 0}, {0, 1}, {1, 1}, {2, 1}},
     {{1, 0}, {1, 1}, {1, 2}, {2, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {0, 2}},
     {{0, 0}, {1, 0}, {1, 1}, {1, 2}}},
    // J
    {{{0, 0}, {0, 1}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {2, 2}},
     {{1, 0}, {1, 1}, {0, 2}, {1, 2}}},
};

constexpr int8_t kSpawnCol = (kBoardW - kPieceBox) / 2;
constexpr int8_t kSpawnRow = 0;

enum class Mode : uint8_t { kPlay, kMenu };

struct GameState {
  bool well[kBoardH][kBoardW] = {};  // settled cells; well[row][col]
  uint8_t pieceKind = 0;
  uint8_t rotState = 0;
  int8_t anchorCol = 0, anchorRow = 0;
  float subY = 0.0f;  // 0..kCellH-1 px fallen into the current row
  uint32_t rngState = 1;
  uint32_t lastFallMs = 0;
  int rowsCleared = 0;
  bool gameOver = false;
  bool locking = false;        // hovering at the floor, mid lock-delay
  uint32_t lockDeadlineMs = 0;
  Mode mode = Mode::kPlay;
  uint8_t menuSelect = kMenuResume;
};

GameState gState;

uint32_t xorshift32(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

bool cellFree(int col, int row) {
  if (col < 0 || col >= kBoardW || row < 0 || row >= kBoardH) return false;
  return !gState.well[row][col];
}

bool pieceFits(const Cell (&cells)[kCellsPerPiece], int ac, int ar) {
  for (const Cell& c : cells) {
    if (!cellFree(ac + c.dx, ar + c.dy)) return false;
  }
  return true;
}

bool tryMove(int dCol) {
  if (dCol == 0) return false;
  int newCol = gState.anchorCol + dCol;
  if (!pieceFits(kShapes[gState.pieceKind][gState.rotState], newCol, gState.anchorRow)) return false;
  gState.anchorCol = static_cast<int8_t>(newCol);
  return true;
}

bool tryRotate(int dir) {
  uint8_t newState = static_cast<uint8_t>((gState.rotState + kRotationStates + dir) % kRotationStates);
  if (!pieceFits(kShapes[gState.pieceKind][newState], gState.anchorCol, gState.anchorRow)) return false;
  gState.rotState = newState;
  return true;
}

// Resets the spawn cells/anchor for `kind` and checks whether it fits --
// callers set gState.gameOver when it doesn't (the classic top-out rule).
bool spawnPiece(uint8_t kind) {
  gState.pieceKind = kind;
  gState.rotState = 0;
  gState.anchorCol = kSpawnCol;
  gState.anchorRow = kSpawnRow;
  gState.subY = 0.0f;
  return pieceFits(kShapes[kind][0], gState.anchorCol, gState.anchorRow);
}

void lockPiece() {
  for (const Cell& c : kShapes[gState.pieceKind][gState.rotState]) {
    gState.well[gState.anchorRow + c.dy][gState.anchorCol + c.dx] = true;
  }
}

bool rowFull(int row) {
  for (int col = 0; col < kBoardW; col++) {
    if (!gState.well[row][col]) return false;
  }
  return true;
}

// Removes row `row` and pulls every row above it down one step -- the
// standard 2D Tetris row-clear rule. A fresh empty row appears at row 0.
void clearRow(int row) {
  for (int r = row; r > 0; r--) {
    for (int col = 0; col < kBoardW; col++) gState.well[r][col] = gState.well[r - 1][col];
  }
  for (int col = 0; col < kBoardW; col++) gState.well[0][col] = false;
}

void clearFullRows() {
  for (int row = 0; row < kBoardH; row++) {
    if (rowFull(row)) {
      clearRow(row);
      gState.rowsCleared++;
    }
  }
}

// Locks the current piece in place, clears any full rows, and spawns the
// next one -- the actual placement, run once the lock-delay window (see
// kLockDelayMs) has elapsed with the piece still resting on something.
void finishLock() {
  gState.subY = 0.0f;
  gState.locking = false;
  lockPiece();
  clearFullRows();
  uint8_t kind = static_cast<uint8_t>(xorshift32(gState.rngState) % kPieceCount);
  if (!spawnPiece(kind)) {
    // Top-out: there's no live game left to resume, so drop straight into
    // the pause menu (Resume hidden -- see gameStep/gameDraw's gameOver
    // checks) instead of leaving the player stuck on a frozen board with
    // no way to start over short of leaving the screen entirely.
    gState.gameOver = true;
    gState.mode = Mode::kMenu;
    gState.menuSelect = kMenuNewGame;
  }
}

// Advances `cur` by one menu item in `dir` (+1/-1), skipping Resume when
// there's no game left to resume back into.
uint8_t stepMenuItem(uint8_t cur, int dir) {
  uint8_t next = static_cast<uint8_t>((cur + kMenuItemCount + dir) % kMenuItemCount);
  if (gState.gameOver && next == kMenuResume) {
    next = static_cast<uint8_t>((next + kMenuItemCount + dir) % kMenuItemCount);
  }
  return next;
}

// One pixel of fall motion. Crossing a full cell boundary is the only
// point collision/locking is checked -- the piece's logical row is
// otherwise untouched, so movement/rotation stay exactly as discrete as
// the well grid itself.
void stepFallPixel(uint32_t now) {
  // Check whether the *next* row is still open before animating any
  // farther into it, not after -- checking only once subY had already
  // accumulated a full kCellH let the piece visually ease into a row it
  // could never actually enter, then snap back up by nearly a full cell
  // the instant it locked one row short of where it had just appeared to
  // be. Checking up front means the piece stops the moment it's actually
  // resting on something, with no overshoot to unwind.
  int newRow = gState.anchorRow + 1;
  if (!pieceFits(kShapes[gState.pieceKind][gState.rotState], gState.anchorCol, newRow)) {
    // Blocked: start (or continue) the lock-delay hover instead of
    // locking outright. A move/rotate applied earlier this same tick (see
    // gameStep) may have already slid the piece off the ledge it was
    // blocked on -- pieceFits above re-checks that fresh every call, so
    // that case simply falls through to normal falling below instead of
    // ever setting `locking`.
    if (!gState.locking) {
      gState.locking = true;
      gState.lockDeadlineMs = now + kLockDelayMs;
    }
    return;
  }
  gState.locking = false;
  gState.subY += 1.0f;
  if (gState.subY >= kCellH) {
    gState.subY -= kCellH;
    gState.anchorRow = static_cast<int8_t>(newRow);
  }
}

// Shared by gameBegin() and the pause menu's New Game action.
void resetGame(uint32_t now) {
  gState = GameState{};
  gState.rngState = now ? now : 1;
  gState.lastFallMs = now;
  uint8_t kind = static_cast<uint8_t>(xorshift32(gState.rngState) % kPieceCount);
  spawnPiece(kind);  // the well is empty, so this always fits
}

// Un-pausing resets the fall clock to `now` so time spent in the menu
// doesn't read as elapsed fall time.
void resumePlay(uint32_t now) {
  gState.mode = Mode::kPlay;
  gState.lastFallMs = now;
}

}  // namespace

// Always opens on the pause menu (Resume pre-highlighted) rather than
// dropping straight into a falling piece -- resetGame() has already spawned
// one, so Resume picks play back up right where a mid-game pause would.
void gameBegin(uint32_t now) {
  resetGame(now);
  gState.mode = Mode::kMenu;
  gState.menuSelect = kMenuResume;
}

bool gameStep(const GameInput& in, uint32_t now) {
  if (gState.mode == Mode::kMenu) {
    if (in.menuUp) {
      gState.menuSelect = stepMenuItem(gState.menuSelect, -1);
    } else if (in.menuDown) {
      gState.menuSelect = stepMenuItem(gState.menuSelect, 1);
    }
    // A is normally the back button (cancel out to Resume), but there's
    // nothing to resume once the game is over -- game-over's menu has no
    // Resume item to land on, so A does nothing there.
    if (in.a) {
      if (!gState.gameOver) resumePlay(now);
    } else if (in.rotateRight) {
      switch (gState.menuSelect) {
        case kMenuResume: if (!gState.gameOver) resumePlay(now); break;
        case kMenuNewGame: resetGame(now); break;
        case kMenuExit: return true;
      }
    }
    return false;
  }

  if (gState.gameOver) return false;  // shouldn't happen: finishLock opens the menu the same tick it tops out

  if (in.moveLeft) tryMove(-1);
  else if (in.moveRight) tryMove(1);

  if (in.rotateLeft) tryRotate(-1);
  else if (in.rotateRight) tryRotate(1);

  if (in.a) {
    gState.mode = Mode::kMenu;
    gState.menuSelect = kMenuResume;
    return false;
  }

  uint32_t interval = kFallIntervalMs;
  while (now - gState.lastFallMs >= interval) {
    gState.lastFallMs += interval;
    stepFallPixel(now);
    if (in.softDrop) stepFallPixel(now);  // 2x fall rate while held
    if (gState.gameOver) break;
  }

  // Lock-delay timeout: checked against `now` directly rather than folded
  // into the kFallIntervalMs-driven loop above, so the half-second grace
  // period is wall-clock time, not tied to the fall-tick rate.
  if (gState.locking && !gState.gameOver && now >= gState.lockDeadlineMs) {
    finishLock();
  }
  return false;
}

GameStatus gameStatus() {
  return {gState.rowsCleared,   gState.gameOver,
          gState.pieceKind,     gState.anchorCol,
          gState.anchorRow,     gState.rotState,
          gState.mode == Mode::kMenu, gState.menuSelect};
}

namespace {

// The far face *and* the 4 side faces connecting it to the near face --
// a real cube has 6 solid faces, and every one of the 4 sides needs its
// own filled quad, not just an outline edge. Drawing the sides as bare
// lines (an earlier version here did exactly that) leaves the space
// between a cube's near and far edges transparent: it only ever looked
// solid when some *neighboring* cube's near face happened to sit behind
// it by coincidence, and when no neighbor was there, you could see
// straight through the cube's own unfilled side to whatever was behind
// it. That's not an occlusion bug to fix with draw order -- it's a
// missing face. Every cube is now a genuinely closed solid regardless of
// its neighbors.
//
// Callers must draw every cube's cubeBack() across the whole board
// *before* any cubeNear() (see gameDraw), not paired cube-by-cube --
// with the camera off to the upper-left, a cube's back geometry (far
// face + sides) can still poke into a neighboring cell's screen-space
// territory, and that neighbor's own opaque near face, drawn in the
// second pass, is what cleanly caps the overlap.
//
// Only 2 of the 4 side faces are ever front-facing (visible) from a
// single off-axis camera -- the 2 the far face leans *toward* (e.g. top
// and left, if the far face shifts up-and-left). The other 2 face away
// from the camera entirely and would never be visible on a real solid;
// their fill ends up self-occluded by this same cube's own near face
// either way, but drawing their outlines added stray edges with no
// silhouette meaning, which is what read as messy/unoccluded-looking
// rather than as a clean closed cube. Backface culling -- standard
// practice for exactly this -- skips them outright instead of drawing
// and relying on later geometry to paint over them.
void cubeBack(u8g2_t* u, float colF, float rowF) {
  Pt near[4], far[4];
  projectFace(colF, rowF, 0.0f, near);
  projectFace(colF, rowF, 1.0f, far);
  u8g2_SetDrawColor(u, 0);
  fillQuad(u, far);
  u8g2_SetDrawColor(u, 1);

  // Edge/side index i connects corner i to corner (i+1)%4: 0=top,
  // 1=right, 2=bottom, 3=left (see projectFace's corner order). The far
  // face leans toward the camera, so comparing far/near corner 0 (top-
  // left) tells us which vertical and horizontal side is front-facing.
  bool leansUp = far[0].y < near[0].y;
  bool leansLeft = far[0].x < near[0].x;
  int visibleEdges[2] = {leansUp ? 0 : 2, leansLeft ? 3 : 1};

  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    if (i == visibleEdges[0] || i == visibleEdges[1]) strokeLine(u, far[i], far[j]);
  }
  for (int e = 0; e < 2; e++) {
    int i = visibleEdges[e];
    int j = (i + 1) % 4;
    Pt side[4] = {near[i], near[j], far[j], far[i]};
    u8g2_SetDrawColor(u, 0);
    fillQuad(u, side);
    u8g2_SetDrawColor(u, 1);
    strokeQuad(u, side);
  }
}

// The near (front) face: opaque black fill, white border, drawn in the
// second pass so it occludes whatever far-face/edge geometry -- this
// cube's own, or a neighbor's -- falls within its own footprint.
void cubeNear(u8g2_t* u, float colF, float rowF) {
  Pt near[4];
  projectFace(colF, rowF, 0.0f, near);
  u8g2_SetDrawColor(u, 0);
  fillQuad(u, near);
  u8g2_SetDrawColor(u, 1);
  strokeQuad(u, near);
}

}  // namespace

void gameDraw(u8g2_t* u, uint32_t now) {
  (void)now;
  u8g2_SetDrawColor(u, 1);

  // Dotted vertical rules flush against the board's left/right edges --
  // zero margin, one column outside kBoardOffsetX..kBoardOffsetX+kBoardW*
  // kCellW so they mark the border without a gap or touching a cell.
  {
    constexpr int kLeftRuleX = kBoardOffsetX - 1;
    constexpr int kRightRuleX = kBoardOffsetX + kBoardW * kCellW;
    for (int y = 0; y < kBoardH * kCellH; y += 2) {
      u8g2_DrawPixel(u, kLeftRuleX, y);
      u8g2_DrawPixel(u, kRightRuleX, y);
    }
  }

  float pieceRow = gState.anchorRow + gState.subY / kCellH;

  // Pass 1: every cube's back geometry (far face + 4 side faces),
  // board-wide, before any near face -- see cubeBack's comment for why
  // this can't be per-cube.
  for (int row = 0; row < kBoardH; row++) {
    for (int col = 0; col < kBoardW; col++) {
      if (gState.well[row][col]) cubeBack(u, static_cast<float>(col), static_cast<float>(row));
    }
  }
  if (!gState.gameOver) {
    for (const Cell& c : kShapes[gState.pieceKind][gState.rotState]) {
      cubeBack(u, static_cast<float>(gState.anchorCol + c.dx), pieceRow + c.dy);
    }
  }

  // Pass 2: every near face, opaque, on top -- caps the front and cleans
  // up any overlap between adjacent cubes' back geometry from pass 1.
  for (int row = 0; row < kBoardH; row++) {
    for (int col = 0; col < kBoardW; col++) {
      if (gState.well[row][col]) cubeNear(u, static_cast<float>(col), static_cast<float>(row));
    }
  }
  if (!gState.gameOver) {
    for (const Cell& c : kShapes[gState.pieceKind][gState.rotState]) {
      cubeNear(u, static_cast<float>(gState.anchorCol + c.dx), pieceRow + c.dy);
    }
  }

  // Pause menu: the board is still drawn frozen underneath, with a
  // cleared-and-framed panel and the 3 options on top -- A opened this
  // (and backs out of it), rotateRight confirms the highlighted line,
  // menuUp/menuDown move the selection.
  if (gState.mode == Mode::kMenu) {
    constexpr u8g2_uint_t kPanelX = 24, kPanelY = 12, kPanelW = 80, kPanelH = 40;
    u8g2_SetDrawColor(u, 0);
    u8g2_DrawBox(u, kPanelX, kPanelY, kPanelW, kPanelH);
    u8g2_SetDrawColor(u, 1);
    u8g2_DrawFrame(u, kPanelX, kPanelY, kPanelW, kPanelH);
    u8g2_SetFont(u, u8g2_font_6x10_tr);
    static const char* const kLabels[kMenuItemCount] = {"Resume", "New Game", "Exit"};
    int shownRow = 0;
    for (int i = 0; i < kMenuItemCount; i++) {
      if (gState.gameOver && i == kMenuResume) continue;  // nothing left to resume
      u8g2_uint_t ty = kPanelY + 12 + shownRow * 11;
      u8g2_DrawStr(u, kPanelX + 8, ty, i == gState.menuSelect ? ">" : " ");
      u8g2_DrawStr(u, kPanelX + 16, ty, kLabels[i]);
      shownRow++;
    }
  }
}

bool wellFilled(int col, int row) {
  if (col < 0 || col >= kBoardW || row < 0 || row >= kBoardH) return false;
  return gState.well[row][col];
}

void pieceCellWorld(int i, int8_t& col, int8_t& row) {
  const Cell& c = kShapes[gState.pieceKind][gState.rotState][i];
  col = static_cast<int8_t>(gState.anchorCol + c.dx);
  row = static_cast<int8_t>(gState.anchorRow + c.dy);
}

void debugSetWellCell(int col, int row, bool filled) {
  if (col < 0 || col >= kBoardW || row < 0 || row >= kBoardH) return;
  gState.well[row][col] = filled;
}

}  // namespace packit
