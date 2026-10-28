#!/usr/bin/env python3
"""Compile Twee stories into the TREEWALK SQLite database, or check one.

TREEWALK reads a Choose-Your-Own-Adventure story as a DAG of nodes from a
read-only SQLite file (see wiki/components/treewalk.md). Authors write books in
Twee 3; this tool turns them into the three tables the badge reads:

    book   (bookID, name)
    node   (bookID, nodeID, parentID, body)
    choice (bookID, nodeID, seq, label, childID)

The tables have no PRIMARY KEY or UNIQUE constraint on purpose, so SQLite builds
no index for them; every lookup on the badge is a full scan.

Usage:
    treewalk_build.py build -o treewalk.db book.twee [book2.twee ...]
    treewalk_build.py build -o treewalk.db --merge extra.twee
    treewalk_build.py check treewalk.db

How a book is read:
  * ``StoryTitle`` names the book; without it the file name is the name.
  * The start passage is ``StoryData``'s ``start`` key, else a passage named
    ``Start``. It becomes the root (``parentID`` NULL).
  * Links ``[[label|target]]``, ``[[label->target]]``, ``[[target<-label]]`` and
    ``[[target]]`` become the passage's choices, in order. At most three.
  * A line holding only links is dropped from the body; a link inside a line is
    replaced by its label.
  * A passage with no links is an ending.
  * Every passage must be reachable from the start, links must resolve, and the
    links must not form a cycle.

IDs are stable so a saved node still means the same passage after a rebuild:
``bookID`` is a hash of the book name and ``nodeID`` a hash of the passage name
(both CRC-32, masked to 31 bits). ``parentID`` is the first parent found walking
breadth-first from the start, so it is the parent on a shortest path.
"""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
import sys
import zlib
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Tuple

SCHEMA_VERSION = 1
PAGE_SIZE = 1024
MAX_CHOICES = 3
# Soft limits: longer text still builds, with a warning (see the wiki page).
SOFT_BODY_CHARS = 600
SOFT_LABEL_CHARS = 40

SCHEMA_SQL = """
CREATE TABLE book   (bookID INTEGER, name TEXT);
CREATE TABLE node   (bookID INTEGER, nodeID INTEGER, parentID INTEGER, body TEXT);
CREATE TABLE choice (bookID INTEGER, nodeID INTEGER, seq INTEGER, label TEXT, childID INTEGER);
"""

# Typographic characters authors paste in, mapped to what the badge font can draw.
ASCII_MAP = {
    "\u2018": "'", "\u2019": "'", "\u201c": '"', "\u201d": '"',
    "\u2013": "-", "\u2014": "--", "\u2026": "...", "\u00a0": " ",
}

_LINK_RE = re.compile(r"\[\[(.+?)\]\]")
_HEADER_RE = re.compile(r"^::\s*(?P<rest>.*)$")


class TweeError(Exception):
    """A book that cannot be compiled, or a database that fails its check."""


@dataclass
class Choice:
    label: str
    target: str  # passage name while parsing; resolved to an ID in Node.choices


@dataclass
class Node:
    name: str
    node_id: int = 0
    parent_id: Optional[int] = None
    body: str = ""
    links: List[Choice] = field(default_factory=list)


@dataclass
class Book:
    name: str
    book_id: int
    nodes: List[Node]  # start first, then document order


def stable_id(text: str) -> int:
    """A stable positive 31-bit ID for ``text`` (never 0, which reads as unset)."""
    value = zlib.crc32(text.encode("utf-8")) & 0x7FFFFFFF
    return value or 1


# ── Parsing ─────────────────────────────────────────────────────────────────


def _unescape(name: str) -> str:
    return re.sub(r"\\(.)", r"\1", name)


def parse_header(line: str) -> Tuple[str, List[str]]:
    """Split a ``:: Name [tags] {meta}`` line into the passage name and its tags."""
    rest = _HEADER_RE.match(line).group("rest").strip()  # type: ignore[union-attr]
    rest = re.sub(r"\s*\{[^{}]*\}\s*$", "", rest)  # position metadata
    tags: List[str] = []
    match = re.search(r"(?<!\\)\[([^\[\]]*)\]\s*$", rest)
    if match:
        tags = match.group(1).split()
        rest = rest[: match.start()].strip()
    return _unescape(rest), tags


def parse_link(inner: str) -> Choice:
    """Read the inside of ``[[...]]`` in any of the Twee link forms."""
    if "|" in inner:
        label, target = inner.split("|", 1)
    elif "->" in inner:
        label, target = inner.rsplit("->", 1)
    elif "<-" in inner:
        target, label = inner.split("<-", 1)
    else:
        label = target = inner
    return Choice(label.strip(), target.strip())


