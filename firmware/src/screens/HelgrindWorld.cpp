#include "HelgrindWorld.h"

namespace helgrind {

bool tileSolid(char t) {
  return t == kTileWall || t == kTileTree || t == kTileWater || t == kTileRune ||
         t == kTileMound;
}

// ── Room templates ─────────────────────────────────────────────────────
//
// Every template keeps the same border shape: walls on all four sides with
// a 2-tile doorway centred on each. Interiors must leave a walkable path
// between all four doors, since world connectivity is always open (the
// artifact chain gates on content, never on movement). Rooms flagged
// kRoomFlipX draw these mirrored, which the symmetric doors allow.

namespace {

const char* const kTemplates[kTemplateCount][kRoomRows] = {
    // kTmplMeadow
    {"#######..#######",
     "#..............#",
     "...,......,.....",
     "......,.........",
     "#....,.....,...#",
     "#######..#######"},
    // kTmplForest
    {"#######..#######",
     "#.T....T.....T.#",
     "...T.......T....",
     "..T....T.T......",
     "#.....T.....T..#",
     "#######..#######"},
    // kTmplCave
    {"#######..#######",
     "###..........###",
     "................",
     ".......##.......",
     "###.........####",
     "#######..#######"},
    // kTmplLonghouse
    {"#######..#######",
     "#..#========#..#",
     "...=========....",
     "...=========....",
     "#..#========#..#",
     "#######..#######"},
    // kTmplShrine
    {"#######..#######",
     "#...r......r...#",
     "......r.r.......",
     "......r.r.......",
     "#...r......r...#",
     "#######..#######"},
    // kTmplLake — a mere with a ford through the middle
    {"#######..#######",
     "#..............#",
     ".....~~~..~~....",
     "....~~~~..~~~...",
     "#..............#",
     "#######..#######"},
    // kTmplFjord
    {"#######..#######",
     "#~~..........~~#",
     "................",
     "................",
     "#~~..........~~#",
     "#######..#######"},
    // kTmplGate — the arena. Rune pillars, bones, nothing to hide behind.
    {"#######..#######",
     "#..r..x.....r..#",
     "......x.x.......",
     ".....x...x......",
     "#..r........r..#",
     "#######..#######"},
    // kTmplVillage
    {"#######..#######",
     "#.#==#....#==#.#",
     "..#==#....#==#..",
     "................",
     "#..............#",
     "#######..#######"},
    // kTmplBarrow — grave mounds in a loose ring
    {"#######..#######",
     "#.mm......mm...#",
     "....m..mm..m....",
     "..m....mm....m..",
     "#.mm.x....xmm..#",
     "#######..#######"},
    // kTmplRuin — a burnt longhouse, walls half gone
    {"#######..#######",
     "#.#==#...#=x=#.#",
     "..#=====.=.=.=..",
     "....=x=====.#...",
     "#.#==#...x...#.#",
     "#######..#######"},
    // kTmplBridge — a river crossed by planks
    {"#######..#######",
     "#~~~~~~==~~~~~~#",
     "........==......",
     "........==......",
     "#~~~~~~==~~~~~~#",
     "#######..#######"},
    // kTmplGrove — a clearing round a rune stone
    {"#######..#######",
     "#T.T........T.T#",
     "...T...r.....T..",
     "....T......T....",
     "#T....T..T....T#",
     "#######..#######"},
};

// Speeds are a third or less of the player's 40 px/s: every fight is one
// you can walk away from, and even wolves are outpaced. Tuned down twice
// on device play (2026-09-20).
const EnemyDef kEnemyDefs[kEnemyKindCount] = {
    {"none", 0, 0, 0.0f, 0, 0, 0, 0},
    {"wolf", 2, 1, 13.0f, 7, 4, 1, 36},
    {"draugr", 4, 2, 7.0f, 6, 7, 2, 44},
    {"troll", 10, 4, 5.0f, 8, 8, 4, 56},
    {"Hrafn", 40, 5, 7.0f, 10, 10, 20, 0},
};

// ── Story text ───────────────────────────────────────────────────────────

// What the village believes: Hrafn the ferryman drowned the night the mere
// froze. What happened: he rowed the dead north, shut the gate behind them
// and stayed. The cold took him, and what is left of him is what holds the
// gate open now. The elder has known all along and told the kinder story.
// The player learns it in pieces — fisher, scrolls, hermit — and the elder
// admits it when handing over the last scroll.
const Page kIntro[] = {
    {"HELGRIND", {"The winter has not broken.", "The gate at the corner of",
                  "the world stands open."}},
    {"HELGRIND", {"The dead walk south. Those", "who went to look did not",
                  "come back. Now it is you."}},
    {"HELGRIND", {"Someone shut it once and", "left a trail for whoever",
                  "came next. Follow it."}},
    {"Controls", {"B attacks. X cycles", "weapons. Y talks or",
                  "reads. A opens menu."}},
};

const Page kEnding[] = {
    {"HELGRIND FALLS", {"Hrafn falls, and the gate", "with him. The snow stops",
                        "before you reach the wood."}},
    {"HELGRIND FALLS", {"You could tell it true.", "You won't. The village",
                        "keeps the kinder one."}},
    {"HELGRIND FALLS", {"You walk south. The mead", "hall is lit.", "The End."}},
};

// Shown on the first visit to Helgrind each session: one page if the chain
// isn't complete, one when Hrafn is there to fight.
const Page kGateWaiting = {"Helgrind", {"The gate stands open.", "Something stands in it,",
                                        "waiting. Not for you. Yet."}};
const Page kGateBoss = {"Hrafn", {"He turns. Whatever he was", "waiting for, it was",
                                  "not you."}};

// Rune stones. Keyed by room; the default is any other stone.
struct RunePage { uint8_t room; Page page; };
const RunePage kRuneStones[] = {
    {52, {"Runestone", {"Fresh cuts over the old:", "'Gone north to look.' A",
                        "cold fire below it."}}},
    {17, {"Runestone", {"Claw marks around the", "base. Wolves sharpen on",
                        "it. Move on."}}},
    {22, {"Runestone", {"The cairns are older than", "the stone. The stone",
                        "only says: HERE."}}},
    {21, {"Runestone", {"Nine stones for nine", "wardens. Hrafn's is not",
                        "here. Not yet."}}},
    {7, {"Gateposts", {"The runes run upward, off", "the top of the stone.",
                       "They are not for reading."}}},
};
const Page kRuneStoneDefault = {"Runestone", {"Old marks, half weathered.", "A name. Someone raised",
                                              "this for them, once."}};

// Chain order is the play order: index 0 must be held before index 1 can
// be picked up, and so on. Each scroll is in Hrafn's hand and points at
// the next thing; the elder fills in the gaps between.
const ArtifactDef kArtifacts[kArtifactCount] = {
    {"Woodsman's Axe", kArtWeapon, kWeaponAxe, 2,
     {{"Woodsman's Axe", {"Left in a stump beside a", "cold fire. The woodsman",
                          "was the first to go look."}},
      {"Woodsman's Axe", {"A throwing axe, still", "sharp. (X to arm it)",
                          nullptr}}}},
    {"Ferryman's Scroll", kArtScroll, 0, 2,
     {{"Ferryman's Scroll", {"\"Rowed the last of them", "north the night the mere",
                             "froze. Do not mourn me."}},
      {"Ferryman's Scroll", {"The bear's tooth I hid in", "the western cave. No one",
                             "should wear it lightly. -H\""}}}},
    {"Bear Amulet", kArtAmulet, 0, 2,
     {{"Bear Amulet", {"A tooth on a cord.", "Something in the dark",
                       "growls, then goes quiet."}},
      {"Bear Amulet", {"Your heart beats harder.", "(+4 max health)", nullptr}}}},
    {"Yew Bow", kArtWeapon, kWeaponBow, 1,
     {{"Yew Bow", {"Strung with a gatewarden's", "hair, the carving says.",
                   "Arrows fly the whole room."}},
      {nullptr, {nullptr, nullptr, nullptr}}}},
    {"Rune Scroll", kArtScroll, 0, 2,
     {{"Rune Scroll", {"The stones on the shore", "say one word, over and",
                       "over: COLD."}},
      {"Rune Scroll", {"\"The frost charm I left in", "the hollow northwest. The",
                       "trolls owe me that much. -H\""}}}},
    {"Frost Amulet", kArtAmulet, 0, 1,
     {{"Frost Amulet", {"It does not melt.", "Neither, now, will you.",
                        "(halves damage taken)"}},
      {nullptr, {nullptr, nullptr, nullptr}}}},
    // Written to the elder the night Hrafn left; the elder kept it.
    {"Gate Scroll", kArtScroll, 0, 2,
     {{"Gate Scroll", {"\"Old friend. Tell them I", "drowned. It is kinder",
                       "than the gate."}},
      {"Gate Scroll", {"Shut from the far side, a", "door held that way never",
                       "shuts true. I am the gap. -H\""}}}},
};

const char* const kNpcNames[] = {"", "Merchant", "Elder", "Skald", "Fisher", "Hermit"};

// Elder: one nudge per chain stage. Stage 6 is the hand-over of the Gate
// Scroll and gets a preamble page; the scroll's own pages follow it.
const Page kElder[] = {
    {"Elder", {"\"The dead walk down from", "the north. The woodsman",
               "went south. Start there.\""}},
    {"Elder", {"\"An axe is a start. The", "ferryman kept a writing.",
               "South-east, the far mere.\""}},
    {"Elder", {"\"Hrafn's hand, that", "writing. Do as it says.",
               "West, into the caves.\""}},
    {"Elder", {"\"The bear's tooth. Good.", "North: a shrine keeps a",
               "bow none dare draw.\""}},
    {"Elder", {"\"South-east, to the fjord.", "Read what the stones",
               "say. Then come back.\""}},
    {"Elder", {"\"Northwest, the hollow", "under the hill. Bring",
               "what does not melt.\""}},
    {"Elder", {"\"He gave me this the night", "he left. I told them he",
               "drowned. It was kinder.\""}},
    {"Elder", {"\"You carry all seven.", "Hrafn waits at the corner",
               "of the world. Go north.\""}},
};
const Page kElderWon = {"Elder", {"\"The gate is quiet. Sit.", "Eat. You have earned",
                                  "the spring.\""}};

const Page kSkald[] = {
    {"Skald", {"\"The gate opened the night", "Hrafn the ferryman",
               "drowned. Ask the elder.\""}},
    {"Skald", {"\"An axe, and a walk in", "the woods. I'll make a",
               "verse of it. A short one.\""}},
    {"Skald", {"\"A bear's tooth on a cord.", "Now that is a verse.\"", nullptr}},
    {"Skald", {"\"You smell of the fjord.", "The dead smell of it",
               "too. Hurry.\""}},
    {"Skald", {"\"The elder's face this", "morning. He told you,",
               "then. Go. I'll start the end.\""}},
};
const Page kSkaldWon = {"Skald", {"\"I have it as twelve", "verses. The village",
                                  "prefers the short one.\""}};

const Page kFisher[] = {
    {"Fisher", {"\"The mere froze in one", "night. I heard rowing",
                "under the ice.\""}},
    {"Fisher", {"\"Drowned, they say. A man", "who rows the dead all",
                "winter learns the ice.\""}},
    {"Fisher", {"\"The ice is thinner today.", "Something is done up",
                "north. Or nearly.\""}},
};
const Page kFisherWon = {"Fisher", {"\"Ice is off the mere.", "First time in a year.",
                                    "I'll not ask why.\""}};

const Page kHermit[] = {
    {"Hermit", {"\"Trolls don't sleep. They", "wait. Whatever you are",
                "after, it is not in here.\""}},
    {"Hermit", {"\"The cold in the hollow", "is not weather. Wear",
                "the bear before you go.\""}},
    {"Hermit", {"\"Hrafn was my brother.", "The elder knows more than",
                "he tells. Ask. Be quick.\""}},
};
const Page kHermitWon = {"Hermit", {"\"So. Quick, then.", "Thank you.\"", nullptr}};

// 8x8 world, row-major from the north-west corner. Difficulty roughly
// rises with distance from the village in the south; each artifact sits
// a little farther out than the one before it so the chain pulls the
// player across the whole map. Loot caches sit in the rooms the chain
// doesn't pass through, so wandering pays.
#define R(name, tmpl, flags, e0, e1, e2, art, npc, loot) \
  { name, tmpl, flags, {e0, e1, e2}, art, npc, loot }
constexpr uint8_t F = kRoomFlipX;
constexpr uint8_t N = kRoomWallN, S = kRoomWallS, W = kRoomWallW, E = kRoomWallE;
const RoomDef kRooms[kRoomCount] = {
    // Row 0 — north edge
    R("Deep Under", kTmplCave, 0, kEnemyTroll, kEnemyTroll, kEnemyNone, 0, kNpcNone, 0),
    R("Troll Warren", kTmplCave, F | E, kEnemyTroll, kEnemyDraugr, kEnemyNone, 0, kNpcNone, 8),
    R("Hanged Wood", kTmplForest, F, kEnemyDraugr, kEnemyDraugr, kEnemyNone, 0, kNpcNone, 0),
    R("Hanged Wood", kTmplForest, 0, kEnemyDraugr, kEnemyWolf, kEnemyWolf, 0, kNpcNone, 0),
    R("Old Mine", kTmplCave, W | E, kEnemyTroll, kEnemyDraugr, kEnemyNone, 0, kNpcNone, 6),
    R("Barrow Field", kTmplBarrow, 0, kEnemyDraugr, kEnemyDraugr, kEnemyWolf, 0, kNpcNone, 0),
    R("Warden's Steps", kTmplRuin, S, kEnemyTroll, kEnemyTroll, kEnemyNone, 0, kNpcNone, 0),
    R("Helgrind", kTmplGate, S, kEnemyNone, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    // Row 1
    R("Cold Cave", kTmplCave, 0, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Frost Hollow", kTmplCave, F | E | S, kEnemyTroll, kEnemyNone, kEnemyNone, 6, kNpcNone, 0),
    R("Wolf Wood", kTmplForest, 0, kEnemyWolf, kEnemyWolf, kEnemyNone, 0, kNpcNone, 0),
    R("High Barrow", kTmplBarrow, F, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Stone Throat", kTmplCave, W | E, kEnemyTroll, kEnemyNone, kEnemyNone, 0, kNpcNone, 5),
    R("Barrow Wood", kTmplForest, F, kEnemyDraugr, kEnemyWolf, kEnemyNone, 0, kNpcNone, 0),
    R("North Fjord", kTmplFjord, 0, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Ice Shore", kTmplFjord, F | W, kEnemyTroll, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    // Row 2
    R("Wolf Wood", kTmplForest, F, kEnemyWolf, kEnemyDraugr, kEnemyNone, 0, kNpcNone, 0),
    R("Wolf Run", kTmplGrove, 0, kEnemyWolf, kEnemyWolf, kEnemyNone, 0, kNpcNone, 0),
    R("Trader's Hall", kTmplLonghouse, 0, kEnemyNone, kEnemyNone, kEnemyNone, 0, kNpcMerchant, 0),
    R("High Meadow", kTmplMeadow, 0, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Pine Wood", kTmplForest, 0, kEnemyWolf, kEnemyWolf, kEnemyNone, 0, kNpcNone, 0),
    R("Rune Shrine", kTmplShrine, 0, kEnemyDraugr, kEnemyDraugr, kEnemyNone, 4, kNpcNone, 0),
    R("Old Cairns", kTmplGrove, F, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 4),
    R("East Fjord", kTmplFjord, 0, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    // Row 3
    R("Hermit's Cave", kTmplCave, 0, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcHermit, 4),
    R("Heath", kTmplMeadow, F, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Pine Wood", kTmplForest, 0, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Cairn Heath", kTmplBarrow, F, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Elder's Hall", kTmplLonghouse, 0, kEnemyNone, kEnemyNone, kEnemyNone, 7, kNpcElder, 0),
    R("Pine Wood", kTmplForest, F, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("River Ford", kTmplBridge, 0, kEnemyWolf, kEnemyWolf, kEnemyNone, 0, kNpcNone, 0),
    R("East Fjord", kTmplFjord, F, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    // Row 4
    R("West Cave", kTmplCave, 0, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Bear Cave", kTmplCave, F | N | S | E, kEnemyTroll, kEnemyNone, kEnemyNone, 3, kNpcNone, 0),
    R("Pine Wood", kTmplForest, F, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Burnt Hall", kTmplRuin, 0, kEnemyNone, kEnemyNone, kEnemyNone, 0, kNpcNone, 5),
    R("Heath", kTmplMeadow, 0, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Pine Wood", kTmplForest, 0, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Fisher's Mere", kTmplLake, 0, kEnemyNone, kEnemyNone, kEnemyNone, 0, kNpcFisher, 0),
    R("East Fjord", kTmplFjord, W, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    // Row 5
    R("West Cave", kTmplCave, F, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 3),
    R("Birch Wood", kTmplForest, 0, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Low Meadow", kTmplMeadow, 0, kEnemyNone, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Low Meadow", kTmplMeadow, F, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Birch Wood", kTmplForest, F, kEnemyWolf, kEnemyWolf, kEnemyNone, 0, kNpcNone, 0),
    R("River Ford", kTmplBridge, 0, kEnemyDraugr, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Mere", kTmplLake, 0, kEnemyNone, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Rune Fjord", kTmplFjord, 0, kEnemyDraugr, kEnemyNone, kEnemyNone, 5, kNpcNone, 0),
    // Row 6
    R("Birch Wood", kTmplForest, F, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Sheepfold", kTmplMeadow, N | E, kEnemyNone, kEnemyNone, kEnemyNone, 0, kNpcNone, 2),
    R("Low Meadow", kTmplMeadow, 0, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Birch Wood", kTmplForest, 0, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Woodsman's Rest", kTmplGrove, 0, kEnemyWolf, kEnemyWolf, kEnemyNone, 1, kNpcNone, 0),
    R("Low Meadow", kTmplMeadow, F, kEnemyNone, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Mere", kTmplLake, F, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("South Fjord", kTmplFjord, W, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    // Row 7 — south edge, the village
    R("Old Barrow", kTmplBarrow, 0, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 4),
    R("Low Meadow", kTmplMeadow, 0, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Mead Hall", kTmplLonghouse, 0, kEnemyNone, kEnemyNone, kEnemyNone, 0, kNpcMerchant, 0),
    R("Village", kTmplVillage, 0, kEnemyNone, kEnemyNone, kEnemyNone, 0, kNpcSkald, 0),
    R("Low Meadow", kTmplMeadow, F, kEnemyNone, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Birch Wood", kTmplForest, 0, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
    R("Ferry Mere", kTmplLake, 0, kEnemyDraugr, kEnemyNone, kEnemyNone, 2, kNpcNone, 0),
    R("South Fjord", kTmplFjord, F, kEnemyWolf, kEnemyNone, kEnemyNone, 0, kNpcNone, 0),
};
#undef R

template <int N>
const Page& stagePage(const Page (&pages)[N], int stage, const int* cutoffs) {
  // cutoffs[i] is the lowest stage at which pages[i+1] applies.
  int idx = 0;
  for (int i = 0; i + 1 < N; i++) {
    if (stage >= cutoffs[i]) idx = i + 1;
  }
  return pages[idx];
}

}  // namespace

const char* templateRow(uint8_t tmpl, int row) {
  if (tmpl >= kTemplateCount) tmpl = kTmplMeadow;
  if (row < 0) row = 0;
  if (row >= kRoomRows) row = kRoomRows - 1;
  return kTemplates[tmpl][row];
}

const EnemyDef& enemyDef(uint8_t kind) {
  if (kind >= kEnemyKindCount) kind = kEnemyNone;
  return kEnemyDefs[kind];
}

const Page* introPages(int& count) {
  count = sizeof(kIntro) / sizeof(kIntro[0]);
  return kIntro;
}

const Page* endingPages(int& count) {
  count = sizeof(kEnding) / sizeof(kEnding[0]);
  return kEnding;
}

const Page* gatePage(bool bossPresent) {
  return bossPresent ? &kGateBoss : &kGateWaiting;
}

const Page* runeStonePage(int room) {
  for (const RunePage& r : kRuneStones) {
    if (r.room == room) return &r.page;
  }
  return &kRuneStoneDefault;
}

const ArtifactDef& artifactDef(uint8_t index) {
  if (index >= kArtifactCount) index = kArtifactCount - 1;
  return kArtifacts[index];
}

const char* npcName(uint8_t npc) {
  if (npc > kNpcHermit) npc = kNpcNone;
  return kNpcNames[npc];
}

const Page* npcPages(uint8_t npc, int stage, bool won, int& count) {
  count = 1;
  if (stage < 0) stage = 0;
  if (stage > kArtifactCount) stage = kArtifactCount;
  switch (npc) {
    case kNpcElder:
      if (won) return &kElderWon;
      return &kElder[stage];
    case kNpcSkald: {
      if (won) return &kSkaldWon;
      static const int cut[] = {1, 3, 5, 7};
      return &stagePage(kSkald, stage, cut);
    }
    case kNpcFisher: {
      if (won) return &kFisherWon;
      static const int cut[] = {2, 5};
      return &stagePage(kFisher, stage, cut);
    }
    case kNpcHermit: {
      if (won) return &kHermitWon;
      static const int cut[] = {3, 6};
      return &stagePage(kHermit, stage, cut);
    }
    default:
      count = 0;
      return nullptr;
  }
}

const RoomDef& roomDef(int roomIndex) {
  if (roomIndex < 0 || roomIndex >= kRoomCount) roomIndex = kStartRoom;
  return kRooms[roomIndex];
}

}  // namespace helgrind
