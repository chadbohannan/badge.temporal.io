# TREEWALK: Choose-Your-Own-Adventure Reader

TREEWALK is a native badge app that reads a Choose-Your-Own-Adventure (CYOA) story stored in SQLite. The reader walks a story DAG one node at a time: each node shows a body of text and up to three choices, and a node with no choices is an ending. The design was agreed on 2026-09-29 (brief plus interview) and the reader was built on 2026-09-30; this page records both. It is the fourth native app after [Vectortank](vectortank.md), [Helgrind](helgrind.md) and [PACKIT](packit.md), and it is the first to need a database.

## Brief

- The reader navigates a DAG from a root node to a leaf. The reader has no other goal or score.
- Buttons `a`, `b` and `y` choose the first, second and third choice. Any of them does nothing on a node that has fewer choices. Per [UI conventions](ui-conventions.md), labels drawn on screen come from the button-letter helper, not hardcoded letters.
- `x` opens a menu with **back** and **exit**. Back is disabled at a root node. Exit leaves the app. The built menu also has **continue** and **books** (see Implementation).
- The current node is the only state that persists. The back history (see Data model) lives in RAM only and is lost when the app closes.
- The backing store is SQLite. No indexes are needed.

## Data model

Three plain tables. A book row names a `bookID`. A node row has `bookID`, `nodeID`, `parentID`, and `body`. Each choice maps a label string to a child node ID; a node's choices are its rows in the choice table, in `seq` order, at most three.

```sql
CREATE TABLE book   (bookID INTEGER, name TEXT);
CREATE TABLE node   (bookID INTEGER, nodeID INTEGER, parentID INTEGER, body TEXT);
CREATE TABLE choice (bookID INTEGER, nodeID INTEGER, seq INTEGER, label TEXT, childID INTEGER);
```

The separate choice table (one row per choice) was confirmed on 2026-09-30 over a JSON column or fixed `c1`..`c3` columns; the column types above are `INTEGER` and `TEXT` and are settled. The tables carry no `PRIMARY KEY` or `UNIQUE` constraint on purpose: SQLite builds an automatic index for each one, and the brief says no indexes. Every lookup is a full scan, which is fine for a story of a few hundred nodes.

Rules that follow from the model:

- A **root** node has a `NULL` `parentID`. Each book has one root; back is disabled there unless the reader has a history to go back through.
- An **ending** is a node with zero choice rows. No flag column exists.
- **Back retraces the path, to the line the reader left.** Taking a choice pushes the page and its scroll line onto an in-memory history; back pops it and restores both. This replaced the first build's stateless step to `parentID` on 2026-10-28, after testing showed that back landing at the top of the page loses the reader's place, and because a DAG node can have several parents but stores one `parentID`, so that step did not retrace a path through a different parent. The history is not saved: the brief's rule that only the current node persists still holds across closes and power-off. With no history (after a resume, or when it has been used up), back falls back to `parentID` at the top of the page.
- A `childID` that points at no node in the same book is a data error. The build tool cannot produce one, but a hand-made or community file could, so the reader should show an error and stay on the current node.
- IDs are stable, so a saved `(bookID, nodeID)` still means the same passage after a rebuild: `bookID` is the CRC-32 of the book name and `nodeID` the CRC-32 of the passage name, each masked to 31 bits (never 0), which fits an `int32` in NVS. The cost is that renaming a book or a passage changes its ID. `parentID` is the first parent found walking breadth-first from the start, so it lies on a shortest path.

## One database file, many books

All books live in one file, `treewalk.db`, and the book table names them. The file ships in the factory image and holds the bundled books. Community books arrive by tooling that merges them into that same file. Authors write books in Twee (the Twine text format), and `firmware/scripts/treewalk_build.py` compiles them into the three tables (built 2026-09-30). Its `build` command writes a fresh file or, with `--merge`, adds books to an existing one and replaces a book of the same name; `check` validates any database. Twee sources live in `firmware/treewalk/books/`, with an author guide in `firmware/treewalk/README.md`.

The tool rejects a book with a missing start, a link to a missing passage, more than three choices, an unreachable passage, a cycle (a link back to a passage already on the path, including a self-link), two passages or two books whose names hash to the same ID, and a merge into a file with a different `PRAGMA user_version`. It warns, without failing, on text over 600 characters, labels over 40, empty bodies, and characters the badge font may not draw; curly quotes, en and em dashes, ellipses and non-breaking spaces are converted to ASCII first. Links become choices in order, a line holding only links is dropped from the body, and a link inside a line becomes its label. The file is written with 1,024-byte pages and `user_version` 1, and is `VACUUM`ed so replacing a book leaves no free pages; the same input gives the same bytes. `check` also fails on a WAL-mode file or any index, since the badge VFS reads neither.

