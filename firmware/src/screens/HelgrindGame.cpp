#include "HelgrindGame.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace helgrind {

namespace {

// ── Screen geometry the game needs to know about ─────────────────────────
//
// The shell draws nothing of its own, so the game owns the footer band
// too. These mirror OLEDLayout so the footer lines up with the rest of
// the firmware's screens; they're restated here to keep this file free
// of firmware headers (it also builds on the host — see
// firmware/test/host/helgrind/).
constexpr int kScreenW = 128;
constexpr int kScreenH = 64;
constexpr int kFooterTopY = 54;
constexpr int kFooterTextBaseY = 62;
const uint8_t* const kFont = u8g2_font_smallsimple_tr;  // oled's FONT_TINY

// u8g2's C API takes unsigned coordinates, so anything that can poke off
// the top or left edge (the sword swing does) is clipped here first —
// the same Liang-Barsky pass oled::drawLine does.
bool clipLine(int& x0, int& y0, int& x1, int& y1) {
  const float xmax = kScreenW - 1, ymax = kScreenH - 1;
  const float dx = static_cast<float>(x1 - x0), dy = static_cast<float>(y1 - y0);
  const float p[4] = {-dx, dx, -dy, dy};
  const float q[4] = {static_cast<float>(x0), xmax - x0, static_cast<float>(y0), ymax - y0};
  float t0 = 0.0f, t1 = 1.0f;
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0.0f) {
      if (q[i] < 0.0f) return false;
      continue;
    }
    float t = q[i] / p[i];
    if (p[i] < 0.0f) {
      if (t > t1) return false;
      if (t > t0) t0 = t;
    } else {
      if (t < t0) return false;
      if (t < t1) t1 = t;
    }
  }
  int nx0 = static_cast<int>(x0 + t0 * dx + 0.5f), ny0 = static_cast<int>(y0 + t0 * dy + 0.5f);
  int nx1 = static_cast<int>(x0 + t1 * dx + 0.5f), ny1 = static_cast<int>(y0 + t1 * dy + 0.5f);
  x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
  return true;
}

void drawLine(u8g2_t* u, int x0, int y0, int x1, int y1) {
  if (!clipLine(x0, y0, x1, y1)) return;
  u8g2_DrawLine(u, x0, y0, x1, y1);
}

// Rounded boxes/frames with oled's radius clamp, so r=0 and tiny shapes
// render exactly as they did through the wrapper.
int clampRadius(int w, int h, int r) {
  if (r < 0) r = 0;
  int maxR = (w < h ? w : h) / 2 - 1;
  if (r > maxR) r = maxR < 0 ? 0 : maxR;
  return r;
}

void drawRBox(u8g2_t* u, int x, int y, int w, int h, int r) {
  if (w <= 0 || h <= 0) return;
  r = clampRadius(w, h, r);
  if (r <= 0) u8g2_DrawBox(u, x, y, w, h);
  else u8g2_DrawRBox(u, x, y, w, h, r);
}

void drawRFrame(u8g2_t* u, int x, int y, int w, int h, int r) {
  if (w <= 0 || h <= 0) return;
  r = clampRadius(w, h, r);
  if (r <= 0) u8g2_DrawFrame(u, x, y, w, h);
  else u8g2_DrawRFrame(u, x, y, w, h, r);
}


// ── Layout ───────────────────────────────────────────────────────────────
//
// The room fills the full 128px width and 48px (6 tiles) of height, sat
// just under the top of the screen; there's no status header because a
// 16x6 tile grid plus the standard footer band already spends the whole
// display. Everything in the room is in "play space" (0..127, 0..47) and
// gets kPlayTop added at draw time only.
constexpr int kPlayW = kRoomCols * kTilePx;   // 128
constexpr int kPlayH = kRoomRows * kTilePx;   // 48
constexpr int kPlayTop = 3;

// ── Player / combat tuning ───────────────────────────────────────────────
constexpr float kPlayerSpeedPxPerS = 40.0f;
constexpr int kPlayerHalf = 3;                // 6x6 box, centre-anchored
constexpr int kPlayerBaseMaxHp = 12;
constexpr int kBearAmuletBonusHp = 4;
constexpr uint32_t kPlayerInvulnMs = 800;
constexpr float kEnemyKnockbackPxPerS = 120.0f;
constexpr float kEnemyKnockbackDecay = 8.0f;  // 1/s
constexpr float kEnemyHitFlashS = 0.15f;
constexpr float kSwingVisibleS = 0.12f;
constexpr uint32_t kQuestBlinkPeriodMs = 1000;  // 1 Hz, per the design pillar
constexpr uint8_t kMinimapPlayerBrightness = 220;
constexpr uint8_t kMinimapQuestBrightness = 110;
constexpr int kMaxShots = 6;

// Which artifact in the chain each amulet is (0-based), so the passive
// buffs can be looked up from the held-artifact bitmask.
constexpr uint8_t kBearAmuletIndex = 2;
constexpr uint8_t kFrostAmuletIndex = 5;

struct WeaponDef {
  const char* label;   // 3-char footer tag
  uint8_t damage;
  uint32_t cooldownMs;
  float speedPxPerS;   // 0 = melee
  float rangePx;       // melee reach, or max projectile travel
};

// Sword hits hardest but only at arm's length; the axe trades damage for a
// short throw; the bow reaches across the whole room for chip damage. The
// straight reach/damage/speed tradeoff from helgrind.md, nothing fancier.
const WeaponDef kWeapons[kWeaponCount] = {
    {"SWD", 3, 300, 0.0f, 8.0f},
    {"AXE", 2, 500, 63.0f, 29.0f},
    {"BOW", 1, 350, 150.0f, 200.0f},
};

// ── Runtime state (module-local; one screen instance per registration,
// matching the rest of this codebase's static-screen convention) ─────────

struct Player {
  float x = 64.0f, y = 40.0f;
  int8_t fx = 0, fy = -1;  // facing, one of the 4 cardinal directions
  int8_t side = 1;         // which way the sprite is drawn: last sideways facing
  int hp = kPlayerBaseMaxHp;
  uint16_t silver = 0;
  uint8_t arts = 0;        // bitmask of held artifacts, bit i = chain index i
  uint8_t weapon = kWeaponSword;
  uint8_t room = kStartRoom;
  float entryX = 64.0f, entryY = 40.0f;  // respawn point for this room
  bool won = false;
  uint64_t loot = 0;       // bitmask of emptied silver caches, bit = room
};

struct Enemy {
  bool alive = false;
  uint8_t kind = kEnemyNone;
  float x = 0, y = 0;
  int hp = 0;
  float flashS = 0;
  float kbx = 0, kby = 0;
  // Idle wandering (see stepEnemies): a pause, then one tile-sized hop.
  bool hopping = false;
  float hopX = 0, hopY = 0;   // where the current hop ends
  float idleS = 0;            // pause left before the next hop
};

struct Shot {
  bool alive = false;
  float x = 0, y = 0, vx = 0, vy = 0;
  float traveled = 0;
  uint8_t weapon = kWeaponAxe;
};

enum class Mode { kPlay, kMessage, kPause, kGameOver, kVictory };

