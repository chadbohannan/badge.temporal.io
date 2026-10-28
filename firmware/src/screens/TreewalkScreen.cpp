#include "TreewalkScreen.h"

#include <Preferences.h>

#include "../hardware/Inputs.h"
#include "../hardware/oled.h"
#include "../ui/ButtonGlyphs.h"
#include "../ui/GUI.h"

using treewalk::kMaxChoices;

namespace {

constexpr const char* kDbPath = "/treewalk/treewalk.db";
constexpr const char* kNvsNamespace = "badge_treewalk";

constexpr int kLineH = 10;
constexpr int kVisibleLines = 6;
constexpr int kBodyCols = 20;    // 6x10 font: 120 px, leaving room for the scroll bar
constexpr int kChoiceCols = 18;  // after the 10 px button glyph and a gap
constexpr int kChoiceX = 13;
constexpr int kPickerCols = 20;  // 120 px after the 8 px selection marker
constexpr int kPickerRows = 4;
constexpr uint32_t kToastMs = 1800;
constexpr size_t kMaxHistory = 64;  // deeper than any bundled path; the oldest page drops off

enum MenuItem { kMenuContinue, kMenuBack, kMenuBooks, kMenuExit, kMenuCount };

// Choice slot -> button. Order matches the brief: A, B, Y.
ButtonGlyphs::Button choiceButton(int slot) {
  static const ButtonGlyphs::Button kButtons[kMaxChoices] = {
      ButtonGlyphs::Button::A, ButtonGlyphs::Button::B, ButtonGlyphs::Button::Y};
  return kButtons[slot];
}

// Greedy word wrap into `cols` columns. '\n' ends a line; a word longer than a
// line is split. Runs of spaces at a wrap point are dropped.
void wrap(const std::string& s, int cols, std::vector<std::string>* out) {
  size_t i = 0;
  while (i <= s.size()) {
    size_t nl = s.find('\n', i);
    if (nl == std::string::npos) nl = s.size();
    const std::string para = s.substr(i, nl - i);
    size_t p = 0;
    if (para.empty()) out->push_back("");
    while (p < para.size()) {
      while (p < para.size() && para[p] == ' ' && !out->empty() && p > 0) p++;
      if (p >= para.size()) break;
      if (para.size() - p <= static_cast<size_t>(cols)) {
        out->push_back(para.substr(p));
        break;
      }
      size_t cut = para.rfind(' ', p + cols);
      if (cut == std::string::npos || cut <= p) cut = p + cols;
      out->push_back(para.substr(p, cut - p));
      p = cut;
    }
    if (nl == s.size()) break;
    i = nl + 1;
  }
}

bool readSave(int32_t* book, int32_t* node) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return false;
  const bool has = prefs.isKey("bookID") && prefs.isKey("nodeID");
  if (has) {
    *book = prefs.getInt("bookID", 0);
    *node = prefs.getInt("nodeID", 0);
  }
  prefs.end();
  return has;
}

void writeSave(int32_t book, int32_t node) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return;
  prefs.putInt("bookID", book);
  prefs.putInt("nodeID", node);
  prefs.end();
}

}  // namespace

void TreewalkScreen::onEnter(GUIManager& gui) {
  (void)gui;
  joyRamp_.reset();
  toast_ = nullptr;
  haveBook_ = false;
  history_.clear();
  if (!store_.open(kDbPath)) {
    mode_ = Mode::kMessage;
    message_ = "No stories found";
    return;
  }
  books_ = store_.books();
  if (books_.empty()) {
    mode_ = Mode::kMessage;
    message_ = "No stories in the file";
    return;
  }
  int32_t book = 0, node = 0;
  if (readSave(&book, &node) && store_.nodeExists(book, node)) {
    bookId_ = book;
    haveBook_ = true;
    if (goTo(node)) return;
  }
  haveBook_ = false;
  openPicker();
}

void TreewalkScreen::onExit(GUIManager& gui) {
  (void)gui;
  store_.close();
  books_.clear();
  pickerLines_.clear();
  lines_.clear();
  history_.clear();
  node_ = treewalk::Node();
}

void TreewalkScreen::openPicker() {
  mode_ = Mode::kPicker;
  pickerSel_ = 0;
  pickerLines_.clear();
  std::vector<std::string> rows;
  for (size_t i = 0; i < books_.size(); i++) {
    rows.clear();
    wrap(books_[i].name, kPickerCols, &rows);
    if (rows.empty()) rows.push_back("");
    for (const std::string& r : rows) pickerLines_.push_back({r, static_cast<int>(i)});
  }
  for (size_t i = 0; i < books_.size(); i++) {
    if (haveBook_ && books_[i].id == bookId_) pickerSel_ = static_cast<int>(i);
  }
}

// Shows node `nodeId` of the current book at line `scroll` and saves it.
// False, changing nothing, if the node cannot be read.
bool TreewalkScreen::goTo(int32_t nodeId, int scroll) {
  treewalk::Node n;
  if (!store_.loadNode(bookId_, nodeId, &n)) return false;
  node_ = std::move(n);
  nodeId_ = nodeId;
  writeSave(bookId_, nodeId_);
  mode_ = Mode::kReading;
  layout();
  const int maxScroll = std::max(0, static_cast<int>(lines_.size()) - kVisibleLines);
  scroll_ = std::min(std::max(scroll, 0), maxScroll);
  return true;
}