Two books are bundled. `lantern-keeper.twee` (*The Lantern Keeper*) has eleven passages, a shared child (the cellar's stair and the start both reach `Stairs`; the lantern and the stairs both reach `Lamp`), one to three choices per passage, and four endings; it is the small test book. `kite-day-cash.twee` (*The Case of the Kite-Day Cash*, added 2026-10-16) has 40 passages written well past the soft limits: body text has a median of 1,216 characters and a maximum of 3,280, about 13 screens of scrolling for a median passage and 34 for the longest, and labels run to 68 characters (five wrapped lines). The tool warns on most of its passages; on 2026-10-16 the owner chose to keep the limits as warnings and ship the book as written. The built file is committed at `firmware/initial_filesystem/treewalk/treewalk.db` (decided 2026-09-30) with its entry in the committed `manifest.json` (70,656 bytes with both books), so builds need no Python step and [`badge_sync`](badge-sync.md) sees it as a normal non-baked file. `CommittedDatabaseTests` in `firmware/scripts/tests/test_treewalk_build.py` rebuilds the file from `firmware/treewalk/books/` and fails, with the regeneration commands in its message, if the committed bytes differ. After changing a book, rebuild the db, then run `TEMPORAL_BADGE_REGEN_MANIFEST=1 python3 scripts/generate_startup_files.py`.

On launch the app shows a book picker when no book is active. Once a node is saved, it resumes at that node. The X menu adds **continue** and **books** to the brief's **back** and **exit** (see Implementation); **books** reopens the picker, and picking a book resets to that book's root. The picker and the **books** item go past the two-item menu in the original brief and were accepted on 2026-09-30.

## Screen layout

The 128×64 OLED shows the body text, then the choices appended after it, as one scrolling region. The stick scrolls it. Body text and choice labels word-wrap in the 6x10 font (20 and 18 columns, see Implementation) with no hard length cap; the Twee tool warns above a soft limit of 600 characters of body and 40 per label, which fits about six and two-and-a-half screens of lines. This is the simplest layout, but choices are visible only after scrolling past the body; the alternative of pinning them to the bottom row was offered and not chosen. The ambient 8×8 LED matrix is left alone: TREEWALK takes no `beginOverride()` from [`LEDAppRuntime`](led-app-runtime.md).

The screen implements `render()` and `handleInput()` directly, like the other native apps in the [screen stack](gui-screen-stack.md). The X menu is a small modal popup with the same shape as PACKIT's pause menu.

## Storage

**SQLite engine.** SQLite is vendored as the amalgamation in `firmware/lib/sqlite3/` with a read-only VFS over FATFS, and it is built and tested on the host. The [SQLite page](sqlite.md) owns the version, the build flags, the VFS design and the test setup. What TREEWALK needs to know: open a book with `sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, kSqliteBadgeVfs)`, use one connection from one task, and expect full-table scans because the tables have no index.

**The database file** sits on the FATFS partition, so a factory flash wipes it and replaces it with the bundled copy. See the [storage model](storage-model.md) for what each flash path touches.

**The current node.** The [storage model](storage-model.md) rule is that state goes in NVS through `badge.kv`, so it survives a reflash, and that rule points to storing `(bookID, nodeID)` there. The trap is that a factory flash replaces the database while NVS survives, so a saved node can point at a book that no longer exists. On open, the reader must check that the saved node exists in the database and fall back to the book picker if it does not. It goes in a dedicated `badge_treewalk` NVS namespace with `bookID` and `nodeID` keys, following the precedent of Helgrind's `badge_helgrind` namespace.

## Implementation

The reader is `firmware/src/screens/TreewalkScreen.{h,cpp}`, with all SQL in `TreewalkStore.{h,cpp}` next to it. `kScreenTreewalk` is registered in `firmware/src/ui/GUI.cpp`. The main menu has no Games submenu, so TREEWALK is a tile beside HELGRIND, GRENADA and PACKIT; that is the placement that shipped, not the submenu chosen in the interview. Scripts open it with `open treewalk`.

- **Modes.** A message (no database, or no books), the book picker, reading, and the X menu drawn over the page. The picker is stick up/down, Confirm opens a book at its root and clears the history, `X` returns to the page you came from at the same scroll line or, with no book active, exits. Book names word-wrap at 20 columns; the list scrolls by line and keeps the whole selected name in view.
- **Buttons.** The picker, the menu and the message use `confirmPressed`/`cancelPressed`, so they follow `swap_ok` (default 1, which makes B confirm and A cancel; see `Inputs::updateSemanticAliases`). The first build read raw `A` and ignored that setting. Choices stay on literal A, B and Y, as the brief says. Hints use the `Confirm` token, not a letter, per [UI conventions](ui-conventions.md).
- **Text.** The 6x10 font gives 20 body columns and 18 choice columns, which keeps clear of a one-pixel scroll bar at the right edge. Choices follow the body as lines carrying a button glyph from `ButtonGlyphs`; an ending shows `-- THE END --`. The stick scrolls one line per tick with `JoyRamp` repeat.
- **Menu.** Four items: Continue (closes the menu), Back, Books and Exit. It always opens on Continue. Back is dimmed and skipped when the history is empty and the node is a root. Confirm selects; `X` or Cancel closes it. Continue was added on 2026-09-30 after the first build had only Back, Books and Exit, so closing the menu was the only way to resume.
- **History.** `TreewalkScreen::history_` holds `{nodeId, scroll}` pairs, oldest first. A choice pushes only after the child loads; back pops only after the page loads, then clamps the scroll line to the page's length. Word wrap is deterministic, so the line offset restores the same view. The history caps at 64 entries and drops the oldest, so it stays at 512 bytes; the bundled books' paths are far shorter. It is cleared on open, on close and when a book is picked.
- **Errors.** A `childID` or `parentID` that does not resolve shows a two-second "Missing page" toast and stays on the current node.
- **Save.** Each node change writes `bookID` and `nodeID` to `badge_treewalk` through `Preferences`, as Helgrind does for `badge_helgrind`. On open the saved node must exist in the database, otherwise the picker shows. Only the resume-to-a-valid-node path has a script; the stale-save fallback has none yet.
- **Tests.** `host/scripts/treewalk-screen.txt` (picker, scroll, choice, menu, back to the scrolled line, back along a path through a second parent, ending, books and return to the scrolled line, exit, resume, back after resume, and wrapped choices) has fifteen goldens, loading the db through `treewalk-screen.args`. It opens *The Lantern Keeper*, second in the picker; back from Stairs returns to the root still scrolled to its last choice (the `treewalk-choices` golden), and Start, Cellar, Stairs, back lands on Cellar, not on Stairs' `parentID`. It then opens *The Case of the Kite-Day Cash* for labels that wrap over three lines: at the bottom of that page the first choice has scrolled off the top, and A still takes it. Six visible lines cannot show three three-line choices at once; buttons work by slot at any scroll position, so that is a reading cost, not a selection bug. A bigger `treewalk.db` slows the `--fs-dir` copy in `community-app` and shifts its starfield golden. Adding the tile shifted the home grid, so `matrix-screen` and `replay-text` now use `open matrix`, and `community-app` selects its tile with one step down.
- **Size.** `pio run -e replay2026` links (2026-10-28, with the back history): flash 2,901,362 bytes (71.4% of the app partition), static RAM 127,168 bytes; the history adds 384 bytes of flash and 16 of static RAM, plus up to 512 bytes of heap while a book is open. Heap use while a book is open is not measured.

## On the badge

The firmware was flashed with `pio run -e replay2026 -t upload` on 2026-09-30, with esptool verifying the write, and `treewalk.db` was copied to `/treewalk/` without `uploadfs`, using chunked raw-REPL writes because [`badge_sync push`](badge-sync.md) timed out on it; `badge_sync diff` then matched the manifest hash. On 2026-10-16 the firmware with the wrapping picker was flashed the same way and the two-book `treewalk.db` (70,656 bytes) went over with `badge_sync push` in 56 s; `diff` matched its hash. Nobody has yet run the app on the hardware, so the picker, scroll speed, button mapping and resume are checked only in the host harness.

## Open questions

- Measure heap while a book is open, and consider routing SQLite allocations to PSRAM.
- Read a long *Kite-Day Cash* passage on the real OLED to judge whether the soft limits (600 body, 40 label) are the right warnings; the harness shows only that long text wraps and scrolls.
- Add a script for the stale-save fallback (a saved node that is not in the database).
