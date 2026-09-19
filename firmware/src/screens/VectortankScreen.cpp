#include "VectortankScreen.h"

#include <cmath>
#include <cstdio>
#include <Preferences.h>

#include "../hardware/Inputs.h"
#include "../hardware/LEDmatrix.h"
#include "../hardware/oled.h"
#include "../led/LEDAppRuntime.h"
#include "../ui/GUI.h"
#include "../ui/OLEDLayout.h"

extern LEDmatrix badgeMatrix;

namespace {

// ── Small local Vec3/scene model — no Vec3/Mat helper exists elsewhere in
// firmware/src (confirmed by research), so this mirrors vt_engine.py's
// Camera/box/particle model directly in plain float math. ──────────────────

struct Vec3 {
  float x, y, z;
};

struct Camera {
  float x = 0.0f;
  float z = -8.0f;
  float yaw = 0.0f;

  // Forward direction is (sin(yaw), cos(yaw)) throughout this file (move(),
  // strafe(), fireProjectile() all agree on it). toCameraSpace() must rotate
  // world deltas by the SAME +yaw so that a point straight ahead lands at
  // camera-space (0, 0, +depth) instead of drifting off at 2x yaw — an
  // earlier version rotated by -yaw here, which happened to look plausible
  // for small turns but silently diverged from the fired-shot direction.
  void move(float xDir, float yDir, float dtS, float turnRate = 2.2f,
            float moveRate = 6.0f) {
    if (xDir != 0.0f) yaw += turnRate * xDir * dtS;
    if (yDir != 0.0f) {
      float step = -moveRate * yDir * dtS;
      x += step * sinf(yaw);
      z += step * cosf(yaw);
    }
  }

  // dir > 0 strafes right, dir < 0 strafes left — moves the camera along
  // the world-space "right" vector (cos(yaw), -sin(yaw)), perpendicular to
  // forward, without changing yaw.
  void strafe(float dir, float dtS, float moveRate = 6.0f) {
    float step = moveRate * dir * dtS;
    x += step * cosf(yaw);
    z += -step * sinf(yaw);
  }

