"""Tests for the TREEWALK Twee-to-SQLite build tool."""

from __future__ import annotations

import importlib.util
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SCRIPTS_DIR = Path(__file__).resolve().parents[1]
BOOKS_DIR = SCRIPTS_DIR.parent / "treewalk" / "books"


def load_tool():
    """Load ``treewalk_build.py`` without adding its directory to ``sys.path``.

    :return: The loaded module.
    """
    path = SCRIPTS_DIR / "treewalk_build.py"
    spec = importlib.util.spec_from_file_location("treewalk_build", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module  # dataclasses look the module up by name
    spec.loader.exec_module(module)
    return module


tool = load_tool()

SMALL = """\
:: StoryTitle
Small Book

:: Start
You wake.

[[Go left->Left]]
[[Right|Right]]

:: Left
A wall.

:: Right
A door.

[[Open it->Left]]
"""


def parse(text: str, name: str = "book"):
    warnings: list[str] = []
    return tool.parse_book(text, name, warnings), warnings


class ParseTests(unittest.TestCase):
    def test_link_forms(self) -> None:
        cases = {
            "Go on|Next": ("Go on", "Next"),
            "Go on->Next": ("Go on", "Next"),
            "Next<-Go on": ("Go on", "Next"),
            "Next": ("Next", "Next"),
            "a->b->Next": ("a->b", "Next"),
        }
        for inner, (label, target) in cases.items():
            link = tool.parse_link(inner)
            self.assertEqual((link.label, link.target), (label, target), inner)

    def test_header_with_tags_and_position(self) -> None:
        name, tags = tool.parse_header(':: The Door [note big] {"position":"10,20"}')
        self.assertEqual((name, tags), ("The Door", ["note", "big"]))

    def test_book_shape(self) -> None:
        book, warnings = parse(SMALL)
        self.assertEqual(warnings, [])
        self.assertEqual(book.name, "Small Book")
        self.assertEqual([n.name for n in book.nodes], ["Start", "Left", "Right"])
        start, left, right = book.nodes
        self.assertIsNone(start.parent_id)
        self.assertEqual(left.parent_id, start.node_id)  # shortest path, not via Right
        self.assertEqual(right.parent_id, start.node_id)
        self.assertEqual([c.target for c in start.links], ["Left", "Right"])
        self.assertEqual(left.links, [])  # an ending

    def test_body_drops_link_lines_and_inlines_labels(self) -> None:
        book, _ = parse(":: Start\nYou see [[a door->Door]] here.\n[[Leave->Door]]\n\n:: Door\nDone.\n")
        start = book.nodes[0]
        self.assertEqual(start.body, "You see a door here.")
        self.assertEqual([c.label for c in start.links], ["a door", "Leave"])

    def test_title_falls_back_to_file_name(self) -> None:
        book, _ = parse(":: Start\nHi.\n", name="my-book")
        self.assertEqual(book.name, "my-book")

    def test_storydata_start(self) -> None:
        book, _ = parse(':: StoryData\n{"start": "Begin"}\n\n:: Begin\nHi.\n')
        self.assertEqual(book.nodes[0].name, "Begin")

    def test_typographic_characters_become_ascii(self) -> None:
        book, warnings = parse(":: Start\n\u2018Tis \u201cdark\u201d \u2013 it\u2019s \u2014 really\u2026\u00a0ok\n")
        self.assertEqual(book.nodes[0].body, '\'Tis "dark" - it\'s -- really... ok')
        self.assertEqual(warnings, [])

    def test_unmappable_characters_warn(self) -> None:
        _, warnings = parse(":: Start\nCaf\u00e9\n")
        self.assertEqual(len(warnings), 1)

    def test_soft_limits_warn_but_build(self) -> None:
        long_label = "x" * (tool.SOFT_LABEL_CHARS + 1)
        text = f":: Start\n{'y' * (tool.SOFT_BODY_CHARS + 1)}\n[[{long_label}->End]]\n\n:: End\nEnd.\n"
        book, warnings = parse(text)
        self.assertEqual(len(book.nodes), 2)
        self.assertEqual(len(warnings), 2)

    def test_script_and_stylesheet_passages_are_skipped(self) -> None:
        book, _ = parse(":: Start\nHi.\n\n:: Code [script]\nvar x;\n\n:: Look [stylesheet]\nbody {}\n")
        self.assertEqual([n.name for n in book.nodes], ["Start"])


class ValidationTests(unittest.TestCase):
    def assertRejected(self, text: str, fragment: str) -> None:
        with self.assertRaises(tool.TweeError) as ctx:
            parse(text)
        self.assertIn(fragment, str(ctx.exception))

    def test_missing_link_target(self) -> None:
        self.assertRejected(":: Start\nHi.\n[[Go->Nowhere]]\n", "Nowhere")

    def test_more_than_three_choices(self) -> None:
        links = "\n".join(f"[[Go {n}->End]]" for n in range(4))
        self.assertRejected(f":: Start\nHi.\n{links}\n\n:: End\nEnd.\n", "4 choices")

    def test_cycle(self) -> None:
        self.assertRejected(":: Start\nA.\n[[b->B]]\n\n:: B\nB.\n[[a->Start]]\n", "cycle")

    def test_self_link_is_a_cycle(self) -> None:
        self.assertRejected(":: Start\nA.\n[[again->Start]]\n", "cycle")

    def test_unreachable_passage(self) -> None:
        self.assertRejected(":: Start\nA.\n\n:: Orphan\nB.\n", "Orphan")

    def test_missing_start(self) -> None:
        self.assertRejected(":: Other\nA.\n", "start passage")

    def test_duplicate_passage(self) -> None:
        self.assertRejected(":: Start\nA.\n\n:: Start\nB.\n", "twice")

    def test_no_passages(self) -> None:
        self.assertRejected("nothing here\n", "no passages")

    def test_node_id_collision(self) -> None:
        with patch.object(tool, "stable_id", return_value=7):
            self.assertRejected(":: Start\nA.\n[[b->B]]\n\n:: B\nB.\n", "same node ID")


class IdTests(unittest.TestCase):
    def test_ids_are_stable_positive_31_bit(self) -> None:
        self.assertEqual(tool.stable_id("Start"), tool.stable_id("Start"))
        self.assertNotEqual(tool.stable_id("Start"), tool.stable_id("Left"))
        for text in ("", "a", "The Lantern Keeper", "x" * 500):
            self.assertTrue(0 < tool.stable_id(text) <= 0x7FFFFFFF, text)

    def test_ids_survive_reordering(self) -> None:
        first, _ = parse(SMALL)
        reordered = SMALL.replace(":: Left\nA wall.\n\n:: Right\nA door.\n\n[[Open it->Left]]\n",
                                  ":: Right\nA door.\n\n[[Open it->Left]]\n\n:: Left\nA wall.\n")
        self.assertNotEqual(reordered, SMALL)
        second, _ = parse(reordered)
        self.assertEqual({n.name: n.node_id for n in first.nodes}, {n.name: n.node_id for n in second.nodes})


class DatabaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = Path(self._tmp.name)

    def build(self, *texts: str, merge: bool = False, out: str = "t.db"):
        sources = []
        for index, text in enumerate(texts):
            src = self.tmp / f"book{index}.twee"
            src.write_text(text, encoding="utf-8")
            sources.append(src)
        return tool.build(sources, self.tmp / out, merge, lambda message: None)

    def query(self, sql: str, out: str = "t.db"):
        db = sqlite3.connect(self.tmp / out)
        try:
            return db.execute(sql).fetchall()
        finally:
            db.close()

    def test_tables_versions_and_no_index(self) -> None:
        self.build(SMALL)
        self.assertEqual(self.query("PRAGMA user_version"), [(tool.SCHEMA_VERSION,)])
        self.assertEqual(self.query("PRAGMA page_size"), [(tool.PAGE_SIZE,)])
        self.assertEqual(self.query("SELECT count(*) FROM sqlite_master WHERE type='index'"), [(0,)])
        self.assertEqual(self.query("SELECT name FROM book"), [("Small Book",)])
        self.assertEqual(self.query("SELECT count(*) FROM node"), [(3,)])
        self.assertEqual(self.query("SELECT seq, label FROM choice ORDER BY nodeID, seq LIMIT 0"), [])

    def test_choices_keep_link_order(self) -> None:
        self.build(SMALL)
        rows = self.query("SELECT seq, label FROM choice c JOIN node n USING(bookID, nodeID) "
                          "WHERE n.parentID IS NULL ORDER BY seq")
        self.assertEqual(rows, [(1, "Go left"), (2, "Right")])

    def test_build_is_byte_identical_across_runs(self) -> None:
        self.build(SMALL, out="a.db")
        self.build(SMALL, out="b.db")
        self.assertEqual((self.tmp / "a.db").read_bytes(), (self.tmp / "b.db").read_bytes())

    def test_merge_adds_and_replaces(self) -> None:
        other = SMALL.replace("Small Book", "Other Book")
        self.build(SMALL)
        self.build(other, merge=True)
        self.assertEqual(self.query("SELECT count(*) FROM book"), [(2,)])
        changed = SMALL.replace("A wall.", "A very long wall.")
        self.build(changed, merge=True)
        self.assertEqual(self.query("SELECT count(*) FROM book"), [(2,)])
        self.assertEqual(self.query("SELECT count(*) FROM node"), [(6,)])
        self.assertEqual(self.query("SELECT count(*) FROM node WHERE body = 'A very long wall.'"), [(1,)])
        self.assertEqual(tool.check_database(self.tmp / "t.db"), [])

    def test_replacing_a_book_leaves_no_free_pages(self) -> None:
        self.build(SMALL)
        big = SMALL.replace("A wall.", "A wall. " + "It goes on. " * 300)
        self.build(big, merge=True)
        self.build(SMALL, merge=True)
        self.assertEqual(self.query("PRAGMA freelist_count"), [(0,)])

    def test_merge_rejects_wrong_schema_version(self) -> None:
        db = sqlite3.connect(self.tmp / "old.db")
        db.execute("PRAGMA user_version = 99")
        db.commit()
        db.close()
        with self.assertRaises(tool.TweeError):
            self.build(SMALL, merge=True, out="old.db")

    def test_book_id_collision_between_names(self) -> None:
        other = SMALL.replace("Small Book", "Other Book")
        real = tool.stable_id
        with patch.object(tool, "stable_id", side_effect=lambda text: 5 if "Book" in text else real(text)):
            with self.assertRaises(tool.TweeError) as ctx:
                self.build(SMALL, other)
        self.assertIn("same book ID", str(ctx.exception))

    def test_check_finds_damage(self) -> None:
        self.build(SMALL)
        path = self.tmp / "t.db"
        cases = {
            "missing child": "UPDATE choice SET childID = 1 WHERE seq = 1",
            "two roots": "UPDATE node SET parentID = NULL WHERE parentID IS NOT NULL",
            "gap in seq": "UPDATE choice SET seq = 3 WHERE seq = 2",
            "index": "CREATE INDEX i ON node(nodeID)",
        }
        for label, sql in cases.items():
            copy = self.tmp / "copy.db"
            copy.write_bytes(path.read_bytes())
            db = sqlite3.connect(copy)
            db.execute(sql)
            db.commit()
            db.close()
            self.assertTrue(tool.check_database(copy), label)

    def test_check_finds_a_cycle_in_a_database(self) -> None:
        self.build(SMALL)
        db = sqlite3.connect(self.tmp / "t.db")
        db.execute("INSERT INTO choice SELECT bookID, nodeID, 2, 'back', "
                   "(SELECT nodeID FROM node WHERE parentID IS NULL) FROM node WHERE parentID IS NOT NULL "
                   "AND nodeID NOT IN (SELECT nodeID FROM choice) LIMIT 1")
        db.commit()
        db.close()
        problems = tool.check_database(self.tmp / "t.db")
        self.assertTrue(any("seq" in p or "cycle" in p or "choice numbers" in p for p in problems), problems)

    def test_missing_file(self) -> None:
        self.assertTrue(tool.check_database(self.tmp / "nope.db"))


class BundledBookTests(unittest.TestCase):
    def test_lantern_keeper_builds_and_is_a_dag_with_a_merge(self) -> None:
        source = BOOKS_DIR / "lantern-keeper.twee"
        book, warnings = parse(source.read_text(encoding="utf-8"), source.stem)
        self.assertEqual(warnings, [])
        self.assertEqual(book.name, "The Lantern Keeper")
        self.assertTrue(8 <= len(book.nodes) <= 14)
        by_id = {n.node_id: n for n in book.nodes}
        endings = [n for n in book.nodes if not n.links]
        self.assertGreaterEqual(len(endings), 3)
        inbound: dict[str, int] = {}
        for node in book.nodes:
            for link in node.links:
                inbound[link.target] = inbound.get(link.target, 0) + 1
        self.assertTrue(any(count > 1 for count in inbound.values()), "no shared child, so no DAG merge")
        self.assertEqual(len(by_id), len(book.nodes))
        for node in book.nodes:
            self.assertLessEqual(len(node.links), 3)


class CommittedDatabaseTests(unittest.TestCase):
    def test_committed_db_matches_twee_sources(self) -> None:
        committed = SCRIPTS_DIR.parent / "initial_filesystem" / "treewalk" / "treewalk.db"
        sources = sorted(BOOKS_DIR.glob("*.twee"))
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "treewalk.db"
            tool.build(sources, out, False, lambda message: None)
            self.assertEqual(
                committed.read_bytes(),
                out.read_bytes(),
                "treewalk.db is stale: run scripts/treewalk_build.py build -o "
                "initial_filesystem/treewalk/treewalk.db treewalk/books/*.twee, "
                "then TEMPORAL_BADGE_REGEN_MANIFEST=1 python3 scripts/generate_startup_files.py",
            )


if __name__ == "__main__":
    unittest.main()