Player gPlayer;
Enemy gEnemies[kMaxRoomEnemies];
Shot gShots[kMaxShots];
Mode gMode = Mode::kPlay;
// Where to go when the message box closes — normally back to play, but
// the ending text hands off to the victory menu instead.
Mode gAfterMessage = Mode::kPlay;
uint32_t gLastAttackMs = 0;
uint32_t gInvulnUntilMs = 0;
float gSwingS = 0.0f;
bool gItemRefusedShown = false;
// Helgrind's arrival page is shown once per session per state (empty
// gate / Hrafn present), so dying to him and respawning doesn't re-show it.
bool gGateSeen[2] = {false, false};
uint8_t gMenuCursor = 0;
int8_t gMenuStickDir = 0;
uint32_t gLastTickMs = 0;

// Small xorshift for idle wandering. Seeded from the clock at gameBegin,
// which keeps scripted runs (fixed clock) reproducible.
uint32_t gRng = 1;
uint32_t rng() {
  gRng ^= gRng << 13;
  gRng ^= gRng >> 17;
  gRng ^= gRng << 5;
  return gRng;
}
float rngUnit() { return (rng() & 0xFFFF) / 65536.0f; }

// Message box: a queue of Pages (title + up to three lines), advanced
// with B. Pages are copied by value but their strings point at static
// world data or literals, except the one scratch line the merchant
// formats prices into.
constexpr int kMaxMsgPages = 6;
Page gMsgPages[kMaxMsgPages];
int gMsgCount = 0;
int gMsgIndex = 0;
char gMsgScratch[28];

void queuePages(const Page* pages, int count) {
  for (int i = 0; i < count && gMsgCount < kMaxMsgPages; i++) {
    gMsgPages[gMsgCount++] = pages[i];
  }
  if (gMsgCount > 0) gMode = Mode::kMessage;
}

void showMessage(const char* title, const char* l0, const char* l1 = nullptr,
                 const char* l2 = nullptr) {
  Page p = {title, {l0, l1, l2}};
  queuePages(&p, 1);
}

void queueArtifactPages(uint8_t index) {
  const ArtifactDef& a = artifactDef(index);
  queuePages(a.pages, a.pageCount);
}

// B on the last page closes the box; earlier pages just advance. Returns
// true when the box closed, so callers can chain a follow-up action.
bool advanceMessage() {
  gMsgIndex++;
  if (gMsgIndex < gMsgCount) return false;
  gMsgCount = 0;
  gMsgIndex = 0;
  gMode = gAfterMessage;
  gAfterMessage = Mode::kPlay;
  return true;
}

// ── Persistence ──────────────────────────────────────────────────────────
//
// The game never touches storage itself: it raises a dirty flag on the
// events that change durable state (room change, pickup, purchase, death,
// victory) and the shell writes gameSave() out when it sees the flag —
// never per frame.
bool gSaveDirty = false;

void saveGame() { gSaveDirty = true; }

// ── Player-derived stats ─────────────────────────────────────────────────

bool hasArtifact(uint8_t index) { return (gPlayer.arts >> index) & 1; }

int nextArtifactIndex() {
  for (uint8_t i = 0; i < kArtifactCount; i++) {
    if (!hasArtifact(i)) return i;
  }
  return kArtifactCount;  // all held
}

int playerMaxHp() {
  return kPlayerBaseMaxHp + (hasArtifact(kBearAmuletIndex) ? kBearAmuletBonusHp : 0);
}

bool weaponUnlocked(uint8_t w) {
  if (w == kWeaponSword) return true;
  for (uint8_t i = 0; i < kArtifactCount; i++) {
    const ArtifactDef& a = artifactDef(i);
    if (a.type == kArtWeapon && a.weapon == w) return hasArtifact(i);
  }
  return false;
}

// The room the minimap should blink: wherever the next artifact in the
// chain sits, then the gate once the chain is complete, nothing once won.
int questRoom() {
  if (gPlayer.won) return -1;
  int next = nextArtifactIndex();
  if (next >= kArtifactCount) return kGateRoom;
  for (int r = 0; r < kRoomCount; r++) {
    if (roomDef(r).artifact == next + 1) return r;
  }
  return -1;
}

// ── Tiles and collision ──────────────────────────────────────────────────

int roomRow(int room) { return room / kWorldCols; }
int roomCol(int room) { return room % kWorldCols; }

bool isDoorTile(int tx, int ty) {
  bool ns = (ty == 0 || ty == kRoomRows - 1) && (tx == 7 || tx == 8);
  bool ew = (tx == 0 || tx == kRoomCols - 1) && (ty == 2 || ty == 3);
  return ns || ew;
}

// True if the doorway between `room` and `neighbour` is closed from
// either side — see kRoomWall* in HelgrindWorld.h.
bool edgeSealed(int room, int neighbour, uint8_t mine, uint8_t theirs) {
  return (roomDef(room).flags & mine) || (roomDef(neighbour).flags & theirs);
}

// Template char at a tile, with the doorway sealed when it would lead off
// the edge of the world or through a closed edge.
char tileAt(int room, int tx, int ty) {
  if (tx < 0 || ty < 0 || tx >= kRoomCols || ty >= kRoomRows) return kTileWall;
  const RoomDef& def = roomDef(room);
  int srcX = (def.flags & kRoomFlipX) ? kRoomCols - 1 - tx : tx;
  char t = templateRow(def.tmpl, ty)[srcX];
  if (isDoorTile(tx, ty)) {
    int r = roomRow(room), c = roomCol(room);
    if (ty == 0 && (r == 0 || edgeSealed(room, room - kWorldCols, kRoomWallN, kRoomWallS))) {
      return kTileWall;
    }
    if (ty == kRoomRows - 1 &&
        (r == kWorldRows - 1 || edgeSealed(room, room + kWorldCols, kRoomWallS, kRoomWallN))) {
      return kTileWall;
    }
    if (tx == 0 && (c == 0 || edgeSealed(room, room - 1, kRoomWallW, kRoomWallE))) {
      return kTileWall;
    }
    if (tx == kRoomCols - 1 &&
        (c == kWorldCols - 1 || edgeSealed(room, room + 1, kRoomWallE, kRoomWallW))) {
      return kTileWall;
    }
  }
  return t;
}

bool solidAtPx(int room, float px, float py) {
  // Off the play area (through a doorway) is walkable — that's how room
  // transitions happen. Only a tile with a solid char blocks.
  if (px < 0 || py < 0 || px >= kPlayW || py >= kPlayH) return false;
  return tileSolid(tileAt(room, static_cast<int>(px) / kTilePx,
                          static_cast<int>(py) / kTilePx));
}

bool boxBlocked(int room, float cx, float cy, int halfW, int halfH) {
  return solidAtPx(room, cx - halfW, cy - halfH) ||
         solidAtPx(room, cx + halfW - 1, cy - halfH) ||
         solidAtPx(room, cx - halfW, cy + halfH - 1) ||
         solidAtPx(room, cx + halfW - 1, cy + halfH - 1);
}

// Axis-separated move so sliding along a wall works without a real
// collision-response pass. Returns whether either axis moved.
void moveWithCollision(int room, float& x, float& y, float dx, float dy,
                       int halfW, int halfH) {
  if (dx != 0.0f && !boxBlocked(room, x + dx, y, halfW, halfH)) x += dx;
  if (dy != 0.0f && !boxBlocked(room, x, y + dy, halfW, halfH)) y += dy;
}

bool boxesOverlap(float ax, float ay, int ahw, int ahh, float bx, float by,
                  int bhw, int bhh) {
  return fabsf(ax - bx) < (ahw + bhw) && fabsf(ay - by) < (ahh + bhh);
}