  Vec3 toCameraSpace(const Vec3& w) const {
    float dx = w.x - x;
    float dz = w.z - z;
    float cosY = cosf(yaw);
    float sinY = sinf(yaw);
    return {dx * cosY - dz * sinY, w.y - 1.0f, dx * sinY + dz * cosY};
  }
};

constexpr float kFovScale = 80.0f;
constexpr float kNear = 0.5f;
constexpr float kFar = 60.0f;
constexpr int kScreenW = 128;
constexpr int kScreenH = 64;
constexpr float kPi = 3.14159265f;

bool project(const Vec3& c, float& sx, float& sy) {
  if (c.z < kNear || c.z > kFar) return false;
  sx = kScreenW / 2.0f + (c.x / c.z) * kFovScale;
  sy = kScreenH / 2.0f - (c.y / c.z) * kFovScale;
  return true;
}

constexpr int kBoxEdges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
    {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

// Face index order used by kEdgeFaces/computeFaceVisibility below:
// 0=bottom, 1=top, 2=-z side, 3=+z side, 4=-x side, 5=+x side. Each of a
// box's 12 edges is the shared border of exactly 2 of those 6 faces —
// this is the adjacency, in kBoxEdges order.
constexpr int kEdgeFaces[12][2] = {
    {0, 2}, {0, 5}, {0, 3}, {0, 4}, {1, 2}, {1, 5},
    {1, 3}, {1, 4}, {2, 4}, {2, 5}, {3, 5}, {3, 4},
};

void boxVerts(float cx, float cz, float half, float height, Vec3 out[8]) {
  out[0] = {cx - half, 0.0f, cz - half};
  out[1] = {cx + half, 0.0f, cz - half};
  out[2] = {cx + half, 0.0f, cz + half};
  out[3] = {cx - half, 0.0f, cz + half};
  out[4] = {cx - half, height, cz - half};
  out[5] = {cx + half, height, cz - half};
  out[6] = {cx + half, height, cz + half};
  out[7] = {cx - half, height, cz + half};
}

constexpr int kMaxArtifacts = 128;
// Particles are explosion debris only now (no ambient dust), so the pool
// just needs enough headroom for a few bursts in flight at once — see
// kExplosionParticleCount below. Bumped from 64 alongside a bigger
// per-explosion burst count (see the "more spectacular" note there).
constexpr int kMaxParticles = 96;

struct Artifact {
  Vec3 verts[8];
  float cx, cz, half, height;
  bool alive;
};

struct Particle {
  float x, y, z;
  float vx, vy, vz;
  float age, lifetime;
  bool active;
  // Previous frame's position, so drawScene() can draw a short streak
  // (prev -> current) instead of a single pixel — see the particle-draw
  // comment in drawScene() for why that reads as much more of an
  // "explosion" than a static dot cloud at this speed/frame-rate.
  float px, py, pz;
};

// Small seeded LCG — reproducible scene layouts without pulling in <random>,
// mirroring vt_engine.py's random.seed(1) sweep runs (not bit-exact, just
// comparably-shaped scenes at each count).
uint32_t gRngState = 1;

void seedRng(uint32_t seed) { gRngState = seed ? seed : 1; }

float randUnit() {
  gRngState = gRngState * 1664525u + 1013904223u;
  return (gRngState >> 8) / static_cast<float>(1u << 24);
}

float randRange(float lo, float hi) { return lo + randUnit() * (hi - lo); }

// ── High score: a single persisted best-kill-count, no identity attached ──
//
// One scalar in its own NVS namespace, following this codebase's existing
// per-feature-namespace convention (see e.g. BadgeConfig's "badge_cfg",
// BadgeInfo's "badge_info") rather than piggybacking on a shared settings
// store. "vectortank"/"highscore" both stay well under the 15-character
// NVS key-length limit that trips up longer names elsewhere in this
// firmware. Opened and closed per call rather than held open, matching
// every other Preferences user in this codebase — this only writes on an
// actual new best, so call frequency is not a wear concern. Declared this
// early (well before stepOpponents() below, which is the first caller) so
// there's no forward-reference ordering issue.
int gKillCount = 0;
uint32_t gHighScore = 0;
constexpr const char* kNvsNamespace = "vectortank";
constexpr const char* kHighScoreKey = "highscore";

uint32_t loadHighScore() {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return 0;
  uint32_t v = prefs.getUInt(kHighScoreKey, 0);
  prefs.end();
  return v;
}

void saveHighScoreIfBetter(uint32_t kills) {
  if (kills <= gHighScore) return;
  gHighScore = kills;
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return;
  prefs.putUInt(kHighScoreKey, gHighScore);
  prefs.end();
}

// Bigger, faster, longer-lived bursts than the original tuning — after
// on-device testing the original 14-particle/short-lifetime burst read as
// a small dust puff rather than an explosion. Paired with the streak-draw
// trail below (instead of a single pixel per particle), this is the
// "more spectacular" pass.
constexpr int kExplosionParticleCount = 26;
constexpr float kExplosionSpeedMin = 2.5f;
constexpr float kExplosionSpeedMax = 5.5f;
constexpr float kExplosionGravity = 4.0f;
constexpr float kExplosionLifetimeMin = 0.35f;
constexpr float kExplosionLifetimeMax = 0.8f;

// Activates up to kExplosionParticleCount inactive slots in the pool,
// scattering them outward from (cx, cy, cz) — this is the only way
// particles ever become active; there's no ambient/idle spawning.
void spawnExplosion(Particle particles[], int maxCount, float cx, float cy,
                    float cz) {
  int spawned = 0;
  for (int i = 0; i < maxCount && spawned < kExplosionParticleCount; i++) {
    Particle& p = particles[i];
    if (p.active) continue;
    float vx = randRange(-1.0f, 1.0f);
    float vy = randRange(0.15f, 1.0f);  // biased upward, like debris/sparks
    float vz = randRange(-1.0f, 1.0f);
    float len = sqrtf(vx * vx + vy * vy + vz * vz);
    if (len < 1e-4f) len = 1.0f;
    float speed = randRange(kExplosionSpeedMin, kExplosionSpeedMax);
    p.x = cx;
    p.y = cy;
    p.z = cz;
    p.px = cx;
    p.py = cy;
    p.pz = cz;
    p.vx = vx / len * speed;
    p.vy = vy / len * speed;
    p.vz = vz / len * speed;
    p.age = 0.0f;
    p.lifetime = randRange(kExplosionLifetimeMin, kExplosionLifetimeMax);
    p.active = true;
    spawned++;
  }
}

void stepParticle(Particle& p, float dtS) {
  if (!p.active) return;
  p.px = p.x;
  p.py = p.y;
  p.pz = p.z;
  p.vy -= kExplosionGravity * dtS;
  p.x += p.vx * dtS;
  p.y += p.vy * dtS;
  p.z += p.vz * dtS;
  p.age += dtS;
  if (p.age >= p.lifetime || p.y < 0.0f) p.active = false;
}

int makeArtifacts(int count, Artifact out[], const Camera& cam,
                   float spread = 80.0f, float minDist = 6.0f) {
  count = count > kMaxArtifacts ? kMaxArtifacts : count;
  for (int i = 0; i < count; i++) {
    float dx, dz;
    do {
      dx = randRange(-spread, spread);
      dz = randRange(-spread, spread);
    } while (dx * dx + dz * dz < minDist * minDist);
    float cx = cam.x + dx;
    float cz = cam.z + dz;
    float half = randRange(0.8f, 1.8f);
    float height = randRange(1.5f, 4.0f);
    boxVerts(cx, cz, half, height, out[i].verts);
    out[i].cx = cx;
    out[i].cz = cz;
    out[i].half = half;
    out[i].height = height;
    out[i].alive = true;
  }
  return count;
}

// ── Solid-obstacle collision ────────────────────────────────────────────────
//
// Artifacts are durable obstacles now (a shot still explodes against one's
// bounding box — see the removed `a.alive = false` in stepProjectiles below
// — but no longer destroys it), so both the player and opponents need to be
// physically blocked by that same box rather than walking through it. This
// runs as position correction *after* a mover's desired x/z is computed,
// rather than a pre-move velocity clip: push the point straight out along
// whichever of the box's 4 sides it penetrated least, which is cheap (one
// AABB overlap test, expanded by the mover's own radius, per artifact) and
// good enough at this scale — it doesn't handle a mover embedded past a
// box's center, but neither player nor opponent movement is fast enough
// relative to typical box half-widths (0.8-1.8) to tunnel that deep in one
// frame.
void resolveArtifactCollision(float& x, float& z, float radius,
                              const Artifact artifacts[], int count) {
  for (int i = 0; i < count; i++) {
    const Artifact& a = artifacts[i];
    if (!a.alive) continue;
    float minX = a.cx - a.half - radius;
    float maxX = a.cx + a.half + radius;
    float minZ = a.cz - a.half - radius;
    float maxZ = a.cz + a.half + radius;
    if (x <= minX || x >= maxX || z <= minZ || z >= maxZ) continue;

    float penLeft = x - minX;
    float penRight = maxX - x;
    float penBottom = z - minZ;
    float penTop = maxZ - z;
    float minPen = fminf(fminf(penLeft, penRight), fminf(penBottom, penTop));
    if (minPen == penLeft) {
      x = minX;
    } else if (minPen == penRight) {
      x = maxX;
    } else if (minPen == penBottom) {
      z = minZ;
    } else {
      z = maxZ;
    }
  }
}

// ── Opponents: billboard stick figures ─────────────────────────────────────
//
// Unlike artifacts, opponents don't get real 3D geometry — they're drawn as
// a fixed local 2D stick-figure pose (head/spine/arms/legs), always facing
// the camera regardless of their own movement, scaled by 1/depth like the
// projectile shrink-with-altitude trick already does. That keeps per-
// opponent cost to one toCameraSpace()/project() call (same as a particle)
// instead of the 8-vertex-per-object cost artifacts pay, and sidesteps
// needing a convex-hull silhouette for a non-convex figure. See
// components/vectortank.md's opponent-scoping notes for why a billboard was
// chosen over a fully-3D skeleton.

// kMaxOpponents is the pool ceiling, not the starting count — the spawner
// (see stepSpawner()/spawnOneOpponent() near the bottom of this file) fills
// it in gradually as the player racks up kills, so this needs real headroom
// beyond kActiveOpponentCount's easier initial wave.
constexpr int kMaxOpponents = 14;
constexpr int kActiveOpponentCount = 4;
// Vertical hit-test bound, deliberately NOT tied to the opponent's visual
// height (kOppHeadY etc. below): a shot launches at kShotSpawnHeight=1.0
// and arcs up to kShotApexHeight~1.85 before falling — tying this to the
// (much shorter) visual figure height made opponents nearly unhittable,
// since the shot spent almost no time below it. This stays generous enough
// to cover the whole arc regardless of how tall the figure is drawn.
constexpr float kOpponentHitHeight = 2.0f;
constexpr float kOpponentHitHalfWidth = 0.45f;  // shot hit-test footprint
constexpr float kOpponentMeleeRange = 2.0f;    // player-damage contact range
constexpr float kOpponentSpeed = 2.0f;         // world units/sec, walking
constexpr float kOpponentSpawnMinDist = 20.0f;
constexpr float kOpponentSpawnMaxDist = 60.0f;
constexpr float kOpponentMaxHealth = 30.0f;
constexpr float kOpponentShotDamage = 10.0f;
constexpr float kOpponentContactDamagePerSec = 25.0f;
constexpr float kOpponentWalkPhaseSpeed = 5.0f;      // rad/s while walking
constexpr float kOpponentIdlePhaseSpeed = 5.0f * 0.2f;  // slow sway in melee range
constexpr float kOpponentCollisionRadius = 0.4f;         // blocked by artifact boxes
constexpr float kOpponentHitFlashS = 0.15f;
constexpr float kOpponentDeathDurationS = 0.3f;

struct Opponent {
  float x, z;
  float health;
  float walkPhase;
  float hitFlashS;
  float deathT;  // < 0 while alive-and-not-dying; counts up once dying
  bool alive;
};

// Spawned in a ring around the camera rather than makeArtifacts()'s scatter,
// since an opponent's whole point is to walk in from a distance — a uniform
// scatter (some of which could land right next to the player) wouldn't read
// as an approach.
int makeOpponents(int count, Opponent out[], const Camera& cam) {
  count = count > kMaxOpponents ? kMaxOpponents : count;
  for (int i = 0; i < count; i++) {
    float angle = randRange(0.0f, 2.0f * kPi);
    float dist = randRange(kOpponentSpawnMinDist, kOpponentSpawnMaxDist);
    out[i].x = cam.x + dist * sinf(angle);
    out[i].z = cam.z + dist * cosf(angle);
    out[i].health = kOpponentMaxHealth;
    // Staggered so opponents don't all step in lockstep.
    out[i].walkPhase = randRange(0.0f, 2.0f * kPi);
    out[i].hitFlashS = 0.0f;
    out[i].deathT = -1.0f;
    out[i].alive = true;
  }
  return count;
}

// Walks each opponent toward the camera; once within melee range it stops
// advancing (idle sway instead) and drains playerHealth every tick it stays
// there, clamped at 0 — there's no death/game-over state for the player yet,
// matching the existing "no win condition, scene just stays empty" precedent
// this file already has for artifacts.
void stepOpponents(Opponent opponents[], int count, float dtS, const Camera& cam,
                   float& playerHealth, const Artifact artifacts[], int artifactCount) {
  for (int i = 0; i < count; i++) {
    Opponent& o = opponents[i];
    if (!o.alive) continue;
    if (o.deathT >= 0.0f) {
      o.deathT += dtS;
      if (o.deathT >= kOpponentDeathDurationS) {
        o.alive = false;
        gKillCount++;
        saveHighScoreIfBetter(static_cast<uint32_t>(gKillCount));
      }
      continue;
    }
    if (o.hitFlashS > 0.0f) o.hitFlashS -= dtS;

    float dx = cam.x - o.x;
    float dz = cam.z - o.z;
    float distSq = dx * dx + dz * dz;
    if (distSq > kOpponentMeleeRange * kOpponentMeleeRange) {
      float dist = sqrtf(distSq);
      float invDist = dist > 1e-4f ? 1.0f / dist : 0.0f;
      o.x += dx * invDist * kOpponentSpeed * dtS;
      o.z += dz * invDist * kOpponentSpeed * dtS;
      resolveArtifactCollision(o.x, o.z, kOpponentCollisionRadius, artifacts, artifactCount);
      o.walkPhase += kOpponentWalkPhaseSpeed * dtS;
    } else {
      o.walkPhase += kOpponentIdlePhaseSpeed * dtS;
      playerHealth -= kOpponentContactDamagePerSec * dtS;
      if (playerHealth < 0.0f) playerHealth = 0.0f;
    }
  }
}

// sz[8] carries each vertex's camera-space depth alongside its screen
// position — callers that only draw edges/fills ignore it, but drawScene()
// reuses it (via minVisibleDepth) to occlusion-test particles/projectiles
// against this artifact without redoing the toCameraSpace() trig work.
bool projectArtifactVerts(const Camera& cam, const Artifact& a, float sx[8],
                          float sy[8], float sz[8], bool visible[8]) {
  bool any = false;
  for (int i = 0; i < 8; i++) {
    Vec3 c = cam.toCameraSpace(a.verts[i]);
    sz[i] = c.z;
    visible[i] = project(c, sx[i], sy[i]);
    any = any || visible[i];
  }
  return any;
}

// Nearest visible-vertex depth — used as "how close is this box's front
// surface to the camera," for testing whether it's close enough to occlude
// a point at some other depth. 1e9f (never occludes) if nothing projects.
float minVisibleDepth(const float sz[8], const bool visible[8]) {
  float m = 1e9f;
  for (int i = 0; i < 8; i++) {
    if (visible[i] && sz[i] < m) m = sz[i];
  }
  return m;
}

// Backface cull: a box's own far edges (the ones on the side facing away
// from the camera) were still being drawn, so a single object's wireframe
// showed through itself — the inter-object silhouette fill below only ever
// masks *other*, farther artifacts, never an object's own hidden edges.
// Faces are axis-aligned and don't rotate with the camera, so this is a
// plain dot product per face, no trig needed. An edge draws if at least
// one of its two adjacent faces (kEdgeFaces) faces the camera; an edge
// with both adjacent faces turned away is genuinely inside the silhouette
// and would only ever draw through the box's own near faces.
void computeFaceVisibility(const Camera& cam, const Artifact& a,
                           bool faceVisible[6]) {
  const float half = a.half, height = a.height, cx = a.cx, cz = a.cz;
  const float ex = cam.x, ey = 1.0f, ez = cam.z;  // toCameraSpace's eye height
  const float nx[6] = {0, 0, 0, 0, -1, 1};
  const float ny[6] = {-1, 1, 0, 0, 0, 0};
  const float nz[6] = {0, 0, -1, 1, 0, 0};
  const float fcx[6] = {cx, cx, cx, cx, cx - half, cx + half};
  const float fcy[6] = {0.0f, height, height * 0.5f, height * 0.5f,
                        height * 0.5f, height * 0.5f};
  const float fcz[6] = {cz, cz, cz - half, cz + half, cz, cz};
  for (int i = 0; i < 6; i++) {
    float dx = ex - fcx[i], dy = ey - fcy[i], dz = ez - fcz[i];
    faceVisible[i] = (nx[i] * dx + ny[i] * dy + nz[i] * dz) > 0.0f;
  }
}

void drawArtifactEdges(oled& d, const float sx[8], const float sy[8],
                       const bool visible[8], const bool faceVisible[6]) {
  for (int e = 0; e < 12; e++) {
    int ea = kBoxEdges[e][0], eb = kBoxEdges[e][1];
    if (!visible[ea] || !visible[eb]) continue;
    if (!faceVisible[kEdgeFaces[e][0]] && !faceVisible[kEdgeFaces[e][1]]) {
      continue;
    }
    d.drawLine(static_cast<int>(sx[ea]), static_cast<int>(sy[ea]),
               static_cast<int>(sx[eb]), static_cast<int>(sy[eb]));
  }
}

// ── Hidden-surface removal: painter's algorithm with a silhouette fill,
// the same trick classic vector arcade games (Battlezone included) used to
// mask farther wireframes with nearer ones on a display that can't
// z-buffer per pixel. drawScene() sorts artifacts far-to-near each frame
// and, for each one (nearest last), fills its projected silhouette in the
// background color before drawing its edges — that erases whatever a
// farther box left behind under it. No depth buffer, no touching the
// line-clipping code; just reordering what already draws. ─────────────────

// ── Fired shots: fixed (non-aimable) lob arc — see VectortankScreen.h for
// why arc-angle isn't a player control on this hardware. Declared ahead of
// drawScene() below since its occlusion pass draws projectiles directly
// and needs these constants/the Projectile layout. ─────────────────────────

constexpr int kMaxProjectiles = 8;
constexpr float kShotSpeed = 14.0f;   // forward (xz-plane) launch speed
constexpr float kShotLift = 3.2f;     // fixed initial vertical speed
constexpr float kGravity = 6.0f;
constexpr float kShotLifetimeS = 4.0f;
constexpr float kFireCooldownMs = 280.0f;
constexpr float kShotSpawnHeight = 1.0f;

// Visual size shrinks with altitude so the shot reads as a small falling
// object that shrinks to a single pixel right as it reaches the ground —
// kShotApexHeight is the analytic peak of the fixed lob arc (y0 + vy0^2/2g).
constexpr float kShotApexHeight =
    kShotSpawnHeight + (kShotLift * kShotLift) / (2.0f * kGravity);
constexpr int kShotMaxHalfPx = 2;

struct Projectile {
  float x, y, z;
  float vx, vy, vz;
  float age;
  bool alive;
};

struct Vec2 {
  float x, y;
};

float cross2(const Vec2& o, const Vec2& a, const Vec2& b) {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Andrew's monotone chain. n is at most 8 (box vertices), so an insertion
// sort for the lexicographic pre-sort is plenty fast and avoids pulling in
// <algorithm> for one call site.
int convexHull(Vec2 pts[], int n, Vec2 hull[8]) {
  if (n < 3) {
    for (int i = 0; i < n; i++) hull[i] = pts[i];
    return n;
  }
  for (int i = 1; i < n; i++) {
    Vec2 key = pts[i];
    int j = i - 1;
    while (j >= 0 && (pts[j].x > key.x ||
                       (pts[j].x == key.x && pts[j].y > key.y))) {
      pts[j + 1] = pts[j];
      j--;
    }
    pts[j + 1] = key;
  }
  Vec2 h[17];
  int k = 0;
  for (int i = 0; i < n; i++) {
    while (k >= 2 && cross2(h[k - 2], h[k - 1], pts[i]) <= 0.0f) k--;
    h[k++] = pts[i];
  }
  int lower = k + 1;
  for (int i = n - 2; i >= 0; i--) {
    while (k >= lower && cross2(h[k - 2], h[k - 1], pts[i]) <= 0.0f) k--;
    h[k++] = pts[i];
  }
  k--;
  int outN = k > 8 ? 8 : k;
  for (int i = 0; i < outN; i++) hull[i] = h[i];
  return outN;
}

// Sutherland-Hodgman clip against the screen rect. This is what keeps
// fillTriangle() coordinates safely inside [0, kScreenW-1] x [0,
// kScreenH-1] — u8g2's filled-triangle path takes u8g2_uint_t (uint16_t)
// with no internal clamping, so an off-screen negative vertex would wrap
// the same way the old drawLine() bug did (see components/vectortank.md).
// Clipping the polygon first, rather than clamping each vertex, also keeps
// the fill shape geometrically correct at the screen edge instead of
// distorting it.
int clipPolygonToScreen(const Vec2* poly, int n, Vec2* out) {
  Vec2 bufA[16], bufB[16];
  int nA = n > 16 ? 16 : n;
  for (int i = 0; i < nA; i++) bufA[i] = poly[i];

  auto clipEdge = [](const Vec2* in, int inN, Vec2* outp, int axis,
                      float bound, bool keepLessEqual) -> int {
    int outN = 0;
    for (int i = 0; i < inN; i++) {
      const Vec2& cur = in[i];
      const Vec2& prev = in[(i - 1 + inN) % inN];
      float curVal = axis == 0 ? cur.x : cur.y;
      float prevVal = axis == 0 ? prev.x : prev.y;
      bool curIn = keepLessEqual ? curVal <= bound : curVal >= bound;
      bool prevIn = keepLessEqual ? prevVal <= bound : prevVal >= bound;
      if (curIn != prevIn) {
        float t = (bound - prevVal) / (curVal - prevVal);
        outp[outN++] = {prev.x + t * (cur.x - prev.x),
                        prev.y + t * (cur.y - prev.y)};
      }
      if (curIn) outp[outN++] = cur;
      if (outN >= 16) break;
    }
    return outN;
  };

  int nB = clipEdge(bufA, nA, bufB, 0, 0.0f, false);            // x >= 0
  nA = clipEdge(bufB, nB, bufA, 0, kScreenW - 1.0f, true);      // x <= W-1
  nB = clipEdge(bufA, nA, bufB, 1, 0.0f, false);                // y >= 0
  nA = clipEdge(bufB, nB, bufA, 1, kScreenH - 1.0f, true);      // y <= H-1
  for (int i = 0; i < nA; i++) out[i] = bufA[i];
  return nA;
}

// Point-in-convex-polygon via consistent cross-product sign — used to test
// whether a particle/projectile's screen position falls inside a nearer
// artifact's silhouette (see the occlusion loop in drawScene()). Works for
// either hull winding direction since it just requires all signs to agree.
bool pointInConvexHull(const Vec2 hull[], int n, float px, float py) {
  int sign = 0;
  for (int i = 0; i < n; i++) {
    const Vec2& a = hull[i];
    const Vec2& b = hull[(i + 1) % n];
    float cr = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
    if (cr > 0.0f) {
      if (sign < 0) return false;
      sign = 1;
    } else if (cr < 0.0f) {
      if (sign > 0) return false;
      sign = -1;
    }
  }
  return true;
}

void fillArtifactSilhouette(oled& d, const float sx[8], const float sy[8],
                            const bool visible[8]) {
  Vec2 pts[8];
  int n = 0;
  for (int i = 0; i < 8; i++) {
    if (visible[i]) pts[n++] = {sx[i], sy[i]};
  }
  if (n < 3) return;
  Vec2 hull[8];
  int hn = convexHull(pts, n, hull);
  if (hn < 3) return;
  Vec2 clipped[16];
  int cn = clipPolygonToScreen(hull, hn, clipped);
  if (cn < 3) return;
  d.setDrawColor(0);
  for (int i = 1; i < cn - 1; i++) {
    d.fillTriangle(static_cast<int>(clipped[0].x), static_cast<int>(clipped[0].y),
                   static_cast<int>(clipped[i].x), static_cast<int>(clipped[i].y),
                   static_cast<int>(clipped[i + 1].x),
                   static_cast<int>(clipped[i + 1].y));
  }
  d.setDrawColor(1);
}

// Cached per-artifact render state for one frame: screen-space vertices,
// visibility, silhouette hull, and nearest depth. Built once per artifact
// and reused for (a) the painter's-algorithm fill+edge draw and (b)
// occlusion-testing particles/projectiles below — without this cache,
// testing 64 particles against 12 artifacts would redo each artifact's
// 8-vertex toCameraSpace()/convexHull() work up to 64 times.
struct ArtifactRenderState {
  bool hasHull;
  float sx[8], sy[8];
  bool visible[8];
  Vec2 hull[8];
  int hullCount;
  float minDepth;
};

// True if some artifact in `states` is both closer to the camera than
// `depth` and covers screen point (px, py) — i.e. a particle/projectile at
// that depth would be hidden behind it. Mirrors the silhouette-fill logic
// above but for single points instead of drawing.
bool occludedByArtifact(const ArtifactRenderState states[], const bool alive[],
                        int count, float px, float py, float depth) {
  for (int i = 0; i < count; i++) {
    if (!alive[i] || !states[i].hasHull) continue;
    if (states[i].minDepth >= depth) continue;
    if (pointInConvexHull(states[i].hull, states[i].hullCount, px, py)) {
      return true;
    }
  }
  return false;
}

// ── Opponent billboard rendering ────────────────────────────────────────────
//
// Local billboard-space proportions, in world units (feet at y=0). Applied
// as screen-space offsets scaled by kFovScale/depth — the same perspective
// scale project() itself uses for a world-space vertical delta — so the
// figure shrinks with distance exactly like everything else in the scene.
// Scaled to 75% of the original proportions (which stood about 1.8 world
// units tall) after on-device testing: full size read as too large next to
// the artifacts, but halving it (37.5%) went too far the other way.
constexpr float kOppHeadY = 1.275f;
constexpr float kOppShoulderY = 1.05f;
constexpr float kOppHipY = 0.675f;
constexpr float kOppStanceX = 0.135f;
constexpr float kOppShoulderX = 0.165f;
constexpr float kOppArmDrop = 0.375f;
constexpr float kOppLegSwingAmp = 0.21f;
constexpr float kOppArmSwingAmp = 0.165f;
constexpr float kOppBobAmp = 0.0375f;
constexpr float kOppHeadRadiusWorld = 0.15f;
constexpr int kOppHeadRadiusMinPx = 1;
constexpr int kOppHeadRadiusMaxPx = 5;

void drawOpponentBillboard(oled& d, const Camera& cam, const Opponent& o,
                           const ArtifactRenderState states[], const bool artifactAlive[],
                           int artifactCount) {
  Vec3 c = cam.toCameraSpace({o.x, 0.0f, o.z});
  float sx, sy;
  if (!project(c, sx, sy)) return;
  if (occludedByArtifact(states, artifactAlive, artifactCount, sx, sy, c.z)) return;

  const float scale = kFovScale / c.z;  // screen px per world unit at this depth

  // deathK crumples the figure toward the ground: 1 = standing, 0 = fully
  // collapsed. Feet stay put; head/shoulder/hip heights shrink toward them.
  const float deathK = (o.deathT < 0.0f)
      ? 1.0f
      : 1.0f - (o.deathT / kOpponentDeathDurationS);

  const float legSwing = sinf(o.walkPhase) * kOppLegSwingAmp;
  const float armSwing = sinf(o.walkPhase + kPi) * kOppArmSwingAmp;
  const float bob = fabsf(sinf(o.walkPhase)) * kOppBobAmp * deathK;

  auto toScreen = [&](float lx, float ly) -> Vec2 {
    return {sx + lx * scale, sy - ly * deathK * scale};
  };

  Vec2 head = toScreen(0.0f, kOppHeadY + bob);
  Vec2 shoulder = toScreen(0.0f, kOppShoulderY + bob);
  Vec2 hip = toScreen(0.0f, kOppHipY + bob);
  Vec2 handL = toScreen(-kOppShoulderX - armSwing, kOppShoulderY - kOppArmDrop + bob);
  Vec2 handR = toScreen(kOppShoulderX + armSwing, kOppShoulderY - kOppArmDrop + bob);
  Vec2 footL = toScreen(-kOppStanceX - legSwing, 0.0f);
  Vec2 footR = toScreen(kOppStanceX + legSwing, 0.0f);

  auto line = [&](const Vec2& a, const Vec2& b) {
    d.drawLine(static_cast<int>(a.x), static_cast<int>(a.y),
              static_cast<int>(b.x), static_cast<int>(b.y));
  };
  line(shoulder, head);
  line(shoulder, hip);
  line(shoulder, handL);
  line(shoulder, handR);
  line(hip, footL);
  line(hip, footR);

  // Head is a small filled rounded box rather than the neck line's bare
  // endpoint — a square with corner radius == half its side reads as a
  // circle at this resolution (oled has no dedicated circle primitive).
  // Sized in screen pixels from the same distance scale as everything
  // else, clamped so it never vanishes to 0px up close or balloons at
  // point-blank range.
  int headRadiusPx = static_cast<int>(lroundf(scale * kOppHeadRadiusWorld * deathK));
  if (headRadiusPx < kOppHeadRadiusMinPx) headRadiusPx = kOppHeadRadiusMinPx;
  if (headRadiusPx > kOppHeadRadiusMaxPx) headRadiusPx = kOppHeadRadiusMaxPx;
  int headDiameter = headRadiusPx * 2 + 1;
  d.drawRBox(static_cast<int>(head.x) - headRadiusPx,
            static_cast<int>(head.y) - headRadiusPx, headDiameter, headDiameter,
            headRadiusPx);

  // Hit-flash: a couple of extra pixels beside the head, cheap on a 1-bit
  // display where inverting a region would need an explicit clear-then-
  // redraw pass.
  if (o.hitFlashS > 0.0f) {
    d.drawPixel(static_cast<int>(head.x) - headRadiusPx - 2,
               static_cast<int>(head.y) - headRadiusPx);
    d.drawPixel(static_cast<int>(head.x) + headRadiusPx + 2,
               static_cast<int>(head.y) - headRadiusPx);
  }
}

// ── Background: horizon + distant mountain range. Both are drawn before
// the artifact silhouette-fill/edge loop below, so a nearer box's fill
// (which clears in the background color) correctly erases the background
// behind it — same painter's-algorithm ordering as the rest of drawScene().
// The mountains are a fixed function of world-space bearing (not screen
// column), so turning the camera scrolls the skyline instead of dragging a
// static image with it, the way a real distant range would parallax.

// TEMPORARY diagnostic offset (-1px) to test whether the perceived
// intensity artifact at horizon/edge intersections is pinned to the
// physical SSD1306 page boundary (row 32) or follows this constant —
// see components/vectortank.md. Revert to kScreenH / 2 afterward.
constexpr int kHorizonY = kScreenH / 2 - 1;  // eye height is 1.0, no pitch, so
                                         // the horizon sits at screen-center
                                         // for every yaw — see toCameraSpace.

void drawHorizon(oled& d) {
  d.drawLine(0, kHorizonY, kScreenW - 1, kHorizonY);
}

constexpr float kMountainBasePx = 6.0f;

// Sum of a few incommensurate sine frequencies over bearing angle gives an
// irregular but seamless (2*pi-periodic) skyline — no per-badge RNG needed,
// and it matches up perfectly when the camera spins a full turn.
float mountainHeightPx(float angleRad) {
  float h = kMountainBasePx + 4.0f * sinf(angleRad * 2.3f) +
            2.5f * sinf(angleRad * 5.1f + 1.7f) +
            1.2f * sinf(angleRad * 11.0f + 0.4f);
  return h < 1.0f ? 1.0f : h;
}

void drawMountains(oled& d, const Camera& cam) {
  int prevX = 0, prevY = kHorizonY;
  for (int x = 0; x < kScreenW; x++) {
    float dx = static_cast<float>(x) - kScreenW / 2.0f;
    float angle = cam.yaw + atan2f(dx, kFovScale);
    int y = kHorizonY - static_cast<int>(mountainHeightPx(angle) + 0.5f);
    if (x > 0) d.drawLine(prevX, prevY, x, y);
    prevX = x;
    prevY = y;
  }
}

// ── LED-matrix radar ─────────────────────────────────────────────────────
//
// The 8x8 ambient LED matrix normally free-runs its own ambient animation
// (or a MicroPython matrix app) via LEDAppRuntime, independent of whatever
// screen is in front — see components/led-app-runtime.md. That's a wasted
// second surface while a game is running, so VectortankScreen claims it
// with ledAppRuntime.beginOverride()/endOverride() (same refcounted
// contract SettingsScreen's LED-brightness live-preview uses) and drives it
// as a top-down radar: artifact bearings/ranges relative to the camera,
// rotated so forward is always "up" on the matrix, exactly like the
// mountain skyline is bearing-locked rather than screen-locked above.

constexpr float kRadarRangeWorld = 32.0f;  // world units from center to edge
constexpr uint32_t kRadarIntervalMs = 100;  // matches LEDAppRuntime::kDefaultDelay
constexpr uint8_t kRadarBlipBrightness = 160;

// A close explosion temporarily takes the matrix over from the radar with a
// fast full-panel strobe, then hands it back — the same override/restore
// shape LEDAppRuntime itself uses (beginOverride/endOverride), just nested
// one level deeper: radar is VectortankScreen's own "ambient" state, and the
// strobe is its temporary override of that. gStrobeStartMs is module-local
// like gCam/gArtifacts (one game in play at a time); left at 0 it reads as
// "not currently strobing" since millis() moves far past kStrobeDurationMs
// within the first frame of play.
constexpr uint32_t kStrobeDurationMs = 250;
constexpr uint32_t kStrobeIntervalMs = 40;  // on/off flip cadence while active
constexpr float kStrobeTriggerRangeWorld = 6.0f;  // explosions closer than
                                                   // this trigger the strobe
uint32_t gStrobeStartMs = 0;

void maybeStrobeFromExplosion(const Camera& cam, float ex, float ez) {
  float dx = ex - cam.x;
  float dz = ez - cam.z;
  if (dx * dx + dz * dz <= kStrobeTriggerRangeWorld * kStrobeTriggerRangeWorld) {
    gStrobeStartMs = millis();
  }
}

bool strobeActive(uint32_t now) { return now - gStrobeStartMs < kStrobeDurationMs; }

// Every explosion (not just close ones — that's the strobe above) marks its
// world location so the radar can flash that exact cell for a second,
// independent of whatever else the radar is showing. Unlike the strobe,
// this never touches any other pixel: it's drawn with individual
// setPixel() calls after drawMask() below, specifically so a flashing
// explosion cell can't mask a nearby artifact blip the way redrawing the
// whole mask would.
constexpr int kMaxExplosionMarkers = 8;
constexpr uint32_t kExplosionMarkerDurationMs = 1000;
constexpr uint32_t kExplosionMarkerFlickerMs = 120;  // on/off flip cadence
constexpr uint8_t kExplosionMarkerBrightness = 255;

struct ExplosionMarker {
  float x = 0.0f, z = 0.0f;
  uint32_t startMs = 0;
  bool active = false;
};
ExplosionMarker gExplosionMarkers[kMaxExplosionMarkers];

void markExplosionForRadar(float x, float z) {
  for (ExplosionMarker& m : gExplosionMarkers) {
    if (!m.active) {
      m = {x, z, millis(), true};
      return;
    }
  }
  // Pool full (kMaxExplosionMarkers=8 vs. a 1s lifetime) — reuse the first
  // slot rather than drop the new marker silently.
  gExplosionMarkers[0] = {x, z, millis(), true};
}

void drawStrobe(uint32_t now) {
  uint32_t phase = (now - gStrobeStartMs) / kStrobeIntervalMs;
  badgeMatrix.fill((phase % 2 == 0) ? 255 : 0);
}

// Shared world-to-8x8-cell projection for both artifact blips and explosion
// markers below — same rotation Camera::toCameraSpace() uses, so a point
// directly ahead of the camera lands toward row 0 (matrix "up") regardless
// of world yaw. Returns false if the point falls outside the matrix.
bool worldToRadarCell(const Camera& cam, float cosY, float sinY, float wx,
                     float wz, int& gx, int& gy) {
  float dx = wx - cam.x;
  float dz = wz - cam.z;
  float rx = dx * cosY - dz * sinY;
  float rz = dx * sinY + dz * cosY;
  gx = static_cast<int>(lroundf(3.5f + (rx / kRadarRangeWorld) * 3.5f));
  gy = static_cast<int>(lroundf(3.5f - (rz / kRadarRangeWorld) * 3.5f));
  return gx >= 0 && gx <= 7 && gy >= 0 && gy <= 7;
}

constexpr uint8_t kRadarOpponentBrightness = 220;  // brighter than a static
                                                    // artifact blip, dimmer
                                                    // than an explosion flash

void drawRadar(const Camera& cam, const Artifact artifacts[], int artifactCount,
               const Opponent opponents[], int opponentCount) {
  // Artifacts (durable obstacles) are deliberately NOT drawn on the radar —
  // now that they're permanent level geometry rather than something to
  // clear out, showing every one of them would clutter the 8x8 grid with
  // static clutter the player can't act on. `artifacts`/`artifactCount`
  // stay in the signature for symmetry with drawScene()'s call shape and
  // in case a future feature (e.g. only revealing a nearby obstacle) wants
  // them again.
  (void)artifacts;
  (void)artifactCount;

  uint32_t now = millis();
  if (strobeActive(now)) {
    drawStrobe(now);
    return;
  }

  uint8_t mask[LEDAppRuntime::kFrameRows] = {};
  float cosY = cosf(cam.yaw);
  float sinY = sinf(cam.yaw);
  // clear() + drawMask() + the marker setPixel() calls below are all
  // separate loops of individual I2C writes; without batching, the panel
  // would visibly blank-then-repaint every ~100ms (see
  // components/vectortank.md's LED-matrix flicker note). beginFrameBatch()/
  // endFrameBatch() route all of it to the chip's hidden frame and swap
  // once, so the viewer only ever sees the finished picture.
  badgeMatrix.beginFrameBatch();
  badgeMatrix.clear(0);
  badgeMatrix.drawMask(mask, kRadarBlipBrightness, 0);

  // Opponent blips, like explosion markers below, are individual setPixel()
  // writes rather than folded into the mask above — they need a distinct
  // (brighter) brightness than a static artifact blip, which a single-
  // brightness drawMask() call can't express.
  for (int i = 0; i < opponentCount; i++) {
    if (!opponents[i].alive) continue;
    int gx, gy;
    if (!worldToRadarCell(cam, cosY, sinY, opponents[i].x, opponents[i].z, gx, gy)) {
      continue;
    }
    badgeMatrix.setPixel(gx, gy, kRadarOpponentBrightness);
  }

  // Explosion markers draw last, as individual pixel writes rather than a
  // second mask — see the comment above markExplosionForRadar(). This only
  // ever touches the marker's own cell, so it can't mask any other pixel
  // the way a second drawMask() would — but the marker's own cell has to
  // actively flash between bright and *off*, not "bright, then leave
  // whatever was already there": a marker landing on an artifact blip's
  // cell would otherwise never visibly go dark (the blip brightness was
  // still sitting there from drawMask() above), so it read as steady
  // instead of flashing. The blip itself isn't lost — drawMask() repaints
  // it fresh from gArtifacts every tick, so it reappears the instant the
  // marker's 1s window ends.
  for (ExplosionMarker& m : gExplosionMarkers) {
    if (!m.active) continue;
    uint32_t elapsed = now - m.startMs;
    if (elapsed >= kExplosionMarkerDurationMs) {
      m.active = false;
      continue;
    }
    int gx, gy;
    if (!worldToRadarCell(cam, cosY, sinY, m.x, m.z, gx, gy)) continue;
    bool onPhase = (elapsed / kExplosionMarkerFlickerMs) % 2 == 0;
    badgeMatrix.setPixel(gx, gy, onPhase ? kExplosionMarkerBrightness : 0);
  }
  badgeMatrix.endFrameBatch();
}

// ── Game-data footer: FPS, a horizontal compass, and a stubbed health
// readout, in the same bottom band every other screen's footer uses
// (OLEDLayout::kFooterTopY..kScreenH) — see OLEDLayout.h's three-footer-
// style comment. None of the existing styles fit (they're all built around
// a button-glyph chip or a single text string), so this draws its own rule
// + three columns directly rather than forcing a mismatched helper.

// The compass is bearing-locked exactly like the mountain skyline and radar
// above: forward always sits at the fixed center pixel, and turning the
// camera scrolls the letters past it, rather than a fixed compass rose with
// a moving player marker. Unlike the mountain skyline's perspective
// projection, this is a plain linear angle-to-pixel mapping (a compass
// ribbon has no "depth" to project) — kCompassPxPerDeg is just a scroll
// speed, not a lens model.
constexpr int kCompassCenterX = kScreenW / 2;
constexpr float kCompassRangeDeg = 60.0f;
constexpr int kCompassHalfWidthPx = 24;
constexpr float kCompassPxPerDeg = kCompassHalfWidthPx / kCompassRangeDeg;

float wrapDegSigned(float deg) {
  while (deg > 180.0f) deg -= 360.0f;
  while (deg < -180.0f) deg += 360.0f;
  return deg;
}

void drawCompass(oled& d, const Camera& cam) {
  struct Cardinal {
    char label;
    float angleRad;
  };
  // Forward is (sin(yaw), cos(yaw)), so world angle 0 (yaw==0) is the
  // camera's starting facing — labeled "N" here as an arbitrary but fixed
  // reference frame, same as compass north is just "wherever 0 was".
  static constexpr Cardinal kCardinals[4] = {
      {'N', 0.0f}, {'E', kPi * 0.5f}, {'S', kPi}, {'W', kPi * 1.5f}};

  d.setFontPreset(FONT_TINY);
  for (const Cardinal& c : kCardinals) {
    float deltaDeg = wrapDegSigned((c.angleRad - cam.yaw) * (180.0f / kPi));
    if (deltaDeg < -kCompassRangeDeg || deltaDeg > kCompassRangeDeg) continue;
    int x = kCompassCenterX + static_cast<int>(lroundf(deltaDeg * kCompassPxPerDeg));
    char buf[2] = {c.label, '\0'};
    int w = d.getStrWidth(buf);
    d.drawStr(x - w / 2, OLEDLayout::kFooterTextBaseY, buf);
  }
  // Fixed center tick marks the player's own heading, directly below the
  // title-bar clock — everything else on the ribbon moves relative to it.
  d.drawVLine(kCompassCenterX, OLEDLayout::kFooterTopY + 1, 3);
}

// FPS is a trailing 1-second moving average rather than shown raw: render()
// is called once per GUIManager tick, so an unsmoothed 1/dt reading jitters
// wildly frame to frame on a display refresh loop that isn't perfectly
// metronomic. A ring buffer of recent frame timestamps makes "frames seen
// in the last 1000ms" trivial to compute, and that count *is* the average
// fps over that window.
constexpr uint32_t kFpsWindowMs = 1000;
constexpr int kFpsSampleCap = 240;  // generous headroom above realistic fps
uint32_t gFpsSampleTimes[kFpsSampleCap];
int gFpsSampleHead = 0;
int gFpsSampleCount = 0;

int recordFrameAndGetFps(uint32_t nowMs) {
  gFpsSampleTimes[gFpsSampleHead] = nowMs;
  gFpsSampleHead = (gFpsSampleHead + 1) % kFpsSampleCap;
  if (gFpsSampleCount < kFpsSampleCap) gFpsSampleCount++;

  int counted = 0;
  for (int i = 0; i < gFpsSampleCount; i++) {
    int pos = (gFpsSampleHead - 1 - i + kFpsSampleCap) % kFpsSampleCap;
    if (nowMs - gFpsSampleTimes[pos] > kFpsWindowMs) break;
    counted++;
  }
  return counted;
}

void drawVectortankFooter(oled& d, const Camera& cam, float dtS, float playerHealth) {
  (void)dtS;
  int fps = recordFrameAndGetFps(millis());

  // The 3D scene draws all the way to the bottom row (near boxes/ground
  // edges can reach this band), unlike the header's clear top-of-screen
  // sky — so unlike most OLEDLayout footers, this one has to blank its
  // band first rather than only drawing on top of empty screen.
  d.setDrawColor(0);
  d.drawBox(0, OLEDLayout::kFooterRuleY, kScreenW, kScreenH - OLEDLayout::kFooterRuleY);
  d.setDrawColor(1);
  d.drawHLine(0, OLEDLayout::kFooterTopY, kScreenW);
  d.setFontPreset(FONT_TINY);

  char fpsBuf[12];
  std::snprintf(fpsBuf, sizeof(fpsBuf), "%d fps", fps);
  d.drawStr(2, OLEDLayout::kFooterTextBaseY, fpsBuf);

  drawCompass(d, cam);

  char hpBuf[10];
  std::snprintf(hpBuf, sizeof(hpBuf), "HP %d",
               static_cast<int>(lroundf(playerHealth)));
  int hpW = d.getStrWidth(hpBuf);
  d.drawStr(kScreenW - 2 - hpW, OLEDLayout::kFooterTextBaseY, hpBuf);
}

// ── Pause menu overlay ───────────────────────────────────────────────────
//
// Back used to pop the screen directly (instant exit); it now opens this
// overlay instead so a stray back-press mid-game doesn't lose the run.
// The overlay draws on top of a frozen frame of the scene rather than its
// own screen, matching how a pause menu should read ("the game is still
// here, just paused"). Doubles as the game-over screen (gameOver == true)
// once player health hits 0 — same layout, just a different title and with
// "resume" (back) disabled, since there's nothing to resume to at 0 HP.

constexpr int kPauseMenuX = 20;
constexpr int kPauseMenuY = 10;
constexpr int kPauseMenuW = 88;
constexpr int kPauseMenuH = 44;
constexpr uint8_t kPauseItemResume = 0;
constexpr uint8_t kPauseItemNewGame = 1;
constexpr uint8_t kPauseItemExit = 2;
constexpr uint8_t kPauseCursorNewGame = kPauseItemNewGame;
constexpr uint8_t kPauseMenuItemCount = 3;  // Resume, New Game, Exit
constexpr int kPauseRowH = 8;

// Cycles the cursor to the next/previous item (dir = +1/-1), skipping
// Resume when there's nothing to resume to (gameOver).
uint8_t nextPauseItem(uint8_t current, bool gameOver, int8_t dir) {
  uint8_t next = current;
  do {
    next = static_cast<uint8_t>(
        (next + dir + kPauseMenuItemCount) % kPauseMenuItemCount);
  } while (gameOver && next == kPauseItemResume);
  return next;
}

void drawPauseMenu(oled& d, bool gameOver, uint8_t cursor, int killCount,
                   uint32_t highScore) {
  d.setDrawColor(0);
  d.drawBox(kPauseMenuX, kPauseMenuY, kPauseMenuW, kPauseMenuH);
  d.setDrawColor(1);
  d.drawRFrame(kPauseMenuX, kPauseMenuY, kPauseMenuW, kPauseMenuH, 0);
  d.setFontPreset(FONT_TINY);

  const char* title = gameOver ? "GAME OVER" : "PAUSED";
  d.drawStr(kPauseMenuX + 8, kPauseMenuY + 9, title);
  char scoreBuf[8];
  std::snprintf(scoreBuf, sizeof(scoreBuf), "%d", killCount);
  int scoreW = d.getStrWidth(scoreBuf);
  d.drawStr(kPauseMenuX + kPauseMenuW - 8 - scoreW, kPauseMenuY + 9, scoreBuf);

  d.drawStr(kPauseMenuX + 8, kPauseMenuY + 17, "HIGH SCORE");
  char highBuf[12];
  std::snprintf(highBuf, sizeof(highBuf), "%lu",
               static_cast<unsigned long>(highScore));
  int highW = d.getStrWidth(highBuf);
  d.drawStr(kPauseMenuX + kPauseMenuW - 8 - highW, kPauseMenuY + 17, highBuf);

  uint8_t items[kPauseMenuItemCount];
  uint8_t itemCount = 0;
  if (!gameOver) items[itemCount++] = kPauseItemResume;
  items[itemCount++] = kPauseItemNewGame;
  items[itemCount++] = kPauseItemExit;

  for (uint8_t i = 0; i < itemCount; i++) {
    const char* label = items[i] == kPauseItemResume   ? "Resume"
                        : items[i] == kPauseItemNewGame ? "New Game"
                                                        : "Exit";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%s%s", items[i] == cursor ? "> " : "  ",
                 label);
    d.drawStr(kPauseMenuX + 8, kPauseMenuY + 25 + i * kPauseRowH, buf);
  }
}

void drawScene(oled& d, const Camera& cam, const Artifact artifacts[],
               int artifactCount, const Particle particles[],
               int particlePoolSize, const Projectile projectiles[],
               int projectileCount, const Opponent opponents[],
               int opponentCount) {
  drawHorizon(d);
  drawMountains(d, cam);

  static ArtifactRenderState states[kMaxArtifacts];
  static bool alive[kMaxArtifacts];
  static int order[kMaxArtifacts];

  for (int i = 0; i < artifactCount; i++) {
    const Artifact& a = artifacts[i];
    alive[i] = a.alive;
    order[i] = i;
    if (!a.alive) {
      states[i].hasHull = false;
      continue;
    }
    float sz[8];
    ArtifactRenderState& st = states[i];
    if (!projectArtifactVerts(cam, a, st.sx, st.sy, sz, st.visible)) {
      st.hasHull = false;
      continue;
    }
    st.minDepth = minVisibleDepth(sz, st.visible);
    Vec2 pts[8];
    int n = 0;
    for (int v = 0; v < 8; v++) {
      if (st.visible[v]) pts[n++] = {st.sx[v], st.sy[v]};
    }
    st.hullCount = convexHull(pts, n, st.hull);
    st.hasHull = st.hullCount >= 3;
  }

  // Farthest-first draw order for the painter's algorithm: each nearer
  // artifact's silhouette fill erases whatever a farther one left behind.
  for (int i = 1; i < artifactCount; i++) {
    int keyIdx = order[i];
    float keyDepth = states[keyIdx].hasHull ? states[keyIdx].minDepth : -1e9f;
    int j = i - 1;
    while (j >= 0) {
      float cmpDepth = states[order[j]].hasHull ? states[order[j]].minDepth : -1e9f;
      if (cmpDepth >= keyDepth) break;
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = keyIdx;
  }

  for (int i = 0; i < artifactCount; i++) {
    int idx = order[i];
    if (!alive[idx] || !states[idx].hasHull) continue;
    const Artifact& a = artifacts[idx];
    const ArtifactRenderState& st = states[idx];
    bool faceVisible[6];
    computeFaceVisibility(cam, a, faceVisible);
    fillArtifactSilhouette(d, st.sx, st.sy, st.visible);
    drawArtifactEdges(d, st.sx, st.sy, st.visible, faceVisible);
  }

  // Opponents draw after every artifact's silhouette fill (same reasoning
  // as particles/projectiles below: they're occlusion-tested against the
  // artifacts, not depth-sorted against them, so they need the artifacts'
  // fills already settled first).
  for (int i = 0; i < opponentCount; i++) {
    if (!opponents[i].alive) continue;
    drawOpponentBillboard(d, cam, opponents[i], states, alive, artifactCount);
  }

  // Particles/projectiles draw last (they're small, foreground-ish effects)
  // but still need occlusion-testing against artifacts — otherwise a shot
  // or explosion behind a box kept showing through it.
  for (int i = 0; i < particlePoolSize; i++) {
    const Particle& p = particles[i];
    if (!p.active) continue;
    Vec3 c = cam.toCameraSpace({p.x, p.y, p.z});
    float sx, sy;
    if (!project(c, sx, sy)) continue;
    if (occludedByArtifact(states, alive, artifactCount, sx, sy, c.z)) continue;
    // A streak from the previous frame's position to this one reads as a
    // much more energetic burst than a cloud of static single pixels at
    // this particle count/speed — same projection, just drawn as a short
    // drawLine() instead of drawPixel(). Falls back to a single-pixel dot
    // on the particle's very first frame (px/py/pz == spawn position, so
    // the "streak" has zero length) or if the previous position doesn't
    // project (e.g. just crossed behind the near clip plane).
    Vec3 pc = cam.toCameraSpace({p.px, p.py, p.pz});
    float psx, psy;
    if (project(pc, psx, psy)) {
      d.drawLine(static_cast<int>(psx), static_cast<int>(psy),
                static_cast<int>(sx), static_cast<int>(sy));
    } else {
      d.drawPixel(static_cast<int>(sx), static_cast<int>(sy));
    }
  }

  for (int i = 0; i < projectileCount; i++) {
    const Projectile& p = projectiles[i];
    if (!p.alive) continue;
    Vec3 c = cam.toCameraSpace({p.x, p.y, p.z});
    float sx, sy;
    if (!project(c, sx, sy)) continue;
    if (occludedByArtifact(states, alive, artifactCount, sx, sy, c.z)) continue;

    int ix = static_cast<int>(sx);
    int iy = static_cast<int>(sy);
    int half = static_cast<int>((p.y / kShotApexHeight) * kShotMaxHalfPx + 0.5f);
    if (half < 0) half = 0;
    if (half > kShotMaxHalfPx) half = kShotMaxHalfPx;
    if (half <= 0) {
      d.drawPixel(ix, iy);
      continue;
    }
    // The outline alone doesn't occlude what's behind it — its own
    // interior was never cleared, so an artifact edge passing between the
    // 4 outline strokes stayed visible. Clear the footprint first (two
    // safe int16_t-typed fillTriangle() calls, same reasoning as
    // fillArtifactSilhouette()'s use of fillTriangle over drawBox — see
    // components/vectortank.md) so the projectile actually masks whatever
    // it's in front of, then draw the outline on top.
    d.setDrawColor(0);
    d.fillTriangle(ix - half, iy - half, ix + half, iy - half, ix + half,
                   iy + half);
    d.fillTriangle(ix - half, iy - half, ix + half, iy + half, ix - half,
                   iy + half);
    d.setDrawColor(1);
    d.drawLine(ix - half, iy - half, ix + half, iy - half);
    d.drawLine(ix - half, iy + half, ix + half, iy + half);
    d.drawLine(ix - half, iy - half, ix - half, iy + half);
    d.drawLine(ix + half, iy - half, ix + half, iy + half);
  }
}

// ── Fired shots: fixed (non-aimable) lob arc — see VectortankScreen.h for
// why arc-angle isn't a player control on this hardware. ───────────────────

void fireProjectile(Projectile out[], int count, const Camera& cam) {
  for (int i = 0; i < count; i++) {
    if (out[i].alive) continue;
    float fx = sinf(cam.yaw);
    float fz = cosf(cam.yaw);
    out[i].x = cam.x;
    out[i].y = kShotSpawnHeight;
    out[i].z = cam.z;
    out[i].vx = fx * kShotSpeed;
    out[i].vy = kShotLift;
    out[i].vz = fz * kShotSpeed;
    out[i].age = 0.0f;
    out[i].alive = true;
    return;
  }
}

// ── Splash damage ────────────────────────────────────────────────────────
//
// Every explosion (ground miss, artifact hit, or opponent hit) now also
// damages anything within kSplashRadius of it, falling off linearly to 0 at
// the edge — the player included, so standing too close to your own shot
// costs health. This is on top of a projectile's own direct hit-test above
// (kOpponentShotDamage), not a replacement for it: a directly-hit opponent
// is almost always inside its own explosion's splash radius too, so it
// takes the fixed direct-hit damage plus a near-full-strength splash tick,
// which reads as "the direct hit mattered" rather than making direct
// precision pointless.
constexpr float kSplashRadius = 3.0f;
constexpr float kSplashMaxDamage = 20.0f;  // at the explosion's exact center

void applySplashDamage(float ex, float ez, const Camera& cam, float& playerHealth,
                       Opponent opponents[], int opponentCount) {
  {
    float dx = cam.x - ex;
    float dz = cam.z - ez;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < kSplashRadius) {
      playerHealth -= kSplashMaxDamage * (1.0f - dist / kSplashRadius);
      if (playerHealth < 0.0f) playerHealth = 0.0f;
    }
  }
  for (int i = 0; i < opponentCount; i++) {
    Opponent& o = opponents[i];
    if (!o.alive || o.deathT >= 0.0f) continue;
    float dx = o.x - ex;
    float dz = o.z - ez;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist >= kSplashRadius) continue;
    o.health -= kSplashMaxDamage * (1.0f - dist / kSplashRadius);
    o.hitFlashS = kOpponentHitFlashS;
    if (o.health <= 0.0f) o.deathT = 0.0f;
  }
}

void stepProjectiles(Projectile projectiles[], int count, float dtS,
                     Artifact artifacts[], int artifactCount,
                     Particle particles[], int particlePoolSize,
                     const Camera& cam, Opponent opponents[],
                     int opponentCount, float& playerHealth) {
  for (int i = 0; i < count; i++) {
    Projectile& p = projectiles[i];
    if (!p.alive) continue;
    p.vy -= kGravity * dtS;
    p.x += p.vx * dtS;
    p.y += p.vy * dtS;
    p.z += p.vz * dtS;
    p.age += dtS;
    if (p.age >= kShotLifetimeS || p.y < 0.0f) {
      p.alive = false;
      spawnExplosion(particles, particlePoolSize, p.x, 0.0f, p.z);
      maybeStrobeFromExplosion(cam, p.x, p.z);
      markExplosionForRadar(p.x, p.z);
      applySplashDamage(p.x, p.z, cam, playerHealth, opponents, opponentCount);
      continue;
    }
    for (int j = 0; j < artifactCount; j++) {
      // Artifacts are durable obstacles: a shot still explodes right at the
      // box's surface (the hit-test itself is unchanged), but no longer
      // destroys it — `a.alive` stays true, so it keeps blocking movement
      // (resolveArtifactCollision) and drawing exactly as before.
      Artifact& a = artifacts[j];
      if (!a.alive) continue;
      if (p.y > a.height) continue;
      float dx = p.x - a.cx;
      float dz = p.z - a.cz;
      if (dx > -a.half && dx < a.half && dz > -a.half && dz < a.half) {
        p.alive = false;
        spawnExplosion(particles, particlePoolSize, p.x, p.y, p.z);
        maybeStrobeFromExplosion(cam, p.x, p.z);
        markExplosionForRadar(p.x, p.z);
        applySplashDamage(p.x, p.z, cam, playerHealth, opponents, opponentCount);
        break;
      }
    }
    if (!p.alive) continue;

    for (int j = 0; j < opponentCount; j++) {
      Opponent& o = opponents[j];
      if (!o.alive || o.deathT >= 0.0f) continue;
      if (p.y > kOpponentHitHeight) continue;
      float dx = p.x - o.x;
      float dz = p.z - o.z;
      if (dx > -kOpponentHitHalfWidth && dx < kOpponentHitHalfWidth &&
          dz > -kOpponentHitHalfWidth && dz < kOpponentHitHalfWidth) {
        o.health -= kOpponentShotDamage;
        o.hitFlashS = kOpponentHitFlashS;
        if (o.health <= 0.0f) o.deathT = 0.0f;
        p.alive = false;
        spawnExplosion(particles, particlePoolSize, p.x, p.y, p.z);
        maybeStrobeFromExplosion(cam, p.x, p.z);
        markExplosionForRadar(p.x, p.z);
        applySplashDamage(p.x, p.z, cam, playerHealth, opponents, opponentCount);
        break;
      }
    }
  }
}

// ── Interactive play state (module-local; one VectortankScreen instance
// exists per GUIManager registration, matching the rest of this codebase's
// static-screen-instance convention). ───────────────────────────────────────

Camera gCam;
Artifact gArtifacts[kMaxArtifacts];
int gArtifactCount = 0;
Particle gParticles[kMaxParticles];
Projectile gProjectiles[kMaxProjectiles];
Opponent gOpponents[kMaxOpponents];
int gOpponentCount = 0;
float gPlayerHealth = 100.0f;
float gSpawnTimerS = 0.0f;

constexpr int kPlayArtifactCount = 12;
constexpr float kPlayerMaxHealth = 100.0f;
constexpr float kPlayerCollisionRadius = 0.4f;  // blocked by artifact boxes

// ── Escalating opponent spawner ─────────────────────────────────────────
//
// kActiveOpponentCount's initial wave is deliberately small (an easier
// start); stepSpawner() then tops the pool up over time, with the interval
// between spawns shrinking as gKillCount rises — the run gets harder the
// better the player is doing, floored at kSpawnMinIntervalS so it never
// becomes an unplayable firehose.
constexpr float kSpawnBaseIntervalS = 8.0f;
constexpr float kSpawnMinIntervalS = 2.0f;
constexpr float kSpawnIntervalStepPerKillS = 0.25f;

// Reuses makeOpponents()'s ring-spawn/init logic for a single slot rather
// than duplicating it — spawns into the first dead slot within the active
// range, or grows gOpponentCount into unused pool capacity if every active
// slot is currently alive. No-ops once the pool (kMaxOpponents) is full.
void spawnOneOpponent() {
  int slot = -1;
  for (int i = 0; i < gOpponentCount; i++) {
    if (!gOpponents[i].alive) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    if (gOpponentCount >= kMaxOpponents) return;
    slot = gOpponentCount++;
  }
  Opponent single[1];
  makeOpponents(1, single, gCam);
  gOpponents[slot] = single[0];
}

void stepSpawner(float dtS) {
  gSpawnTimerS -= dtS;
  if (gSpawnTimerS > 0.0f) return;
  float interval = kSpawnBaseIntervalS -
                   kSpawnIntervalStepPerKillS * static_cast<float>(gKillCount);
  if (interval < kSpawnMinIntervalS) interval = kSpawnMinIntervalS;
  gSpawnTimerS = interval;
  spawnOneOpponent();
}

// Resets all module-level game-world state — shared by onEnter() and the
// pause menu's "New Game" action so a fresh run doesn't need its own
// separately-maintained copy of what onEnter() already does. Screen-owned
// timing/UI members (lastFireMs_, paused_, etc.) are reset at each call
// site instead, since resetGame() only ever touches the free-function
// globals above.
void resetGame() {
  seedRng(millis());
  gCam = Camera();
  gArtifactCount = makeArtifacts(kPlayArtifactCount, gArtifacts, gCam);
  for (int i = 0; i < kMaxParticles; i++) gParticles[i].active = false;
  for (int i = 0; i < kMaxProjectiles; i++) gProjectiles[i].alive = false;
  gOpponentCount = makeOpponents(kActiveOpponentCount, gOpponents, gCam);
  gPlayerHealth = kPlayerMaxHealth;
  gKillCount = 0;
  gSpawnTimerS = kSpawnBaseIntervalS;
}

}  // namespace