def split_passages(text: str) -> List[Tuple[str, List[str], str]]:
    """Split Twee source into ``(name, tags, body)`` passages."""
    passages: List[Tuple[str, List[str], str]] = []
    current: Optional[Tuple[str, List[str], List[str]]] = None
    for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        if line.startswith("::"):
            if current:
                passages.append((current[0], current[1], "\n".join(current[2])))
            name, tags = parse_header(line)
            current = (name, tags, [])
        elif current:
            current[2].append(line)
    if current:
        passages.append((current[0], current[1], "\n".join(current[2])))
    return passages


def clean_body(raw: str, warnings: List[str], where: str) -> Tuple[str, List[Choice]]:
    """Return the passage text with links removed, and the links found.

    Lines that hold only links are dropped; a link inside a line becomes its label.
    """
    links: List[Choice] = []
    kept: List[str] = []
    for line in raw.split("\n"):
        found = [parse_link(m.group(1)) for m in _LINK_RE.finditer(line)]
        links.extend(found)
        if found and not _LINK_RE.sub("", line).strip():
            continue
        kept.append(_LINK_RE.sub(lambda m: parse_link(m.group(1)).label, line).rstrip())
    body = "\n".join(kept).strip()
    body = re.sub(r"\n{3,}", "\n\n", body)
    for src, dst in ASCII_MAP.items():
        body = body.replace(src, dst)
    for choice in links:
        for src, dst in ASCII_MAP.items():
            choice.label = choice.label.replace(src, dst)
    odd = sorted({c for c in body + "".join(c.label for c in links) if ord(c) > 126 or (ord(c) < 32 and c != "\n")})
    if odd:
        warnings.append(f"{where}: characters the badge font may not draw: {''.join(odd)!r}")
    return body, links


def parse_book(text: str, default_name: str, warnings: Optional[List[str]] = None) -> Book:
    """Compile one Twee source into a validated ``Book``.

    :param text: Twee 3 source.
    :param default_name: Book name to use when there is no ``StoryTitle`` passage.
    :param warnings: Collects non-fatal problems (long text, characters the font lacks).
    :raises TweeError: On any problem that would make the book unreadable on the badge.
    """
    warnings = warnings if warnings is not None else []
    title = default_name
    start_name = "Start"
    nodes: Dict[str, Node] = {}

    for name, tags, body in split_passages(text):
        if name == "StoryTitle":
            title = body.strip() or default_name
            continue
        if name == "StoryData":
            try:
                start_name = json.loads(body).get("start", start_name)
            except (ValueError, AttributeError) as err:
                raise TweeError(f"StoryData is not valid JSON: {err}")
            continue
        if {"script", "stylesheet"} & set(tags) or name in ("StoryInit",):
            continue
        if not name:
            raise TweeError("a passage has no name")
        if name in nodes:
            raise TweeError(f"passage {name!r} is defined twice")
        text_body, links = clean_body(body, warnings, f"passage {name!r}")
        nodes[name] = Node(name=name, body=text_body, links=links)

    if not nodes:
        raise TweeError("the book has no passages")
    if start_name not in nodes:
        raise TweeError(f"the start passage {start_name!r} does not exist")

    for node in nodes.values():
        if len(node.links) > MAX_CHOICES:
            raise TweeError(f"passage {node.name!r} has {len(node.links)} choices; the limit is {MAX_CHOICES}")
        for link in node.links:
            if link.target not in nodes:
                raise TweeError(f"passage {node.name!r} links to {link.target!r}, which does not exist")
        if len(node.body) > SOFT_BODY_CHARS:
            warnings.append(f"passage {node.name!r}: body is {len(node.body)} characters (soft limit {SOFT_BODY_CHARS})")
        for link in node.links:
            if len(link.label) > SOFT_LABEL_CHARS:
                warnings.append(f"passage {node.name!r}: choice {link.label!r} is {len(link.label)} characters (soft limit {SOFT_LABEL_CHARS})")
        if not node.body:
            warnings.append(f"passage {node.name!r}: the body is empty")

    _assign_ids(nodes)
    _find_cycle(nodes, start_name)
    order = _assign_parents(nodes, start_name)
    return Book(name=title, book_id=stable_id(title), nodes=order)


def _assign_ids(nodes: Dict[str, Node]) -> None:
    seen: Dict[int, str] = {}
    for node in nodes.values():
        node.node_id = stable_id(node.name)
        if node.node_id in seen:
            raise TweeError(f"passages {seen[node.node_id]!r} and {node.name!r} hash to the same node ID; rename one")
        seen[node.node_id] = node.name


