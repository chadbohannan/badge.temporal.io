#include "VectortankScreen.h"

#include <cmath>

#include "../hardware/Inputs.h"
#include "../hardware/oled.h"
#include "../ui/GUI.h"

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

  void move(float xDir, float yDir, float dtS, float turnRate = 2.2f,
            float moveRate = 6.0f) {
    if (xDir != 0.0f) yaw += turnRate * xDir * dtS;
    if (yDir != 0.0f) {
      float step = -moveRate * yDir * dtS;
      x += step * sinf(yaw);
      z += step * cosf(yaw);
    }
  }

  Vec3 toCameraSpace(const Vec3& w) const {
    float dx = w.x - x;
    float dz = w.z - z;
    float cosY = cosf(-yaw);
    float sinY = sinf(-yaw);
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
constexpr int kMaxParticles = 256;

struct Artifact {
  Vec3 verts[8];
};

struct Particle {
  float x, y, z;
  float vx, vy, vz;
  float age, lifetime;
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

void respawnParticle(Particle& p, const Camera& cam) {
  p.x = cam.x + randRange(-3.0f, 3.0f);
  p.y = randRange(0.2f, 2.5f);
  p.z = cam.z + randRange(2.0f, 10.0f);
  p.vx = randRange(-0.4f, 0.4f);
  p.vy = randRange(0.1f, 0.6f);
  p.vz = randRange(-0.4f, 0.4f);
  p.age = 0.0f;
  p.lifetime = randRange(0.8f, 2.0f);
}

void stepParticle(Particle& p, float dtS, const Camera& cam) {
  p.x += p.vx * dtS;
  p.y += p.vy * dtS;
  p.z += p.vz * dtS;
  p.age += dtS;
  if (p.age >= p.lifetime) respawnParticle(p, cam);
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
  }
  return count;
}

int makeParticles(int count, Particle out[], const Camera& cam) {
  count = count > kMaxParticles ? kMaxParticles : count;
  for (int i = 0; i < count; i++) respawnParticle(out[i], cam);
  return count;
}

void drawArtifact(oled& d, const Camera& cam, const Artifact& a) {
  float sx[8], sy[8];
  bool visible[8];
  bool any = false;
  for (int i = 0; i < 8; i++) {
    Vec3 c = cam.toCameraSpace(a.verts[i]);
    visible[i] = project(c, sx[i], sy[i]);
    any = any || visible[i];
  }
  if (!any) return;
  for (auto& edge : kBoxEdges) {
    int ea = edge[0], eb = edge[1];
    if (!visible[ea] || !visible[eb]) continue;
    d.drawLine(static_cast<int>(sx[ea]), static_cast<int>(sy[ea]),
               static_cast<int>(sx[eb]), static_cast<int>(sy[eb]));
  }
}

void drawParticle(oled& d, const Camera& cam, const Particle& p) {
  Vec3 c = cam.toCameraSpace({p.x, p.y, p.z});
  float sx, sy;
  if (project(c, sx, sy)) {
    d.drawPixel(static_cast<int>(sx), static_cast<int>(sy));
  }
}

void drawScene(oled& d, const Camera& cam, const Artifact artifacts[],
               int artifactCount, const Particle particles[],
               int particleCount) {
  for (int i = 0; i < artifactCount; i++) drawArtifact(d, cam, artifacts[i]);
  for (int i = 0; i < particleCount; i++) drawParticle(d, cam, particles[i]);
}

// ── Interactive play state (module-local; one VectortankScreen instance
// exists per GUIManager registration, matching the rest of this codebase's
// static-screen-instance convention). ───────────────────────────────────────

Camera gCam;
Artifact gArtifacts[kMaxArtifacts];
int gArtifactCount = 0;
Particle gParticles[kMaxParticles];
int gParticleCount = 0;

constexpr int kPlayArtifactCount = 12;
constexpr int kPlayParticleCount = 20;

}  // namespace

void VectortankScreen::onEnter(GUIManager& gui) {
  (void)gui;
  seedRng(1);
  gCam = Camera();
  gArtifactCount = makeArtifacts(kPlayArtifactCount, gArtifacts, gCam);
  gParticleCount = makeParticles(kPlayParticleCount, gParticles, gCam);
  lastFrameMs_ = millis();
}

void VectortankScreen::render(oled& d, GUIManager& gui) {
  (void)gui;
  uint32_t now = millis();
  float dtS = (now - lastFrameMs_) / 1000.0f;
  lastFrameMs_ = now;

  d.setDrawColor(1);
  drawScene(d, gCam, gArtifacts, gArtifactCount, gParticles, gParticleCount);
}

void VectortankScreen::handleInput(const Inputs& inp, int16_t cursorX,
                                    int16_t cursorY, GUIManager& gui) {
  (void)cursorX;
  (void)cursorY;
  if (inp.edges().cancelPressed) {
    gui.popScreen();
    return;
  }

  uint32_t now = millis();
  float dtS = (now - lastFrameMs_) / 1000.0f;
  float xDir = (static_cast<float>(inp.joyX()) - 2047.0f) / 2047.0f;
  float yDir = (static_cast<float>(inp.joyY()) - 2047.0f) / 2047.0f;
  gCam.move(xDir, yDir, dtS);
  for (int i = 0; i < gParticleCount; i++) {
    stepParticle(gParticles[i], dtS, gCam);
  }
}