void VectortankScreen::onEnter(GUIManager& gui) {
  (void)gui;
  resetGame();
  gHighScore = loadHighScore();
  lastFrameMs_ = millis();
  lastFireMs_ = 0;
  lastRadarMs_ = 0;
  paused_ = false;
  gameOver_ = false;
  pauseCursor_ = kPauseItemResume;
  // Claim the ambient LED matrix for the duration of play — refcounted, so
  // this nests safely with any other beginOverride() caller (e.g. Settings'
  // LED-brightness preview), and endOverride() in onExit() hands the matrix
  // back to whatever ambient mode/Python app was running before.
  ledAppRuntime.beginOverride();
}

void VectortankScreen::onExit(GUIManager& gui) {
  (void)gui;
  ledAppRuntime.endOverride();
}

void VectortankScreen::render(oled& d, GUIManager& gui) {
  (void)gui;
  OLEDLayout::drawStatusHeader(d, "Grenada");

  uint32_t now = millis();
  float dtS = (now - lastFrameMs_) / 1000.0f;
  lastFrameMs_ = now;

  // The strobe needs a faster tick than the radar's own refresh rate to
  // actually flicker, so shrink the write interval while one is running.
  uint32_t ledInterval = strobeActive(now) ? kStrobeIntervalMs : kRadarIntervalMs;
  if (now - lastRadarMs_ >= ledInterval) {
    lastRadarMs_ = now;
    drawRadar(gCam, gArtifacts, gArtifactCount, gOpponents, gOpponentCount);
  }

  d.setDrawColor(1);
  drawScene(d, gCam, gArtifacts, gArtifactCount, gParticles, kMaxParticles,
            gProjectiles, kMaxProjectiles, gOpponents, gOpponentCount);

  drawVectortankFooter(d, gCam, dtS, gPlayerHealth);

  if (paused_) {
    drawPauseMenu(d, gameOver_, pauseCursor_, gKillCount, gHighScore);
  }
}