def _find_cycle(nodes: Dict[str, Node], start: str) -> None:
    """Reject links that lead back to a passage already on the path."""
    state: Dict[str, int] = {}  # 1 on the current path, 2 finished
    path: List[str] = []

    def visit(name: str) -> None:
        state[name] = 1
        path.append(name)
        for link in nodes[name].links:
            if state.get(link.target) == 1:
                loop = path[path.index(link.target):] + [link.target]
                raise TweeError("the story has a cycle: " + " -> ".join(loop))
            if link.target not in state:
                visit(link.target)
        path.pop()
        state[name] = 2

    # Iterative depth would avoid the limit, but stories are small and the
    # 1000-frame default is far above any book that fits the badge.
    visit(start)


def _assign_parents(nodes: Dict[str, Node], start: str) -> List[Node]:
    """Set ``parent_id`` breadth-first from the start; reject unreachable passages."""
    root = nodes[start]
    root.parent_id = None
    reached = {start}
    queue = deque([root])
    while queue:
        node = queue.popleft()
        for link in node.links:
            child = nodes[link.target]
            if child.name not in reached:
                reached.add(child.name)
                child.parent_id = node.node_id
                queue.append(child)
    missing = [n for n in nodes if n not in reached]
    if missing:
        raise TweeError("passages not reachable from the start: " + ", ".join(repr(n) for n in missing))
    return [root] + [n for n in nodes.values() if n is not root]


# ── Database ────────────────────────────────────────────────────────────────


def _fresh_database(path: Path) -> sqlite3.Connection:
    if path.exists():
        path.unlink()
    db = sqlite3.connect(path)
    db.execute(f"PRAGMA page_size = {PAGE_SIZE}")
    db.executescript(SCHEMA_SQL)
    db.execute(f"PRAGMA user_version = {SCHEMA_VERSION}")
    return db


def _open_for_merge(path: Path) -> sqlite3.Connection:
    db = sqlite3.connect(path)
    version = db.execute("PRAGMA user_version").fetchone()[0]
    if version != SCHEMA_VERSION:
        db.close()
        raise TweeError(f"{path} has schema version {version}; this tool writes version {SCHEMA_VERSION}")
    return db


def write_books(path: Path, books: Sequence[Book], merge: bool = False) -> None:
    """Write ``books`` to ``path``.

    A book already in the file (same ID) is replaced. With ``merge`` false the file
    is created fresh; with it true the file must already exist.

    :raises TweeError: On a schema mismatch, or two different book names sharing an ID.
    """
    ids: Dict[int, str] = {}
    for book in books:
        if book.book_id in ids and ids[book.book_id] != book.name:
            raise TweeError(f"books {ids[book.book_id]!r} and {book.name!r} hash to the same book ID; rename one")
        ids[book.book_id] = book.name

    if merge and path.exists():
        db = _open_for_merge(path)
    else:
        db = _fresh_database(path)
    try:
        for book_id, name in ids.items():
            existing = db.execute("SELECT name FROM book WHERE bookID = ?", (book_id,)).fetchone()
            if existing and existing[0] != name:
                raise TweeError(f"book {name!r} has the same ID as {existing[0]!r} already in {path}; rename one")
        for book in books:
            for table in ("choice", "node", "book"):
                db.execute(f"DELETE FROM {table} WHERE bookID = ?", (book.book_id,))
            db.execute("INSERT INTO book VALUES (?, ?)", (book.book_id, book.name))
            for node in book.nodes:
                db.execute(
                    "INSERT INTO node VALUES (?, ?, ?, ?)",
                    (book.book_id, node.node_id, node.parent_id, node.body),
                )
            by_name = {n.name: n for n in book.nodes}
            for node in book.nodes:
                for seq, link in enumerate(node.links, start=1):
                    db.execute(
                        "INSERT INTO choice VALUES (?, ?, ?, ?, ?)",
                        (book.book_id, node.node_id, seq, link.label, by_name[link.target].node_id),
                    )
        db.commit()
        db.execute("VACUUM")  # drop free pages left by a replaced book
    finally:
        db.close()