// ── Room population ──────────────────────────────────────────────────────

// Fixed spawn slots per room; findFloorNear() nudges each onto a walkable
// tile if the template puts an obstacle there.
constexpr int kEnemySlotTiles[kMaxRoomEnemies][2] = {{3, 1}, {12, 4}, {8, 1}};
constexpr int kBossTile[2] = {7, 2};

void findFloorNear(int room, int& tx, int& ty) {
  if (!tileSolid(tileAt(room, tx, ty))) return;
  for (int r = 1; r <= 3; r++) {
    for (int dy = -r; dy <= r; dy++) {
      for (int dx = -r; dx <= r; dx++) {
        int nx = tx + dx, ny = ty + dy;
        if (nx <= 0 || ny <= 0 || nx >= kRoomCols - 1 || ny >= kRoomRows - 1) continue;
        if (!tileSolid(tileAt(room, nx, ny))) {
          tx = nx;
          ty = ny;
          return;
        }
      }
    }
  }
}

void tileCenter(int tx, int ty, float& x, float& y) {
  x = tx * kTilePx + kTilePx / 2.0f;
  y = ty * kTilePx + kTilePx / 2.0f;
}

void spawnEnemy(int slot, uint8_t kind, int tx, int ty) {
  findFloorNear(gPlayer.room, tx, ty);
  Enemy& e = gEnemies[slot];
  e.alive = true;
  e.kind = kind;
  tileCenter(tx, ty, e.x, e.y);
  e.hp = enemyDef(kind).hp;
  e.flashS = 0.0f;
  e.kbx = e.kby = 0.0f;
  e.hopping = false;
  e.idleS = 0.5f + rngUnit() * 1.5f;
}

// Item/NPC/loot positions are chosen in template space (so they land on
// floor for every template) and mirrored along with the room. Loot goes
// through findFloorNear() since its slot is an obstacle in some layouts.
void mirrorTile(int room, int& tx) {
  if (roomDef(room).flags & kRoomFlipX) tx = kRoomCols - 1 - tx;
}

void artifactTile(int room, int& tx, int& ty) {
  tx = roomDef(room).tmpl == kTmplLonghouse ? 10 : 12;
  ty = 2;
  mirrorTile(room, tx);
}

void npcTile(int room, int& tx, int& ty) {
  tx = 4;
  ty = 2;
  mirrorTile(room, tx);
}

void lootTile(int room, int& tx, int& ty) {
  tx = 2;
  ty = 4;
  mirrorTile(room, tx);
  findFloorNear(room, tx, ty);
}

// The elder hands over the Gate Scroll in dialogue, so it never draws on
// the floor of his hall.
bool roomHasArtifactToShow(int room) {
  uint8_t art = roomDef(room).artifact;
  if (art == 0 || hasArtifact(art - 1)) return false;
  return art - 1 != kElderGivenArtifact;
}

bool roomHasLootToShow(int room) {
  // The hermit's room repurposes its loot field as a conversation-granted
  // gift amount (see specialAction()), not a floor-tile cache.
  return roomDef(room).loot != 0 && roomDef(room).npc != kNpcHermit &&
         !((gPlayer.loot >> room) & 1u);
}

// Marks a room's one-time silver as claimed and adds it to the purse,
// whether found on the floor (checkLootPickup()) or handed over in
// dialogue (the hermit's gift in specialAction()). Returns the amount.
uint8_t claimRoomLoot(int room) {
  uint8_t silver = roomDef(room).loot;
  gPlayer.loot |= (1ull << room);
  gPlayer.silver += silver;
  saveGame();
  return silver;
}

// Classic-era room rule: enemies come back every time you walk in. The
// warden only exists once the chain is complete and hasn't been beaten.
void enterRoom(int room, float x, float y) {
  gPlayer.room = static_cast<uint8_t>(room);
  gPlayer.x = x;
  gPlayer.y = y;
  gPlayer.entryX = x;
  gPlayer.entryY = y;
  gItemRefusedShown = false;
  gSwingS = 0.0f;
  for (Shot& s : gShots) s.alive = false;
  for (Enemy& e : gEnemies) e.alive = false;

  const RoomDef& def = roomDef(room);
  if (room == kGateRoom) {
    bool boss = nextArtifactIndex() >= kArtifactCount && !gPlayer.won;
    if (boss) spawnEnemy(0, kEnemyBoss, kBossTile[0], kBossTile[1]);
    if (!gPlayer.won && !gGateSeen[boss]) {
      gGateSeen[boss] = true;
      queuePages(gatePage(boss), 1);
    }
    return;
  }
  for (int i = 0; i < kMaxRoomEnemies; i++) {
    if (def.enemies[i] == kEnemyNone) continue;
    spawnEnemy(i, def.enemies[i], kEnemySlotTiles[i][0], kEnemySlotTiles[i][1]);
  }
}

void newGame(uint32_t now) {
  gPlayer = Player();
  gMode = Mode::kPlay;
  gInvulnUntilMs = 0;
  gGateSeen[0] = gGateSeen[1] = false;
  enterRoom(kStartRoom, 64.0f, 28.0f);
  saveGame();
  int n;
  const Page* intro = introPages(n);
  queuePages(intro, n);
}

void respawn(uint32_t now) {
  gPlayer.hp = playerMaxHp();
  gInvulnUntilMs = now + kPlayerInvulnMs;
  gMode = Mode::kPlay;
  enterRoom(gPlayer.room, gPlayer.entryX, gPlayer.entryY);
  saveGame();
}

// ── Combat ───────────────────────────────────────────────────────────────

void damageEnemy(Enemy& e, int dmg, float fromX, float fromY) {
  e.hp -= dmg;
  e.flashS = kEnemyHitFlashS;
  float dx = e.x - fromX, dy = e.y - fromY;
  float len = sqrtf(dx * dx + dy * dy);
  if (len > 1e-3f) {
    e.kbx = dx / len * kEnemyKnockbackPxPerS;
    e.kby = dy / len * kEnemyKnockbackPxPerS;
  }
  if (e.hp <= 0) {
    e.alive = false;
    gPlayer.silver += enemyDef(e.kind).silver;
    if (e.kind == kEnemyBoss) {
      gPlayer.won = true;
      saveGame();
      gMenuCursor = 0;
      int n;
      const Page* ending = endingPages(n);
      queuePages(ending, n);
      gAfterMessage = Mode::kVictory;
    }
  }
}

void attack(uint32_t now) {
  const WeaponDef& w = kWeapons[gPlayer.weapon];
  if (now - gLastAttackMs < w.cooldownMs) return;
  gLastAttackMs = now;

  if (w.speedPxPerS == 0.0f) {
    // Melee: one box in front of the player, resolved immediately.
    gSwingS = kSwingVisibleS;
    float hx = gPlayer.x + gPlayer.fx * (kPlayerHalf + w.rangePx * 0.5f);
    float hy = gPlayer.y + gPlayer.fy * (kPlayerHalf + w.rangePx * 0.5f);
    int half = static_cast<int>(w.rangePx * 0.5f);
    for (Enemy& e : gEnemies) {
      if (!e.alive) continue;
      const EnemyDef& ed = enemyDef(e.kind);
      if (boxesOverlap(hx, hy, half, half, e.x, e.y, ed.w / 2, ed.h / 2)) {
        damageEnemy(e, w.damage, gPlayer.x, gPlayer.y);
      }
    }
    return;
  }

  for (Shot& s : gShots) {
    if (s.alive) continue;
    s.alive = true;
    s.weapon = gPlayer.weapon;
    s.x = gPlayer.x + gPlayer.fx * kPlayerHalf;
    s.y = gPlayer.y + gPlayer.fy * kPlayerHalf;
    s.vx = gPlayer.fx * w.speedPxPerS;
    s.vy = gPlayer.fy * w.speedPxPerS;
    s.traveled = 0.0f;
    return;
  }
}

