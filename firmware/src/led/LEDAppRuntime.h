#pragma once

#include <Arduino.h>

#include "../infra/Scheduler.h"

class LEDmatrix;

class LEDAppRuntime : public IService {
 public:
  enum class Mode : uint8_t {
    Temporal = 0,
    Replay,
    Sparkle,
    Rain,
    Wave,
    Life,
    LifeRandom,
    Custom,
    Off,
    PythonApp,
  };

  static constexpr uint8_t kPythonSlugCap = 32;

  static constexpr uint8_t kFrameRows = 8;
  static constexpr uint8_t kStaleGrace = 8;
  static constexpr uint16_t kDefaultDelay = 100;  // 10 fps
  static constexpr uint8_t kDefaultBrightness = 20;
  using Frame = uint8_t[kFrameRows];

  // User-editable Replay wordmark. Text is rasterized at runtime into a
  // scrolling column buffer via the OLED's u8g2 font engine (see
  // rebuildReplayColumns in the .cpp) rather than stored as a fixed bitmap.
  static constexpr uint8_t kReplayTextCap = 25;  // 24 chars + NUL
  static constexpr uint16_t kReplayColumnPad = 8;
  static constexpr uint16_t kReplayColumnCap = 224;

  // Fixed shortlist of fonts confirmed to fit within the 8-row matrix
  // height (see wiki/components/led-app-runtime.md for how these were
  // picked out of FontCatalog). Values are stable indices persisted (by
  // string id, not index) in /led_state.json.
  enum class ReplayFont : uint8_t {
    Spleen5x8 = 0,
    Font5x8,
    Font5x7,
    Font4x6,
    U8glib4,
  };

  void begin(LEDmatrix* matrix);
  void service() override;
  const char* name() const override { return "LEDAppRuntime"; }

  void restoreAmbient();
  void beginPreview(Mode mode, const uint8_t* draft = nullptr);
  void updatePreview(Mode mode, const uint8_t* draft = nullptr);
  void endPreview();

  void commitMode(Mode mode);
  void commitMode(Mode mode, uint16_t delay, uint8_t brightness);
  void commitLife(const uint8_t* seed);
  void commitCustom(const uint8_t* pattern);
  void commitReplay(const char* text, ReplayFont font);

  // Live-preview a candidate Replay text/font while the user is still
  // editing, without touching the committed state or /led_state.json.
  // Mirrors updatePreview()'s role for the Life/Custom byte-array drafts.
  void previewReplay(const char* text, ReplayFont font);

  // Persist a Python-driven ambient matrix app. The slug names a folder
  // under /apps/ whose matrix.py registers a callback via
  // badge.matrix_app_start(...). After commit, MicroPythonMatrixService
  // drains the pending-exec request and sources the registration script.
  void commitMatrixApp(const char* slug);
  const char* pythonAppSlug() const { return state_.pythonAppSlug; }
  // Pop-and-clear the pending exec slug. Returns true and copies the
  // slug into `out` (NUL-terminated) when there is work to do.
  bool consumePendingExec(char* out, size_t cap);

  void beginOverride();
  void endOverride();

  Mode mode() const { return state_.mode; }
  uint16_t delay() const { return state_.delay; }
  uint8_t brightness() const { return state_.brightness; }
  void setDelay(uint16_t d) { state_.delay = d < 5 ? 5 : (d > 10000 ? 10000 : d); }
  void setBrightness(uint8_t b) { state_.brightness = b; }
  const uint8_t* lifeSeed() const { return state_.lifeSeed; }
  const uint8_t* customPattern() const { return state_.custom; }
  const char* replayText() const { return state_.replayText; }
  ReplayFont replayFont() const { return state_.replayFont; }

  static const char* modeId(Mode mode);
  static const char* modeName(Mode mode);
  static Mode modeAt(uint8_t index);
  static uint8_t modeIndex(Mode mode);
  static uint8_t modeCount();
  // Not static: Replay's poster now depends on the instance's saved
  // text/font, not just a fixed bitmap, so it needs state_ access.
  void posterFrame(Mode mode, uint8_t out[kFrameRows]);