void VectortankScreen::handleInput(const Inputs& inp, int16_t cursorX,
                                    int16_t cursorY, GUIManager& gui) {
  (void)cursorX;
  (void)cursorY;

  if (paused_) {
    // The game itself is frozen while this overlay is up, so left/right no
    // longer need to dodge a strafe conflict: left (raw edge, "x") cycles
    // the cursor forward through the items, and the stick's Y axis does the
    // same (up = previous, down = next) since a paused menu has no other
    // use for it. Right/b (device "B" selection button, same binding the
    // rest of the badge uses for confirm) activates whichever is
    // highlighted — Resume unpauses, New Game restarts, Exit leaves. At 0
    // HP there's no Resume item (nothing to resume to), so navigation
    // skips it and only New Game/Exit can get the player out of this state.
    constexpr float kStickHi = 0.5f;
    constexpr float kStickLo = 0.2f;
    float yDir = (static_cast<float>(inp.joyY()) - 2047.0f) / 2047.0f;
    int8_t stickDir = 0;
    if (yDir < -kStickHi) {
      stickDir = -1;
    } else if (yDir > kStickHi) {
      stickDir = 1;
    }
    if (stickDir != 0 && pauseStickDir_ == 0) {
      pauseCursor_ = nextPauseItem(pauseCursor_, gameOver_, stickDir);
    }
    if (fabsf(yDir) < kStickLo) {
      pauseStickDir_ = 0;
    } else if (stickDir != 0) {
      pauseStickDir_ = stickDir;
    }

    if (inp.edges().leftPressed) {
      pauseCursor_ = nextPauseItem(pauseCursor_, gameOver_, 1);
    }
    if (inp.edges().rightPressed) {
      if (pauseCursor_ == kPauseItemResume) {
        paused_ = false;
      } else if (pauseCursor_ == kPauseItemNewGame) {
        resetGame();
        paused_ = false;
        gameOver_ = false;
      } else {
        gui.popScreen();
      }
    }
    return;
  }

  // Left/right (x/b) are claimed for strafe below, so back/fire live on
  // up/down's RAW edges rather than the confirm/cancel semantic layer —
  // confirm/cancel are aliased onto down+right as a pair (see Inputs.cpp),
  // and right is no longer free once it's doing strafe duty.
  if (inp.edges().downPressed) {
    // Back now opens this pause overlay instead of exiting immediately —
    // see the "Pause menu overlay" comment block above drawPauseMenu().
    paused_ = true;
    pauseCursor_ = kPauseItemResume;
    pauseStickDir_ = 0;
    return;
  }

  uint32_t now = millis();
  float dtS = (now - lastFrameMs_) / 1000.0f;
  float xDir = (static_cast<float>(inp.joyX()) - 2047.0f) / 2047.0f;
  float yDir = (static_cast<float>(inp.joyY()) - 2047.0f) / 2047.0f;
  gCam.move(xDir, yDir, dtS);
  if (inp.buttons().left) gCam.strafe(-1.0f, dtS);
  if (inp.buttons().right) gCam.strafe(1.0f, dtS);
  resolveArtifactCollision(gCam.x, gCam.z, kPlayerCollisionRadius, gArtifacts,
                           gArtifactCount);
  for (int i = 0; i < kMaxParticles; i++) {
    stepParticle(gParticles[i], dtS);
  }
  stepOpponents(gOpponents, gOpponentCount, dtS, gCam, gPlayerHealth, gArtifacts,
               gArtifactCount);
  stepProjectiles(gProjectiles, kMaxProjectiles, dtS, gArtifacts,
                  gArtifactCount, gParticles, kMaxParticles, gCam,
                  gOpponents, gOpponentCount, gPlayerHealth);
  stepSpawner(dtS);

  if (inp.edges().upPressed && now - lastFireMs_ >= kFireCooldownMs) {
    fireProjectile(gProjectiles, kMaxProjectiles, gCam);
    lastFireMs_ = now;
  }

  // Permanent (until New Game/Exit) pause on death — there's no "resume"
  // from 0 HP, so this reuses the pause overlay's own frozen-scene
  // rendering rather than a separate game-over screen.
  if (gPlayerHealth <= 0.0f && !gameOver_) {
    gameOver_ = true;
    paused_ = true;
    pauseCursor_ = kPauseCursorNewGame;
  }
}