void stepShots(float dtS) {
  for (Shot& s : gShots) {
    if (!s.alive) continue;
    const WeaponDef& w = kWeapons[s.weapon];
    float dx = s.vx * dtS, dy = s.vy * dtS;
    s.x += dx;
    s.y += dy;
    s.traveled += fabsf(dx) + fabsf(dy);
    if (s.traveled >= w.rangePx || s.x < 0 || s.y < 0 || s.x >= kPlayW ||
        s.y >= kPlayH || solidAtPx(gPlayer.room, s.x, s.y)) {
      s.alive = false;
      continue;
    }
    for (Enemy& e : gEnemies) {
      if (!e.alive) continue;
      const EnemyDef& ed = enemyDef(e.kind);
      if (boxesOverlap(s.x, s.y, 1, 1, e.x, e.y, ed.w / 2, ed.h / 2)) {
        damageEnemy(e, w.damage, s.x - dx * 4, s.y - dy * 4);
        s.alive = false;
        break;
      }
    }
  }
}

void hurtPlayer(int dmg, uint32_t now) {
  if (now < gInvulnUntilMs) return;
  if (hasArtifact(kFrostAmuletIndex)) dmg = (dmg + 1) / 2;
  gPlayer.hp -= dmg;
  gInvulnUntilMs = now + kPlayerInvulnMs;
  if (gPlayer.hp <= 0) {
    gPlayer.hp = 0;
    gMode = Mode::kGameOver;
    gMenuCursor = 0;
  }
}

void stepEnemies(float dtS, uint32_t now) {
  for (Enemy& e : gEnemies) {
    if (!e.alive) continue;
    const EnemyDef& ed = enemyDef(e.kind);
    if (e.flashS > 0.0f) e.flashS -= dtS;

    float mx = e.kbx * dtS, my = e.kby * dtS;
    e.kbx -= e.kbx * kEnemyKnockbackDecay * dtS;
    e.kby -= e.kby * kEnemyKnockbackDecay * dtS;

    float dx = gPlayer.x - e.x, dy = gPlayer.y - e.y;
    float dist = sqrtf(dx * dx + dy * dy);
    bool chasing = ed.aggroPx == 0 || dist <= ed.aggroPx || e.hp < ed.hp;
    if (chasing) {
      e.hopping = false;
      if (dist > 1.0f) {
        mx += dx / dist * ed.speedPxPerS * dtS;
        my += dy / dist * ed.speedPxPerS * dtS;
      }
    } else if (e.hopping) {
      // Mid-hop: walk to the target tile centre, then rest a while.
      float hx = e.hopX - e.x, hy = e.hopY - e.y;
      float hd = sqrtf(hx * hx + hy * hy);
      float step = ed.speedPxPerS * 0.75f * dtS;
      if (hd <= step) {
        mx += hx;
        my += hy;
        e.hopping = false;
        e.idleS = 0.8f + rngUnit() * 2.0f;
      } else {
        mx += hx / hd * step;
        my += hy / hd * step;
      }
    } else {
      // Resting: when the pause runs out, pick a neighbouring tile and go
      // if it's clear; otherwise try again shortly.
      e.idleS -= dtS;
      if (e.idleS <= 0.0f) {
        static const int8_t kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        const int8_t* d = kDirs[rng() & 3];
        float tx = e.x + d[0] * kTilePx, ty = e.y + d[1] * kTilePx;
        bool inside = tx >= ed.w / 2 && tx <= kPlayW - ed.w / 2 &&
                      ty >= ed.h / 2 && ty <= kPlayH - ed.h / 2;
        if (inside && !boxBlocked(gPlayer.room, tx, ty, ed.w / 2, ed.h / 2)) {
          e.hopX = tx;
          e.hopY = ty;
          e.hopping = true;
        } else {
          e.idleS = 0.3f;
        }
      }
    }
    moveWithCollision(gPlayer.room, e.x, e.y, mx, my, ed.w / 2, ed.h / 2);
    // Keep enemies inside the room even if they wander toward a doorway.
    if (e.x < ed.w / 2) e.x = ed.w / 2;
    if (e.y < ed.h / 2) e.y = ed.h / 2;
    if (e.x > kPlayW - ed.w / 2) e.x = kPlayW - ed.w / 2;
    if (e.y > kPlayH - ed.h / 2) e.y = kPlayH - ed.h / 2;

    if (boxesOverlap(e.x, e.y, ed.w / 2, ed.h / 2, gPlayer.x, gPlayer.y,
                     kPlayerHalf, kPlayerHalf)) {
      hurtPlayer(ed.contactDamage, now);
      if (gMode != Mode::kPlay) return;
    }
  }
}

// ── Interactions: pickups, NPCs, scrolls ─────────────────────────────────

void collectArtifact(uint8_t index) {
  const ArtifactDef& a = artifactDef(index);
  gPlayer.arts |= static_cast<uint8_t>(1u << index);
  if (a.type == kArtAmulet && index == kBearAmuletIndex) gPlayer.hp += kBearAmuletBonusHp;
  saveGame();
  queueArtifactPages(index);
}

void checkArtifactPickup() {
  int room = gPlayer.room;
  if (!roomHasArtifactToShow(room)) return;
  int tx, ty;
  artifactTile(room, tx, ty);
  float ix, iy;
  tileCenter(tx, ty, ix, iy);
  if (!boxesOverlap(ix, iy, 4, 4, gPlayer.x, gPlayer.y, kPlayerHalf, kPlayerHalf)) {
    gItemRefusedShown = false;
    return;
  }
  uint8_t index = roomDef(room).artifact - 1;
  const ArtifactDef& a = artifactDef(index);
  if (index != nextArtifactIndex()) {
    if (gItemRefusedShown) return;
    gItemRefusedShown = true;
    showMessage(a.name, "It will not come loose.", "Something else must",
                "come first.");
    return;
  }
  collectArtifact(index);
}

void checkLootPickup() {
  int room = gPlayer.room;
  if (!roomHasLootToShow(room)) return;
  int tx, ty;
  lootTile(room, tx, ty);
  float lx, ly;
  tileCenter(tx, ty, lx, ly);
  if (!boxesOverlap(lx, ly, 4, 4, gPlayer.x, gPlayer.y, kPlayerHalf, kPlayerHalf)) return;
  uint8_t silver = claimRoomLoot(room);
  std::snprintf(gMsgScratch, sizeof(gMsgScratch), "%d silver, unclaimed.", silver);
  showMessage("Cache", "A purse under the snow.", gMsgScratch);
}

bool nearNpc() {
  if (roomDef(gPlayer.room).npc == kNpcNone) return false;
  int tx, ty;
  npcTile(gPlayer.room, tx, ty);
  float nx, ny;
  tileCenter(tx, ty, nx, ny);
  return boxesOverlap(nx, ny, 8, 8, gPlayer.x, gPlayer.y, kPlayerHalf, kPlayerHalf);
}