def check_database(path: Path) -> List[str]:
    """Return the problems found in a TREEWALK database (empty when it is sound)."""
    if not path.exists():
        return [f"{path} does not exist"]
    problems: List[str] = []
    db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    try:
        version = db.execute("PRAGMA user_version").fetchone()[0]
        if version != SCHEMA_VERSION:
            return [f"schema version is {version}, expected {SCHEMA_VERSION}"]
        if db.execute("PRAGMA journal_mode").fetchone()[0].lower() == "wal":
            problems.append("the database is in WAL mode; the badge VFS cannot read it")
        if db.execute("SELECT count(*) FROM sqlite_master WHERE type = 'index'").fetchone()[0]:
            problems.append("the database has an index; TREEWALK's tables are meant to have none")
        for book_id, name in db.execute("SELECT bookID, name FROM book"):
            problems.extend(_check_book(db, book_id, name))
    except sqlite3.DatabaseError as err:
        problems.append(f"not a readable database: {err}")
    finally:
        db.close()
    return problems


def _check_book(db: sqlite3.Connection, book_id: int, name: str) -> List[str]:
    where = f"book {name!r}"
    problems: List[str] = []
    parents = {n: p for n, p in db.execute("SELECT nodeID, parentID FROM node WHERE bookID = ?", (book_id,))}
    choices: Dict[int, List[Tuple[int, int]]] = {n: [] for n in parents}
    for node_id, seq, child in db.execute(
        "SELECT nodeID, seq, childID FROM choice WHERE bookID = ? ORDER BY nodeID, seq", (book_id,)
    ):
        if node_id not in parents:
            problems.append(f"{where}: a choice belongs to missing node {node_id}")
            continue
        choices[node_id].append((seq, child))
    roots = [n for n, p in parents.items() if p is None]
    if len(roots) != 1:
        problems.append(f"{where}: expected one root node, found {len(roots)}")
    for node_id, parent in parents.items():
        if parent is not None and parent not in parents:
            problems.append(f"{where}: node {node_id} has missing parent {parent}")
        found = choices[node_id]
        if len(found) > MAX_CHOICES:
            problems.append(f"{where}: node {node_id} has {len(found)} choices")
        if [s for s, _ in found] != list(range(1, len(found) + 1)):
            problems.append(f"{where}: node {node_id} has choice numbers {[s for s, _ in found]}, expected 1..{len(found)}")
        for _, child in found:
            if child not in parents:
                problems.append(f"{where}: node {node_id} has a choice to missing node {child}")
    if len(roots) == 1 and not problems:
        problems.extend(_check_graph(where, roots[0], choices))
    return problems


def _check_graph(where: str, root: int, choices: Dict[int, List[Tuple[int, int]]]) -> List[str]:
    seen = {root}
    stack = [root]
    while stack:
        for _, child in choices[stack.pop()]:
            if child not in seen:
                seen.add(child)
                stack.append(child)
    problems: List[str] = []
    if len(seen) != len(choices):
        problems.append(f"{where}: {len(choices) - len(seen)} nodes are not reachable from the root")
    state: Dict[int, int] = {}

    def cyclic(node: int) -> bool:
        state[node] = 1
        for _, child in choices[node]:
            if state.get(child) == 1 or (child not in state and cyclic(child)):
                return True
        state[node] = 2
        return False

    if cyclic(root):
        problems.append(f"{where}: the choices contain a cycle")
    return problems


# ── Command line ────────────────────────────────────────────────────────────


def build(inputs: Sequence[Path], output: Path, merge: bool, warn: Callable[[str], None]) -> List[Book]:
    """Compile ``inputs`` into ``output`` and check the result."""
    books: List[Book] = []
    for src in inputs:
        warnings: List[str] = []
        try:
            books.append(parse_book(src.read_text(encoding="utf-8"), src.stem, warnings))
        except TweeError as err:
            raise TweeError(f"{src}: {err}")
        for message in warnings:
            warn(f"{src}: warning: {message}")
    write_books(output, books, merge)
    problems = check_database(output)
    if problems:
        raise TweeError("the database failed its own check:\n  " + "\n  ".join(problems))
    return books


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p_build = sub.add_parser("build", help="compile Twee books into a database")
    p_build.add_argument("-o", "--output", required=True, type=Path)
    p_build.add_argument("--merge", action="store_true", help="add to an existing database, replacing books with the same name")
    p_build.add_argument("books", nargs="+", type=Path)
    p_check = sub.add_parser("check", help="check a database")
    p_check.add_argument("database", type=Path)
    args = parser.parse_args(argv)

    try:
        if args.command == "build":
            books = build(args.books, args.output, args.merge, lambda m: print(m, file=sys.stderr))
            for book in books:
                print(f"{args.output}: {book.name!r} (bookID {book.book_id}): {len(book.nodes)} nodes")
            return 0
        problems = check_database(args.database)
        for problem in problems:
            print(f"{args.database}: {problem}", file=sys.stderr)
        if not problems:
            print(f"{args.database}: ok")
        return 1 if problems else 0
    except TweeError as err:
        print(f"error: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
