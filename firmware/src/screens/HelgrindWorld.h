#pragma once
#include <stdint.h>

// ─── Helgrind world data ────────────────────────────────────────────────────
//
// Static, hand-authored content for the Helgrind game: the 8x8 room grid,
// per-room tile templates, the linear artifact chain, enemy archetypes,
// and every line of story text (intro, scrolls, NPC dialogue, ending).
// Everything here is const data — the runtime state that plays out over
// it (player position, which artifacts are held, live enemies) lives in
// HelgrindGame.cpp.
//
// Room-index convention: room = row * 8 + col, row 0 is the north edge of
// the world, col 0 the west edge. That maps directly to the LED matrix
// minimap as setPixel(col, row), one LED per room — see helgrind.md's
// "one LED pixel per room" pillar.
//
// The story, in one breath: the winter hasn't broken; the gate at the
// north-east corner of the world (Helgrind) has opened and the dead walk
// south. Hrafn, the last gatewarden, shut it once and stayed to hold it —
// the cold took him, and he is the "warden" at the end. The seven things
// he left behind, found in the order he left them, are what gets the
// player to him.

namespace helgrind {

constexpr int kWorldCols = 8;
constexpr int kWorldRows = 8;
constexpr int kRoomCount = kWorldCols * kWorldRows;

// Each room is a fixed 16x6 grid of 8px tiles (128x48 px), drawn in the
// band above the footer. Room templates are 6 strings of 16 tile chars.
constexpr int kRoomCols = 16;
constexpr int kRoomRows = 6;
constexpr int kTilePx = 8;

// Tile chars used by templates. Doorways are the always-open gaps in the
// border walls (columns 7-8 on the north/south edge, rows 2-3 on the
// east/west edge); the renderer closes them on the world's outer edge.
constexpr char kTileFloor = '.';
constexpr char kTileWall = '#';
constexpr char kTileTree = 'T';
constexpr char kTileWater = '~';
constexpr char kTileGrass = ',';
constexpr char kTileRune = 'r';
constexpr char kTilePlank = '=';
constexpr char kTileMound = 'm';   // barrow mound, solid
constexpr char kTileBones = 'x';   // decor, walkable

bool tileSolid(char t);

enum Template : uint8_t {
  kTmplMeadow = 0,
  kTmplForest,
  kTmplCave,
  kTmplLonghouse,
  kTmplShrine,
  kTmplLake,
  kTmplFjord,
  kTmplGate,
  kTmplVillage,
  kTmplBarrow,
  kTmplRuin,
  kTmplBridge,
  kTmplGrove,
  kTemplateCount,
};

const char* templateRow(uint8_t tmpl, int row);

// ── Enemies ─────────────────────────────────────────────────────────────
enum EnemyKind : uint8_t {
  kEnemyNone = 0,
  kEnemyWolf,
  kEnemyDraugr,
  kEnemyTroll,
  kEnemyBoss,
  kEnemyKindCount,
};

struct EnemyDef {
  const char* name;
  uint8_t hp;
  uint8_t contactDamage;
  float speedPxPerS;
  uint8_t w, h;      // hitbox/sprite size in px
  uint8_t silver;    // reward on kill
  // Beyond this distance the enemy ignores the player and just shifts
  // tile to tile now and then; inside it (or once hurt) it gives chase.
  // 0 = always chasing.
  uint8_t aggroPx;
};

const EnemyDef& enemyDef(uint8_t kind);

// ── Text pages ───────────────────────────────────────────────────────────
//
// Every piece of story text is a Page: a title and up to three short
// lines (at most 117px in FONT_TINY, roughly 26 chars — what fits the
// message box). Multi-page text is a contiguous array of Pages.
struct Page {
  const char* title;
  const char* lines[3];
};

const Page* introPages(int& count);
const Page* endingPages(int& count);
// One page for arriving at Helgrind: the empty gate, or Hrafn in it.
const Page* gatePage(bool bossPresent);
// What a rune stone (kTileRune) says when read with the action button:
// a few rooms have their own, the rest share one.
const Page* runeStonePage(int room);

// ── Artifacts: the linear chain ────────────────────────────────────────
enum ArtifactType : uint8_t { kArtWeapon, kArtAmulet, kArtScroll };

// Weapon ids double as the player's weapon-select order (x cycles them).
enum Weapon : uint8_t { kWeaponSword = 0, kWeaponAxe, kWeaponBow, kWeaponCount };

struct ArtifactDef {
  const char* name;
  ArtifactType type;
  uint8_t weapon;        // kArtWeapon: which Weapon this unlocks
  uint8_t pageCount;     // 1 or 2
  Page pages[2];         // pickup text; scrolls re-read the same pages
};

constexpr int kArtifactCount = 7;
const ArtifactDef& artifactDef(uint8_t index);

// ── NPCs ────────────────────────────────────────────────────────────────
//
// Merchants sell mead; everyone else talks. Dialogue is keyed on the
// player's chain stage (number of artifacts held, 0..7) plus a "won" flag,
// so each NPC's line moves with the story instead of repeating forever.
enum NpcKind : uint8_t {
  kNpcNone = 0,
  kNpcMerchant,
  kNpcElder,
  kNpcSkald,
  kNpcFisher,
  kNpcHermit,
};

const char* npcName(uint8_t npc);
// Returns the page an NPC says at this stage. `count` is how many
// consecutive pages to show (1 for most lines, more for the elder's big
// beats).
const Page* npcPages(uint8_t npc, int stage, bool won, int& count);

// The Gate Scroll (chain index 6) is handed over by the elder rather than
// lying on the floor — see HelgrindScreen's specialAction().
constexpr uint8_t kElderGivenArtifact = 6;

// The stage at which the hermit's dialogue (kHermit's cut[] = {3, 6} in
// HelgrindWorld.cpp) reaches its final line and hands over his one-time
// parting gift — see specialAction().
constexpr uint8_t kHermitGiftStage = 6;

// ── Rooms ────────────────────────────────────────────────────────────────
constexpr int kMaxRoomEnemies = 3;

// Room flags. kRoomFlipX mirrors the template left-to-right so the same
// thirteen layouts read as more than thirteen rooms; doorways are
// symmetric so mirroring never affects connectivity.
//
// kRoomWall* seal that edge's doorway. A closed edge only needs authoring
// on one of the two rooms — tileAt() also seals the neighbour's opposing
// door — so the table never has to keep both sides in sync. Closures are
// how the map gets shape (a cave complex with one mouth, a coast you walk
// along, a gate approached from one side); every room must still be
// reachable from the village, which the host-side check in
// firmware/host/helgrind/check-world.py verifies.
constexpr uint8_t kRoomFlipX = 1 << 0;
constexpr uint8_t kRoomWallN = 1 << 1;
constexpr uint8_t kRoomWallS = 1 << 2;
constexpr uint8_t kRoomWallW = 1 << 3;
constexpr uint8_t kRoomWallE = 1 << 4;

struct RoomDef {
  const char* name;
  uint8_t tmpl;
  uint8_t flags;
  uint8_t enemies[kMaxRoomEnemies];  // EnemyKind, kEnemyNone = empty slot
  uint8_t artifact;                  // 1-based index into the chain, 0 = none
  uint8_t npc;                       // NpcKind
  uint8_t loot;                      // silver in a one-time cache, 0 = none
};

const RoomDef& roomDef(int roomIndex);

constexpr int kStartRoom = 7 * kWorldCols + 3;  // village, south edge
constexpr int kGateRoom = 0 * kWorldCols + 7;   // Helgrind itself, NE corner

constexpr uint8_t kMeadPrice = 5;
constexpr uint8_t kMeadHeal = 6;

}  // namespace helgrind