// Is a tile of this kind within a couple of pixels of the player's box?
bool nearTile(char kind) {
  const int r = 2;
  int tx0 = static_cast<int>(gPlayer.x - kPlayerHalf - r) / kTilePx;
  int tx1 = static_cast<int>(gPlayer.x + kPlayerHalf + r) / kTilePx;
  int ty0 = static_cast<int>(gPlayer.y - kPlayerHalf - r) / kTilePx;
  int ty1 = static_cast<int>(gPlayer.y + kPlayerHalf + r) / kTilePx;
  for (int ty = ty0; ty <= ty1; ty++) {
    for (int tx = tx0; tx <= tx1; tx++) {
      if (tx < 0 || ty < 0 || tx >= kRoomCols || ty >= kRoomRows) continue;
      if (tileAt(gPlayer.room, tx, ty) == kind) return true;
    }
  }
  return false;
}

void specialAction() {
  if (nearNpc()) {
    uint8_t npc = roomDef(gPlayer.room).npc;
    if (npc == kNpcMerchant) {
      if (gPlayer.hp >= playerMaxHp()) {
        showMessage("Merchant", "\"You look hale enough.", "Come back when you",
                    "bleed.\"");
      } else if (gPlayer.silver < kMeadPrice) {
        std::snprintf(gMsgScratch, sizeof(gMsgScratch), "\"Mead is %d silver.",
                      kMeadPrice);
        showMessage("Merchant", gMsgScratch, "You are short. The dead",
                    "don't pay either.\"");
      } else {
        gPlayer.silver -= kMeadPrice;
        gPlayer.hp += kMeadHeal;
        if (gPlayer.hp > playerMaxHp()) gPlayer.hp = playerMaxHp();
        saveGame();
        std::snprintf(gMsgScratch, sizeof(gMsgScratch), "Mead. +%d health.", kMeadHeal);
        showMessage("Merchant", "\"Drink up. Slowly.\"", gMsgScratch);
      }
      return;
    }
    int stage = nextArtifactIndex();
    int n;
    const Page* pages = npcPages(npc, stage, gPlayer.won, n);
    if (pages) queuePages(pages, n);
    // The elder's stage-6 line is the hand-over: the Gate Scroll's own
    // pages follow his preamble and the artifact is granted on the spot.
    if (npc == kNpcElder && !gPlayer.won && stage == kElderGivenArtifact) {
      collectArtifact(kElderGivenArtifact);
    }
    // The hermit's last line (stage 6, his final cut) is a parting gift,
    // granted once via the same one-time-cache mechanic loot rooms use —
    // his room's loot field doubles as the gift amount (roomHasLootToShow
    // excludes his room from the ordinary floor-tile pickup).
    if (npc == kNpcHermit && !gPlayer.won && stage >= kHermitGiftStage &&
        !((gPlayer.loot >> gPlayer.room) & 1u)) {
      uint8_t silver = claimRoomLoot(gPlayer.room);
      std::snprintf(gMsgScratch, sizeof(gMsgScratch), "%d silver. Go.", silver);
      showMessage("Hermit", "A pouch, pressed into", gMsgScratch);
    }
    return;
  }

  // No NPC: a rune stone beside you can be read.
  if (nearTile(kTileRune)) {
    queuePages(runeStonePage(gPlayer.room), 1);
    return;
  }

  // Otherwise re-read the most recent scroll held.
  for (int i = kArtifactCount - 1; i >= 0; i--) {
    const ArtifactDef& a = artifactDef(i);
    if (a.type == kArtScroll && hasArtifact(i)) {
      queueArtifactPages(i);
      return;
    }
  }
  showMessage("Scrolls", "You carry no scrolls.", "The elder might know",
              "where to start.");
}

void cycleWeapon() {
  for (int i = 1; i <= kWeaponCount; i++) {
    uint8_t w = static_cast<uint8_t>((gPlayer.weapon + i) % kWeaponCount);
    if (weaponUnlocked(w)) {
      if (w != gPlayer.weapon) {
        gPlayer.weapon = w;
        saveGame();
      }
      return;
    }
  }
}

// ── Room transitions ─────────────────────────────────────────────────────

void checkRoomTransition() {
  int r = roomRow(gPlayer.room), c = roomCol(gPlayer.room);
  if (gPlayer.y < 0 && r > 0) {
    enterRoom(gPlayer.room - kWorldCols, gPlayer.x, kPlayH - kPlayerHalf - 1);
  } else if (gPlayer.y >= kPlayH && r < kWorldRows - 1) {
    enterRoom(gPlayer.room + kWorldCols, gPlayer.x, kPlayerHalf + 1);
  } else if (gPlayer.x < 0 && c > 0) {
    enterRoom(gPlayer.room - 1, kPlayW - kPlayerHalf - 1, gPlayer.y);
  } else if (gPlayer.x >= kPlayW && c < kWorldCols - 1) {
    enterRoom(gPlayer.room + 1, kPlayerHalf + 1, gPlayer.y);
  } else {
    return;
  }
  saveGame();
}

// ── Simulation step (kPlay only) ─────────────────────────────────────────

void stepPlay(const GameInput& in, float dtS, uint32_t now) {
  float xDir = in.stickX;
  float yDir = in.stickY;
  constexpr float kDead = 0.25f;
  if (fabsf(xDir) < kDead) xDir = 0.0f;
  if (fabsf(yDir) < kDead) yDir = 0.0f;
  if (xDir != 0.0f || yDir != 0.0f) {
    // Facing snaps to the dominant axis so attacks go one of 4 ways.
    if (fabsf(xDir) >= fabsf(yDir)) {
      gPlayer.fx = xDir > 0 ? 1 : -1;
      gPlayer.fy = 0;
      gPlayer.side = gPlayer.fx;
    } else {
      gPlayer.fx = 0;
      gPlayer.fy = yDir > 0 ? 1 : -1;
    }
    float len = sqrtf(xDir * xDir + yDir * yDir);
    if (len > 1.0f) {
      xDir /= len;
      yDir /= len;
    }
    moveWithCollision(gPlayer.room, gPlayer.x, gPlayer.y,
                      xDir * kPlayerSpeedPxPerS * dtS,
                      yDir * kPlayerSpeedPxPerS * dtS, kPlayerHalf, kPlayerHalf);
  }
  checkRoomTransition();

  if (gSwingS > 0.0f) gSwingS -= dtS;
  if (in.b) attack(now);
  if (in.x) cycleWeapon();
  if (in.y) {
    specialAction();
    return;
  }

  stepShots(dtS);
  stepEnemies(dtS, now);
  if (gMode != Mode::kPlay) return;
  checkArtifactPickup();
  if (gMode != Mode::kPlay) return;
  checkLootPickup();
}

// ── Drawing ──────────────────────────────────────────────────────────────