  static uint8_t replayFontCount();
  static ReplayFont replayFontAt(uint8_t index);
  static const char* replayFontId(ReplayFont font);
  static const char* replayFontLabel(ReplayFont font);

 private:
  struct State {
    Mode mode = Mode::Temporal;
    uint16_t delay = kDefaultDelay;
    uint8_t brightness = kDefaultBrightness;
    uint8_t lifeSeed[kFrameRows] = {
        0x00, 0x00, 0x20, 0x10, 0x70, 0x00, 0x00, 0x00,
    };
    uint8_t custom[kFrameRows] = {
        0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00,
    };
    bool lifeRandomize = false;
    char pythonAppSlug[kPythonSlugCap] = {};
    char replayText[kReplayTextCap] = "REPLAY";
    ReplayFont replayFont = ReplayFont::Spleen5x8;
  };

  static bool isLifeMode(Mode m) {
    return m == Mode::Life || m == Mode::LifeRandom;
  }

  void loadState();
  void saveState();
  void restartActiveMode();
  void stopNativeAnimation();
  void drawFrame(const uint8_t frame[kFrameRows]);
  void copyFrame(uint8_t dst[kFrameRows], const uint8_t* src,
                 const uint8_t fallback[kFrameRows]);
  const uint8_t* activeSeed() const;
  Mode activeMode() const;
  uint16_t activeInterval() const;
  void buildFrame(Mode mode, uint8_t out[kFrameRows]);
  void buildLifeFrame(uint8_t out[kFrameRows]);
  uint8_t randomByte();

  // Resolves which text/font should currently drive the Replay scroll:
  // the in-progress preview draft while one is active, else the
  // committed state.
  void effectiveReplay(const char** text, ReplayFont* font) const;
  // Rasterizes `text` in `font` into replayColumns_ via the OLED's u8g2
  // engine, caching against the last-built text/font so repeated calls
  // with unchanged input (e.g. every tick) are a no-op.
  void ensureReplayColumns(const char* text, ReplayFont font);
  void rebuildReplayColumns(const char* text, ReplayFont font);

  LEDmatrix* matrix_ = nullptr;
  State state_;
  bool loaded_ = false;

  bool previewActive_ = false;
  Mode previewMode_ = Mode::Temporal;
  uint8_t previewDraft_[kFrameRows] = {};
  bool previewHasDraft_ = false;
  char previewReplayText_[kReplayTextCap] = {};
  ReplayFont previewReplayFont_ = ReplayFont::Spleen5x8;
  bool previewHasReplayDraft_ = false;

  // Cache of the last text/font rasterized into replayColumns_, so
  // ensureReplayColumns() only re-rasterizes when the effective text or
  // font actually changed.
  char replayColumnsBuiltText_[kReplayTextCap] = {};
  ReplayFont replayColumnsBuiltFont_ = ReplayFont::Spleen5x8;
  bool replayColumnsBuilt_ = false;
  uint8_t replayColumns_[kReplayColumnCap] = {};
  uint16_t replayColumnCount_ = kReplayColumnCap;

  Mode runningMode_ = Mode::Off;
  bool runningValid_ = false;
  uint32_t lastTickMs_ = 0;
  uint32_t frameIndex_ = 0;
  uint8_t life_[kFrameRows] = {};
  uint8_t lifeNext_[kFrameRows] = {};
  uint8_t lifePrev_[kFrameRows] = {};
  uint8_t lifePrev2_[kFrameRows] = {};
  uint8_t staleCount_ = 0;
  bool lifeRestart_ = false;
  uint8_t rain_[kFrameRows] = {};
  uint32_t rng_ = 0x43C9A1D7;
  uint8_t overrideDepth_ = 0;

  bool pendingPyExec_ = false;
  char pendingPySlug_[kPythonSlugCap] = {};
};

extern LEDAppRuntime ledAppRuntime;

extern "C" void led_app_runtime_restore_ambient(void);
extern "C" void led_app_runtime_begin_override(void);
extern "C" void led_app_runtime_end_override(void);
