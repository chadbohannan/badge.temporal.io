#include "VectortankScreen.h"

#include <cmath>
#include <cstdio>

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
// kExplosionParticleCount below.
constexpr int kMaxParticles = 64;

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

constexpr int kExplosionParticleCount = 14;
constexpr float kExplosionSpeedMin = 1.8f;
constexpr float kExplosionSpeedMax = 3.5f;
constexpr float kExplosionGravity = 4.0f;
constexpr float kExplosionLifetimeMin = 0.25f;
constexpr float kExplosionLifetimeMax = 0.55f;

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
  p.vy -= kExplosionGravity * dtS;
  p.x += p.vx * dtS;
  p.y += p.vy * dtS;
  p.z += p.vz * dtS;
  p.age += dtS;
  if (p.age >= p.lifetime || p.y < 0.0f) p.active = false;
}

int makeArtifacts(int count, Artifact out[], const Camera& /*cam*/,
                   float spread = 40.0f, float minDist = 6.0f) {
  count = count > kMaxArtifacts ? kMaxArtifacts : count;
  for (int i = 0; i < count; i++) {
    float cx, cz;
    do {
      cx = randRange(-spread, spread);
      cz = randRange(minDist, spread);
    } while (cx * cx + cz * cz < minDist * minDist);
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

void drawRadar(const Camera& cam, const Artifact artifacts[], int artifactCount) {
  uint32_t now = millis();
  if (strobeActive(now)) {
    drawStrobe(now);
    return;
  }

  uint8_t mask[LEDAppRuntime::kFrameRows] = {};
  float cosY = cosf(cam.yaw);
  float sinY = sinf(cam.yaw);
  for (int i = 0; i < artifactCount; i++) {
    if (!artifacts[i].alive) continue;
    int gx, gy;
    if (!worldToRadarCell(cam, cosY, sinY, artifacts[i].cx, artifacts[i].cz,
                          gx, gy)) {
      continue;
    }
    mask[gy] |= static_cast<uint8_t>(0x80u >> gx);
  }
  // clear() + drawMask() + the marker setPixel() calls below are all
  // separate loops of individual I2C writes; without batching, the panel
  // would visibly blank-then-repaint every ~100ms (see
  // components/vectortank.md's LED-matrix flicker note). beginFrameBatch()/
  // endFrameBatch() route all of it to the chip's hidden frame and swap
  // once, so the viewer only ever sees the finished picture.
  badgeMatrix.beginFrameBatch();
  badgeMatrix.clear(0);
  badgeMatrix.drawMask(mask, kRadarBlipBrightness, 0);

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

constexpr float kPi = 3.14159265f;

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

// FPS is smoothed (exponential moving average) rather than shown raw:
// render() is called once per GUIManager tick, so an unsmoothed 1/dt
// reading jitters wildly frame to frame on a display refresh loop that
// isn't perfectly metronomic.
float gFpsSmoothed = 60.0f;

void drawVectortankFooter(oled& d, const Camera& cam, float dtS) {
  if (dtS > 0.0f) {
    float instFps = 1.0f / dtS;
    gFpsSmoothed = gFpsSmoothed * 0.9f + instFps * 0.1f;
  }

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
  std::snprintf(fpsBuf, sizeof(fpsBuf), "%d fps",
               static_cast<int>(lroundf(gFpsSmoothed)));
  d.drawStr(2, OLEDLayout::kFooterTextBaseY, fpsBuf);

  drawCompass(d, cam);

  // Stubbed: no damage/health model exists yet, just a fixed readout so the
  // footer's layout and the eventual wiring are settled ahead of that work.
  const char* hpBuf = "HP 100%";
  int hpW = d.getStrWidth(hpBuf);
  d.drawStr(kScreenW - 2 - hpW, OLEDLayout::kFooterTextBaseY, hpBuf);
}

// ── Pause menu overlay ───────────────────────────────────────────────────
//
// Back used to pop the screen directly (instant exit); it now opens this
// overlay instead so a stray back-press mid-game doesn't lose the run.
// The overlay draws on top of a frozen frame of the scene rather than its
// own screen, matching how a pause menu should read ("the game is still
// here, just paused"). Only one item exists today (Exit), but it's drawn
// as a list so a future item doesn't need a redesign.

constexpr int kPauseMenuX = 24;
constexpr int kPauseMenuY = 18;
constexpr int kPauseMenuW = 80;
constexpr int kPauseMenuH = 28;

void drawPauseMenu(oled& d) {
  d.setDrawColor(0);
  d.drawBox(kPauseMenuX, kPauseMenuY, kPauseMenuW, kPauseMenuH);
  d.setDrawColor(1);
  d.drawRFrame(kPauseMenuX, kPauseMenuY, kPauseMenuW, kPauseMenuH, 0);
  d.drawStr(kPauseMenuX + 8, kPauseMenuY + 11, "PAUSED");
  d.drawStr(kPauseMenuX + 8, kPauseMenuY + 23, "> Exit");
}

void drawScene(oled& d, const Camera& cam, const Artifact artifacts[],
               int artifactCount, const Particle particles[],
               int particlePoolSize, const Projectile projectiles[],
               int projectileCount) {
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
    d.drawPixel(static_cast<int>(sx), static_cast<int>(sy));
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

void stepProjectiles(Projectile projectiles[], int count, float dtS,
                     Artifact artifacts[], int artifactCount,
                     Particle particles[], int particlePoolSize,
                     const Camera& cam) {
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
      continue;
    }
    for (int j = 0; j < artifactCount; j++) {
      Artifact& a = artifacts[j];
      if (!a.alive) continue;
      if (p.y > a.height) continue;
      float dx = p.x - a.cx;
      float dz = p.z - a.cz;
      if (dx > -a.half && dx < a.half && dz > -a.half && dz < a.half) {
        a.alive = false;
        p.alive = false;
        spawnExplosion(particles, particlePoolSize, p.x, p.y, p.z);
        maybeStrobeFromExplosion(cam, p.x, p.z);
        markExplosionForRadar(p.x, p.z);
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

constexpr int kPlayArtifactCount = 12;

}  // namespace

void VectortankScreen::onEnter(GUIManager& gui) {
  (void)gui;
  seedRng(1);
  gCam = Camera();
  gArtifactCount = makeArtifacts(kPlayArtifactCount, gArtifacts, gCam);
  for (int i = 0; i < kMaxParticles; i++) gParticles[i].active = false;
  for (int i = 0; i < kMaxProjectiles; i++) gProjectiles[i].alive = false;
  lastFrameMs_ = millis();
  lastFireMs_ = 0;
  lastRadarMs_ = 0;
  paused_ = false;
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
    drawRadar(gCam, gArtifacts, gArtifactCount);
  }

  d.setDrawColor(1);
  drawScene(d, gCam, gArtifacts, gArtifactCount, gParticles, kMaxParticles,
            gProjectiles, kMaxProjectiles);

  drawVectortankFooter(d, gCam, dtS);

  if (paused_) drawPauseMenu(d);
}

void VectortankScreen::handleInput(const Inputs& inp, int16_t cursorX,
                                    int16_t cursorY, GUIManager& gui) {
  (void)cursorX;
  (void)cursorY;

  if (paused_) {
    // The game itself is frozen while this overlay is up, so right/b is
    // free to mean what it means everywhere else on the badge (device
    // "B" selection button) instead of strafe; down still closes the
    // menu and resumes play.
    if (inp.edges().downPressed) {
      paused_ = false;
      return;
    }
    if (inp.edges().rightPressed) {
      gui.popScreen();
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
    return;
  }

  uint32_t now = millis();
  float dtS = (now - lastFrameMs_) / 1000.0f;
  float xDir = (static_cast<float>(inp.joyX()) - 2047.0f) / 2047.0f;
  float yDir = (static_cast<float>(inp.joyY()) - 2047.0f) / 2047.0f;
  gCam.move(xDir, yDir, dtS);
  if (inp.buttons().left) gCam.strafe(-1.0f, dtS);
  if (inp.buttons().right) gCam.strafe(1.0f, dtS);
  for (int i = 0; i < kMaxParticles; i++) {
    stepParticle(gParticles[i], dtS);
  }
  stepProjectiles(gProjectiles, kMaxProjectiles, dtS, gArtifacts,
                  gArtifactCount, gParticles, kMaxParticles, gCam);

  if (inp.edges().upPressed && now - lastFireMs_ >= kFireCooldownMs) {
    fireProjectile(gProjectiles, kMaxProjectiles, gCam);
    lastFireMs_ = now;
  }
}