void drawTile(u8g2_t* u, char t, int x, int y) {
  switch (t) {
    case kTileWall:
      drawRFrame(u, x, y, kTilePx, kTilePx, 0);
      u8g2_DrawPixel(u, x + 3, y + 3);
      u8g2_DrawPixel(u, x + 4, y + 4);
      break;
    case kTileTree:
      u8g2_DrawTriangle(u, x + 4, y, x, y + 5, x + 7, y + 5);
      u8g2_DrawVLine(u, x + 3, y + 5, 3);
      u8g2_DrawVLine(u, x + 4, y + 5, 3);
      break;
    case kTileWater:
      u8g2_DrawHLine(u, x, y + 2, 3);
      u8g2_DrawHLine(u, x + 4, y + 3, 3);
      u8g2_DrawHLine(u, x + 1, y + 6, 3);
      u8g2_DrawHLine(u, x + 5, y + 5, 2);
      break;
    case kTileGrass:
      u8g2_DrawPixel(u, x + 2, y + 5);
      u8g2_DrawPixel(u, x + 3, y + 4);
      u8g2_DrawPixel(u, x + 5, y + 6);
      break;
    case kTileRune:
      drawRFrame(u, x + 1, y, 6, 8, 1);
      u8g2_DrawPixel(u, x + 3, y + 3);
      u8g2_DrawPixel(u, x + 4, y + 4);
      u8g2_DrawPixel(u, x + 3, y + 5);
      break;
    case kTilePlank:
      u8g2_DrawPixel(u, x + 1, y + 1);
      u8g2_DrawPixel(u, x + 5, y + 5);
      break;
    case kTileMound:
      drawLine(u, x, y + 7, x + 3, y + 2);
      drawLine(u, x + 4, y + 2, x + 7, y + 7);
      u8g2_DrawHLine(u, x + 3, y + 2, 2);
      u8g2_DrawPixel(u, x + 3, y + 5);
      break;
    case kTileBones:
      u8g2_DrawHLine(u, x + 1, y + 4, 5);
      u8g2_DrawPixel(u, x + 1, y + 3);
      u8g2_DrawPixel(u, x + 5, y + 5);
      u8g2_DrawPixel(u, x + 5, y + 3);
      u8g2_DrawPixel(u, x + 1, y + 5);
      break;
    default:
      break;
  }
}

void drawRoomTiles(u8g2_t* u, int room) {
  for (int ty = 0; ty < kRoomRows; ty++) {
    for (int tx = 0; tx < kRoomCols; tx++) {
      drawTile(u, tileAt(room, tx, ty), tx * kTilePx, kPlayTop + ty * kTilePx);
    }
  }
}

void drawFigure(u8g2_t* u, int cx, int cy, bool hat) {
  u8g2_DrawBox(u, cx - 1, cy - 3, 2, 2);        // head
  u8g2_DrawVLine(u, cx, cy - 1, 3);             // body
  u8g2_DrawHLine(u, cx - 2, cy, 5);             // arms
  u8g2_DrawPixel(u, cx - 1, cy + 2);            // legs
  u8g2_DrawPixel(u, cx + 1, cy + 2);
  if (hat) u8g2_DrawHLine(u, cx - 2, cy - 4, 5);
}

// Box that may poke above or left of the screen (a weapon held up near
// the top wall); u8g2's coordinates are unsigned so clamp first.
void drawBoxClipped(u8g2_t* u, int x, int y, int w, int h) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (w > 0 && h > 0) u8g2_DrawBox(u, x, y, w, h);
}

// The held weapon, one pixel out from the leading arm, carried upright
// at rest: sword blade straight up, axe haft up with the head forward,
// bow as a small arc bowed away. `side` is the drawn facing (always left
// or right); the swing itself follows the true facing so an upward or
// downward strike is still shown where it lands.
void drawHeldWeapon(u8g2_t* u, int cx, int cy, int side) {
  int hx = cx + side * 4, hy = cy - 1;  // hand: past the arm, at arm height
  if (gSwingS > 0.0f && gPlayer.weapon == kWeaponSword) {
    int len = static_cast<int>(kWeapons[kWeaponSword].rangePx);
    int fx = gPlayer.fx, fy = gPlayer.fy;
    int sx = fx ? hx : cx, sy = fx ? hy : (fy < 0 ? cy - 6 : cy + 3);
    drawLine(u, sx, sy, sx + fx * (len - 1), sy + fy * (len - 1));
    return;
  }
  switch (gPlayer.weapon) {
    case kWeaponSword:
      drawLine(u, hx, hy - 3, hx, hy);                              // blade up
      break;
    case kWeaponAxe:
      for (const Shot& sh : gShots) {
        if (sh.alive && sh.weapon == kWeaponAxe) return;            // it's in the air
      }
      drawLine(u, hx, hy - 3, hx, hy);                              // haft up
      drawBoxClipped(u, side > 0 ? hx + 1 : hx - 2, hy - 4, 2, 2);  // head, forward
      break;
    default:  // bow: a 5-pixel arc bowed away from the player
      u8g2_DrawPixel(u, hx, hy - 2);
      drawLine(u, hx + side, hy - 1, hx + side, hy + 1);
      u8g2_DrawPixel(u, hx, hy + 2);
      break;
  }
}

// The player: a 3-wide head and body so it reads as a figure rather than
// a stick, drawn only facing left or right (whichever way it last walked
// sideways) with a nose and the leading arm on that side.
void drawPlayer(u8g2_t* u, uint32_t now) {
  // Invulnerability flicker: skip every other 60ms slice after a hit.
  if (now < gInvulnUntilMs && ((now / 60) & 1)) return;
  int cx = static_cast<int>(gPlayer.x), cy = kPlayTop + static_cast<int>(gPlayer.y);
  int side = gPlayer.side;
  u8g2_DrawBox(u, cx - 1, cy - 5, 3, 2);            // head
  u8g2_DrawPixel(u, cx + side * 2, cy - 4);         // nose
  u8g2_DrawPixel(u, cx, cy - 3);                    // neck
  u8g2_DrawBox(u, cx - 1, cy - 2, 3, 3);            // body
  u8g2_DrawPixel(u, cx + side * 2, cy - 1);         // leading arm
  u8g2_DrawPixel(u, cx - 1, cy + 1);                // legs
  u8g2_DrawPixel(u, cx + 1, cy + 1);
  u8g2_DrawPixel(u, cx - 1, cy + 2);
  u8g2_DrawPixel(u, cx + 1, cy + 2);
  drawHeldWeapon(u, cx, cy, side);
}

void drawEnemy(u8g2_t* u, const Enemy& e, uint32_t now) {
  if (e.flashS > 0.0f && ((now / 40) & 1)) return;
  int cx = static_cast<int>(e.x), cy = kPlayTop + static_cast<int>(e.y);
  switch (e.kind) {
    case kEnemyWolf:
      u8g2_DrawBox(u, cx - 3, cy - 1, 6, 2);
      u8g2_DrawPixel(u, cx + 3, cy - 2);
      u8g2_DrawPixel(u, cx + 3, cy - 1);
      u8g2_DrawPixel(u, cx - 3, cy + 1);
      u8g2_DrawPixel(u, cx + 1, cy + 1);
      break;
    case kEnemyDraugr:
      drawRFrame(u, cx - 1, cy - 4, 3, 3, 0);
      u8g2_DrawVLine(u, cx, cy - 1, 3);
      drawLine(u, cx - 3, cy - 1, cx + 3, cy + 1);
      u8g2_DrawPixel(u, cx - 1, cy + 3);
      u8g2_DrawPixel(u, cx + 1, cy + 3);
      break;
    case kEnemyTroll:
      drawRFrame(u, cx - 4, cy - 4, 8, 8, 2);
      u8g2_DrawPixel(u, cx - 2, cy - 1);
      u8g2_DrawPixel(u, cx + 1, cy - 1);
      u8g2_DrawHLine(u, cx - 2, cy + 2, 4);
      break;
    case kEnemyBoss:
      drawRFrame(u, cx - 5, cy - 5, 10, 10, 3);
      drawRFrame(u, cx - 2, cy - 2, 4, 4, 0);
      u8g2_DrawPixel(u, cx - 4, cy - 6);
      u8g2_DrawPixel(u, cx + 3, cy - 6);
      break;
    default:
      break;
  }
}