// Back to the page the reader came from, at the line they left it. With no
// history (after a resume) it falls back to the node's parentID, at the top.
void TreewalkScreen::goBack() {
  bool ok = false;
  if (!history_.empty()) {
    const Visit v = history_.back();
    ok = goTo(v.nodeId, v.scroll);
    if (ok) history_.pop_back();
  } else if (node_.hasParent) {
    ok = goTo(node_.parentId);
  }
  if (!ok) {
    mode_ = Mode::kReading;
    toast("Missing page");
  }
}

void TreewalkScreen::layout() {
  lines_.clear();
  std::vector<std::string> rows;
  wrap(node_.body, kBodyCols, &rows);
  for (const std::string& r : rows) lines_.push_back({r, -1, false});
  lines_.push_back({"", -1, false});
  if (node_.choices.empty()) {
    lines_.push_back({"-- THE END --", -1, false});
    return;
  }
  for (size_t c = 0; c < node_.choices.size(); c++) {
    rows.clear();
    wrap(node_.choices[c].label, kChoiceCols, &rows);
    if (rows.empty()) rows.push_back("");
    for (size_t r = 0; r < rows.size(); r++) {
      lines_.push_back({rows[r], r == 0 ? static_cast<int8_t>(c) : static_cast<int8_t>(-1), true});
    }
  }
}

void TreewalkScreen::toast(const char* text) {
  toast_ = text;
  toastUntilMs_ = millis() + kToastMs;
}

int8_t TreewalkScreen::stickDir(const Inputs& inputs) {
  const int16_t dy = static_cast<int16_t>(inputs.joyY()) - 2047;
  constexpr int16_t kDeadband = 400;
  int8_t dir = 0;
  if (dy > kDeadband) dir = 1;
  else if (dy < -kDeadband) dir = -1;
  return joyRamp_.tick(dir, millis()) ? dir : 0;
}

void TreewalkScreen::handleInput(const Inputs& inputs, int16_t cursorX, int16_t cursorY,
                                 GUIManager& gui) {
  (void)cursorX;
  (void)cursorY;
  const int8_t dir = stickDir(inputs);
  switch (mode_) {
    case Mode::kMessage:
      if (inputs.edges().confirmPressed || inputs.edges().cancelPressed || inputs.edges().xPressed) gui.popScreen();
      break;
    case Mode::kPicker: handlePicker(inputs, dir, gui); break;
    case Mode::kReading: handleReading(inputs, dir); break;
    case Mode::kMenu: handleMenu(inputs, dir, gui); break;
  }
}

void TreewalkScreen::handlePicker(const Inputs& inputs, int8_t dir, GUIManager& gui) {
  const int n = static_cast<int>(books_.size());
  if (dir > 0 && pickerSel_ < n - 1) pickerSel_++;
  if (dir < 0 && pickerSel_ > 0) pickerSel_--;
  const Inputs::ButtonEdges& e = inputs.edges();
  if (e.xPressed) {
    if (haveBook_) {
      mode_ = Mode::kReading;  // back to the page we came from, at the same line
      return;
    }
    gui.popScreen();
    return;
  }
  if (!e.confirmPressed) return;
  int32_t root = 0;
  const int32_t book = books_[pickerSel_].id;
  if (!store_.rootOf(book, &root)) {
    toast("Book has no first page");
    return;
  }
  const int32_t previousBook = bookId_;
  const bool previousHave = haveBook_;
  bookId_ = book;
  haveBook_ = true;
  if (!goTo(root)) {
    bookId_ = previousBook;
    haveBook_ = previousHave;
    toast("Cannot read first page");
    return;
  }
  history_.clear();
}

void TreewalkScreen::handleReading(const Inputs& inputs, int8_t dir) {
  const int maxScroll = std::max(0, static_cast<int>(lines_.size()) - kVisibleLines);
  scroll_ = std::min(std::max(scroll_ + dir, 0), maxScroll);
  const Inputs::ButtonEdges& e = inputs.edges();
  if (e.xPressed) {
    mode_ = Mode::kMenu;
    menuSel_ = kMenuContinue;
    return;
  }
  int slot = -1;
  if (e.aPressed) slot = 0;
  else if (e.bPressed) slot = 1;
  else if (e.yPressed) slot = 2;
  if (slot < 0 || slot >= static_cast<int>(node_.choices.size())) return;
  const Visit from{nodeId_, static_cast<int16_t>(scroll_)};
  if (!goTo(node_.choices[slot].childId)) {
    toast("Missing page");
    return;
  }
  if (history_.size() >= kMaxHistory) history_.erase(history_.begin());
  history_.push_back(from);
}

