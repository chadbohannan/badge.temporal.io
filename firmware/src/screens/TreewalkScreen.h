#pragma once
#include <string>
#include <vector>

#include "JoyRamp.h"
#include "Screen.h"
#include "TreewalkStore.h"

// ─── TREEWALK: Choose-Your-Own-Adventure reader ────────────────────────────
//
// Walks a story DAG stored in /treewalk/treewalk.db (design and data model:
// wiki/components/treewalk.md). Each node shows its body, then its choices
// (at most three) as one word-wrapped region the stick scrolls. A, B and Y
// take the first, second and third choice; X opens a menu with Continue (close
// it), Back, Books (the book picker) and Exit. Back returns to the page the
// reader came from, at the line they left it, from an in-memory history; with
// no history (after a resume) it goes to the node's parentID, and it is
// disabled at a root with no history. A node with no choices is an ending. The picker and menu follow the badge's
// confirm/cancel setting (B confirms by default); the three choices are
// always the literal A, B and Y. The history lives
// in RAM and starts empty on each open. Only the current (bookID, nodeID)
// persists, in the "badge_treewalk" NVS namespace; it is checked against the
// database on open because a factory flash replaces the database but keeps
// NVS. The LED matrix is not touched.

class TreewalkScreen : public Screen {
 public:
  void onEnter(GUIManager& gui) override;
  void onExit(GUIManager& gui) override;
  void render(oled& d, GUIManager& gui) override;
  void handleInput(const Inputs& inputs, int16_t cursorX, int16_t cursorY,
                   GUIManager& gui) override;
  ScreenId id() const override { return kScreenTreewalk; }
  bool showCursor() const override { return false; }
  bool suppressesForceSleep() const override { return true; }

 private:
  enum class Mode : uint8_t { kMessage, kPicker, kReading, kMenu };

  struct Line {
    std::string text;
    int8_t glyph = -1;  // choice slot 0..2 drawn before the first line of a choice
    bool indent = false;
  };

  // A page left by taking a choice, and the scroll line it was left at.
  struct Visit {
    int32_t nodeId;
    int16_t scroll;
  };

  struct PickerLine {
    std::string text;
    int book;  // index into books_
  };

  void openPicker();
  bool goTo(int32_t nodeId, int scroll = 0);
  void goBack();
  bool canGoBack() const { return !history_.empty() || node_.hasParent; }
  void layout();
  void toast(const char* text);
  int8_t stickDir(const Inputs& inputs);
  void handlePicker(const Inputs& inputs, int8_t dir, GUIManager& gui);
  void handleReading(const Inputs& inputs, int8_t dir);
  void handleMenu(const Inputs& inputs, int8_t dir, GUIManager& gui);
  void drawPicker(oled& d);
  void drawReading(oled& d);
  void drawMenu(oled& d);

  treewalk::Store store_;
  Mode mode_ = Mode::kMessage;
  const char* message_ = "";
  std::vector<treewalk::Book> books_;
  std::vector<PickerLine> pickerLines_;  // book names word-wrapped
  int pickerSel_ = 0;
  int32_t bookId_ = 0;
  int32_t nodeId_ = 0;
  bool haveBook_ = false;
  treewalk::Node node_;
  std::vector<Line> lines_;
  std::vector<Visit> history_;  // pages behind the current one, oldest first
  int scroll_ = 0;
  int menuSel_ = 0;
  const char* toast_ = nullptr;
  uint32_t toastUntilMs_ = 0;
  JoyRamp joyRamp_;
};