void drawShots(u8g2_t* u) {
  for (const Shot& s : gShots) {
    if (!s.alive) continue;
    int x = static_cast<int>(s.x), y = kPlayTop + static_cast<int>(s.y);
    if (s.weapon == kWeaponAxe) {
      // A thrown axe tumbles: four quarter-turns of a 3px haft with a 2x2
      // head, advanced every 3px of travel.
      switch (static_cast<int>(s.traveled / 3.0f) & 3) {
        case 0: drawLine(u, x - 2, y, x, y);     drawBoxClipped(u, x + 1, y - 1, 2, 2); break;
        case 1: drawLine(u, x, y - 2, x, y);     drawBoxClipped(u, x - 1, y + 1, 2, 2); break;
        case 2: drawLine(u, x, y, x + 2, y);     drawBoxClipped(u, x - 2, y, 2, 2);     break;
        default: drawLine(u, x, y, x, y + 2);    drawBoxClipped(u, x, y - 2, 2, 2);     break;
      }
    } else {
      int ex = x - (s.vx > 0 ? 3 : s.vx < 0 ? -3 : 0);
      int ey = y - (s.vy > 0 ? 3 : s.vy < 0 ? -3 : 0);
      drawLine(u, ex, ey, x, y);
    }
  }
}

void drawRoomObjects(u8g2_t* u) {
  int room = gPlayer.room;
  if (roomHasArtifactToShow(room)) {
    int tx, ty;
    artifactTile(room, tx, ty);
    int x = tx * kTilePx, y = kPlayTop + ty * kTilePx;
    drawRFrame(u, x, y, kTilePx, kTilePx, 2);
    u8g2_DrawHLine(u, x + 2, y + 3, 4);
    u8g2_DrawVLine(u, x + 3, y + 2, 4);
  }
  if (roomHasLootToShow(room)) {
    int tx, ty;
    lootTile(room, tx, ty);
    int x = tx * kTilePx, y = kPlayTop + ty * kTilePx;
    drawRBox(u, x + 2, y + 3, 4, 4, 1);   // purse
    u8g2_DrawHLine(u, x + 3, y + 1, 2);        // tie
  }
  uint8_t npc = roomDef(room).npc;
  if (npc != kNpcNone) {
    int tx, ty;
    npcTile(room, tx, ty);
    int cx = tx * kTilePx + 4, cy = kPlayTop + ty * kTilePx + 4;
    drawFigure(u, cx, cy, npc == kNpcMerchant || npc == kNpcElder);
    switch (npc) {
      case kNpcElder:   // staff
        u8g2_DrawVLine(u, cx + 3, cy - 4, 7);
        break;
      case kNpcSkald:   // lyre, held out to the side
        drawRFrame(u, cx - 5, cy - 2, 3, 4, 0);
        break;
      case kNpcFisher:  // rod
        drawLine(u, cx + 2, cy, cx + 5, cy - 5);
        break;
      case kNpcHermit:  // hood
        u8g2_DrawHLine(u, cx - 2, cy - 4, 5);
        u8g2_DrawPixel(u, cx - 2, cy - 3);
        u8g2_DrawPixel(u, cx + 2, cy - 3);
        break;
      default:
        break;
    }
  }
}

// Footer: health, weapon, silver, chain progress — the "basic game/player
// state" band from the design doc.
void drawFooter(u8g2_t* u) {
  u8g2_SetDrawColor(u, 1);
  u8g2_DrawHLine(u, 0, kFooterTopY, kScreenW);
  u8g2_SetFont(u, kFont);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "HP %d/%d", gPlayer.hp, playerMaxHp());
  u8g2_DrawStr(u, 2, kFooterTextBaseY, buf);
  u8g2_DrawStr(u, 50, kFooterTextBaseY, kWeapons[gPlayer.weapon].label);
  std::snprintf(buf, sizeof(buf), "$%u", static_cast<unsigned>(gPlayer.silver));
  u8g2_DrawStr(u, 76, kFooterTextBaseY, buf);
  int held = 0;
  for (uint8_t i = 0; i < kArtifactCount; i++) held += hasArtifact(i) ? 1 : 0;
  std::snprintf(buf, sizeof(buf), "%d/%d", held, kArtifactCount);
  int w = u8g2_GetStrWidth(u, buf);
  u8g2_DrawStr(u, kScreenW - 2 - w, kFooterTextBaseY, buf);
}

// Text starts at kX+3 and the frame is 1px, so a line may be up to
// kW-7 = 117px wide; the longest line in HelgrindWorld.cpp measures 114px
// in u8g2_font_smallsimple_tr (checked with u8g2_GetStrWidth on the host).
void drawMessageBox(u8g2_t* u) {
  constexpr int kX = 2, kY = 8, kW = 124, kH = 42;
  u8g2_SetDrawColor(u, 0);
  u8g2_DrawBox(u, kX, kY, kW, kH);
  u8g2_SetDrawColor(u, 1);
  drawRFrame(u, kX, kY, kW, kH, 1);
  u8g2_SetFont(u, kFont);
  const Page& pg = gMsgPages[gMsgIndex];
  if (pg.title) u8g2_DrawStr(u, kX + 3, kY + 8, pg.title);
  // The B hint shares the title row: three 8px text lines fill the rest.
  const char* ok = gMsgIndex + 1 < gMsgCount ? "B:more" : "B:ok";
  u8g2_DrawStr(u, kX + kW - 4 - u8g2_GetStrWidth(u, ok), kY + 8, ok);
  u8g2_DrawHLine(u, kX + 2, kY + 10, kW - 4);
  for (int i = 0; i < 3; i++) {
    if (pg.lines[i]) u8g2_DrawStr(u, kX + 3, kY + 20 + i * 9, pg.lines[i]);
  }
}

// Pause / game-over / victory share one overlay: a title, a stats line,
// and 2-3 selectable rows. Which rows exist depends on the mode.
constexpr int kMenuMaxItems = 3;

int menuItems(Mode mode, const char* labels[kMenuMaxItems]) {
  switch (mode) {
    case Mode::kGameOver:
      labels[0] = "Respawn";
      labels[1] = "Exit";
      return 2;
    case Mode::kVictory:
      labels[0] = "New Game";
      labels[1] = "Exit";
      return 2;
    default:
      labels[0] = "Resume";
      labels[1] = "New Game";
      labels[2] = "Exit";
      return 3;
  }
}