void TreewalkScreen::handleMenu(const Inputs& inputs, int8_t dir, GUIManager& gui) {
  // Back is skipped when there is nowhere to go back to.
  int next = menuSel_ + dir;
  if (next == kMenuBack && !canGoBack()) next += dir;
  if (dir != 0 && next >= kMenuContinue && next < kMenuCount) menuSel_ = next;
  const Inputs::ButtonEdges& e = inputs.edges();
  if (e.xPressed || e.cancelPressed) {
    mode_ = Mode::kReading;
    return;
  }
  if (!e.confirmPressed) return;
  switch (menuSel_) {
    case kMenuContinue: mode_ = Mode::kReading; break;
    case kMenuBack: goBack(); break;
    case kMenuBooks: openPicker(); break;
    case kMenuExit: gui.popScreen(); break;
  }
}

void TreewalkScreen::render(oled& d, GUIManager& gui) {
  (void)gui;
  u8g2_t* u = d.raw();
  u8g2_SetFont(u, u8g2_font_6x10_tr);
  u8g2_SetFontPosBaseline(u);
  u8g2_SetDrawColor(u, 1);
  switch (mode_) {
    case Mode::kMessage:
      u8g2_DrawStr(u, 0, 24, message_);
      ButtonGlyphs::drawInlineHint(d, 0, 62, "Confirm Exit");
      return;
    case Mode::kPicker: drawPicker(d); break;
    case Mode::kReading: drawReading(d); break;
    case Mode::kMenu:
      drawReading(d);
      drawMenu(d);
      break;
  }
  if (toast_ && static_cast<int32_t>(millis() - toastUntilMs_) < 0) {
    const int w = d.getStrWidth(toast_) + 8;
    const int x = (128 - w) / 2;
    u8g2_SetDrawColor(u, 0);
    u8g2_DrawBox(u, x, 26, w, 14);
    u8g2_SetDrawColor(u, 1);
    u8g2_DrawFrame(u, x, 26, w, 14);
    u8g2_DrawStr(u, x + 4, 37, toast_);
  }
}

void TreewalkScreen::drawPicker(oled& d) {
  u8g2_t* u = d.raw();
  u8g2_DrawStr(u, 0, 9, "STORIES");
  u8g2_DrawHLine(u, 0, 11, 128);
  // Scroll by wrapped lines, keeping the whole selected name in view (its
  // start, if it is taller than the list).
  const int total = static_cast<int>(pickerLines_.size());
  int first = 0;
  while (first < total && pickerLines_[first].book != pickerSel_) first++;
  int last = first;
  while (last < total && pickerLines_[last].book == pickerSel_) last++;
  const int top = std::min(std::max(last - kPickerRows, 0), first);
  for (int i = 0; i < kPickerRows && top + i < total; i++) {
    const PickerLine& ln = pickerLines_[top + i];
    const int y = 22 + i * kLineH;
    if (top + i == first) u8g2_DrawStr(u, 0, y, ">");
    u8g2_DrawStr(u, 8, y, ln.text.c_str());
  }
  ButtonGlyphs::drawInlineHint(d, 0, 62, haveBook_ ? "Confirm Open  X Return" : "Confirm Open  X Exit");
}

void TreewalkScreen::drawReading(oled& d) {
  u8g2_t* u = d.raw();
  const int total = static_cast<int>(lines_.size());
  for (int row = 0; row < kVisibleLines && scroll_ + row < total; row++) {
    const Line& ln = lines_[scroll_ + row];
    const int top = row * kLineH;
    if (ln.glyph >= 0) ButtonGlyphs::draw(d, choiceButton(ln.glyph), 0, top);
    u8g2_DrawStr(u, ln.indent ? kChoiceX : 0, top + 9, ln.text.c_str());
  }
  if (total > kVisibleLines) {
    const int thumb = std::max(4, 64 * kVisibleLines / total);
    const int y = (64 - thumb) * scroll_ / (total - kVisibleLines);
    u8g2_DrawVLine(u, 127, y, thumb);
  }
}

void TreewalkScreen::drawMenu(oled& d) {
  u8g2_t* u = d.raw();
  constexpr int kX = 24, kY = 6, kW = 80, kH = 52;
  static const char* const kLabels[kMenuCount] = {"Continue", "Back", "Books", "Exit"};
  u8g2_SetDrawColor(u, 0);
  u8g2_DrawBox(u, kX, kY, kW, kH);
  u8g2_SetDrawColor(u, 1);
  u8g2_DrawFrame(u, kX, kY, kW, kH);
  for (int i = 0; i < kMenuCount; i++) {
    const int y = kY + 12 + i * 11;
    if (i == menuSel_) u8g2_DrawStr(u, kX + 8, y, ">");
    u8g2_DrawStr(u, kX + 16, y, kLabels[i]);
    if (i == kMenuBack && !canGoBack()) {
      // Disabled: knock out alternate pixels so the label reads as dimmed.
      u8g2_SetDrawColor(u, 0);
      for (int py = y - 8; py <= y + 1; py++)
        for (int px = kX + 16; px < kX + 16 + 24; px++)
          if ((px + py) & 1) u8g2_DrawPixel(u, px, py);
      u8g2_SetDrawColor(u, 1);
    }
  }
}