void drawMenu(u8g2_t* u, Mode mode) {
  constexpr int kX = 20, kY = 6, kW = 88, kH = 46;
  u8g2_SetDrawColor(u, 0);
  u8g2_DrawBox(u, kX, kY, kW, kH);
  u8g2_SetDrawColor(u, 1);
  drawRFrame(u, kX, kY, kW, kH, 0);
  u8g2_SetFont(u, kFont);
  const char* title = mode == Mode::kGameOver ? "FALLEN"
                      : mode == Mode::kVictory ? "HELGRIND FALLS"
                                               : "PAUSED";
  u8g2_DrawStr(u, kX + 6, kY + 9, title);
  char buf[20];
  int held = 0;
  for (uint8_t i = 0; i < kArtifactCount; i++) held += hasArtifact(i) ? 1 : 0;
  std::snprintf(buf, sizeof(buf), "%d/%d  $%u", held, kArtifactCount,
                static_cast<unsigned>(gPlayer.silver));
  u8g2_DrawStr(u, kX + kW - 6 - u8g2_GetStrWidth(u, buf), kY + 9, buf);
  u8g2_DrawHLine(u, kX + 2, kY + 12, kW - 4);

  const char* labels[kMenuMaxItems] = {nullptr, nullptr, nullptr};
  int n = menuItems(mode, labels);
  for (int i = 0; i < n; i++) {
    std::snprintf(buf, sizeof(buf), "%s%s", i == gMenuCursor ? "> " : "  ", labels[i]);
    u8g2_DrawStr(u, kX + 6, kY + 22 + i * 8, buf);
  }
}

// ── LED minimap ──────────────────────────────────────────────────────────
//
// One LED per room, indexed like the world (row * 8 + col). The player's
// room is a steady pixel; the quest room blinks at 1 Hz. When both are the
// same room the player pixel itself pulses between the two brightnesses so
// the "you're here" signal is never lost.
void fillMinimap(uint8_t pixels[kRoomCount], uint32_t now) {
  bool blinkOn = (now % kQuestBlinkPeriodMs) < kQuestBlinkPeriodMs / 2;
  int quest = questRoom();
  std::memset(pixels, 0, kRoomCount);
  if (quest >= 0 && quest != gPlayer.room && blinkOn) {
    pixels[quest] = kMinimapQuestBrightness;
  }
  uint8_t pb = kMinimapPlayerBrightness;
  if (quest == gPlayer.room && !blinkOn) pb = kMinimapQuestBrightness;
  pixels[gPlayer.room] = pb;
}

}  // namespace

// ── Public API ───────────────────────────────────────────────────────────

void gameBegin(const SaveData* save, uint32_t now) {
  gPlayer = Player();
  gMsgCount = 0;
  gMsgIndex = 0;
  gAfterMessage = Mode::kPlay;
  gMenuCursor = 0;
  gMenuStickDir = 0;
  gLastTickMs = now;
  gSaveDirty = false;
  gRng = now | 1;
  if (save) {
    gPlayer.room = save->room < kRoomCount ? save->room : kStartRoom;
    gPlayer.hp = save->hp;
    gPlayer.silver = save->silver;
    gPlayer.arts = save->arts;
    gPlayer.weapon = save->weapon < kWeaponCount ? save->weapon : uint8_t(kWeaponSword);
    gPlayer.won = save->won;
    gPlayer.entryX = save->entryX;
    gPlayer.entryY = save->entryY;
    gPlayer.loot = save->loot;
    if (gPlayer.hp <= 0 || gPlayer.hp > playerMaxHp()) gPlayer.hp = playerMaxHp();
    gMode = Mode::kPlay;
    gInvulnUntilMs = 0;
    gGateSeen[0] = gGateSeen[1] = false;
    enterRoom(gPlayer.room, gPlayer.entryX, gPlayer.entryY);
  } else {
    newGame(now);
  }
}

SaveData gameSave() {
  SaveData s;
  s.room = gPlayer.room;
  s.hp = static_cast<uint8_t>(gPlayer.hp);
  s.silver = gPlayer.silver;
  s.arts = gPlayer.arts;
  s.weapon = gPlayer.weapon;
  s.won = gPlayer.won;
  s.entryX = static_cast<uint8_t>(gPlayer.entryX);
  s.entryY = static_cast<uint8_t>(gPlayer.entryY);
  s.loot = gPlayer.loot;
  return s;
}

bool gameTakeSaveDirty() {
  bool d = gSaveDirty;
  gSaveDirty = false;
  return d;
}

bool gameStep(const GameInput& in, uint32_t now) {
  float dtS = (now - gLastTickMs) / 1000.0f;
  gLastTickMs = now;
  if (dtS > 0.1f) dtS = 0.1f;  // don't let a stall teleport things

  if (gMode == Mode::kMessage) {
    if (in.b || in.a) advanceMessage();
    return false;
  }

  if (gMode != Mode::kPlay) {
    const char* labels[kMenuMaxItems];
    int n = menuItems(gMode, labels);

    // Latch the stick's nav direction so a held tilt doesn't repeat every
    // frame; released once it returns to neutral.
    constexpr float kStickHi = 0.5f, kStickLo = 0.2f;
    float yDir = in.stickY;
    int8_t stickDir = yDir < -kStickHi ? -1 : yDir > kStickHi ? 1 : 0;
    if (stickDir != 0 && gMenuStickDir == 0) {
      gMenuCursor = static_cast<uint8_t>((gMenuCursor + stickDir + n) % n);
    }
    if (fabsf(yDir) < kStickLo) {
      gMenuStickDir = 0;
    } else if (stickDir != 0) {
      gMenuStickDir = stickDir;
    }
    if (in.x) gMenuCursor = static_cast<uint8_t>((gMenuCursor + 1) % n);

    if (in.a && gMode == Mode::kPause) {
      gMode = Mode::kPlay;
      return false;
    }
    if (!in.b) return false;
    const char* pick = labels[gMenuCursor];
    if (std::strcmp(pick, "Resume") == 0) {
      gMode = Mode::kPlay;
    } else if (std::strcmp(pick, "Respawn") == 0) {
      respawn(now);
    } else if (std::strcmp(pick, "New Game") == 0) {
      newGame(now);
    } else {
      return true;
    }
    return false;
  }

  if (in.a) {
    gMode = Mode::kPause;
    gMenuCursor = 0;
    gMenuStickDir = 0;
    return false;
  }
  stepPlay(in, dtS, now);
  return false;
}

void gameDraw(u8g2_t* u, uint32_t now) {
  u8g2_SetDrawColor(u, 1);
  drawRoomTiles(u, gPlayer.room);
  drawRoomObjects(u);
  for (const Enemy& e : gEnemies) {
    if (e.alive) drawEnemy(u, e, now);
  }
  drawShots(u);
  if (gMode != Mode::kGameOver) drawPlayer(u, now);
  drawFooter(u);

  switch (gMode) {
    case Mode::kMessage:
      drawMessageBox(u);
      break;
    case Mode::kPause:
    case Mode::kGameOver:
    case Mode::kVictory:
      drawMenu(u, gMode);
      break;
    default:
      break;
  }
}

void gameMinimap(uint8_t pixels[kRoomCount], uint32_t now) { fillMinimap(pixels, now); }

GameStatus gameStatus() {
  GameStatus st;
  st.room = gPlayer.room;
  st.x = gPlayer.x;
  st.y = gPlayer.y;
  st.hp = gPlayer.hp;
  st.silver = gPlayer.silver;
  st.arts = gPlayer.arts;
  st.weapon = gPlayer.weapon;
  st.won = gPlayer.won;
  st.mode = static_cast<uint8_t>(gMode);
  st.enemies = 0;
  for (const Enemy& e : gEnemies) st.enemies += e.alive ? 1 : 0;
  return st;
}

}  // namespace helgrind
