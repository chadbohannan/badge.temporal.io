"""Tests for badge_sync's chunked, resumable push.

The badge-side scripts run in CPython against an in-memory filesystem, with
``os``, ``ubinascii`` and ``open`` swapped for fakes, so the tests cover the
scripts the badge executes as well as the host's retry logic.
"""

from __future__ import annotations

import base64
import builtins
import importlib.util
import io
import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parents[1]


def load_tool():
    """Load ``badge_sync.py`` without adding its directory to ``sys.path``.

    :return: The loaded module.
    """
    path = SCRIPTS_DIR / "badge_sync.py"
    spec = importlib.util.spec_from_file_location("badge_sync", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module  # dataclasses look the module up by name
    spec.loader.exec_module(module)
    return module


tool = load_tool()


class FakeFatfs:
    """An in-memory FATFS: files, directories, and a rename that refuses to
    overwrite, as FATFS does."""

    def __init__(self):
        self.files: dict[str, bytearray] = {}
        self.dirs = {"/"}

    def stat(self, p):
        if p in self.files:
            return (0x8000, 0, 0, 0, 0, 0, len(self.files[p]), 0, 0, 0)
        if p in self.dirs:
            return (0x4000, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        raise OSError(2)

    def remove(self, p):
        if p not in self.files:
            raise OSError(2)
        del self.files[p]

    def mkdir(self, p):
        if p in self.dirs or p in self.files:
            raise OSError(17)
        if (p.rsplit("/", 1)[0] or "/") not in self.dirs:
            raise OSError(2)
        self.dirs.add(p)

    def rename(self, a, b):
        if b in self.files or a not in self.files:
            raise OSError(17)
        self.files[b] = self.files.pop(a)

    def open(self, p, mode="r"):
        fs = self

        class Handle(io.BytesIO):
            def close(self):
                if "r" not in mode and not self.closed:
                    fs.files[p] = bytearray(self.getvalue())
                super().close()

        if "r" in mode:
            if p not in self.files:
                raise OSError(2)
            return Handle(bytes(self.files[p]))
        if (p.rsplit("/", 1)[0] or "/") not in self.dirs:
            raise OSError(2)
        h = Handle(bytes(self.files.get(p, b"")) if "a" in mode else b"")
        h.seek(0, io.SEEK_END)
        return h


class FakeRepl:
    """Runs each script against a FakeFatfs. ``stalls`` maps an exec number
    to "before" (the script never ran) or "after" (it ran, the reply was
    lost); either raises TimeoutError, as a stalled badge does."""

    def __init__(self, fs: FakeFatfs, stalls=None, on_exec=None):
        self.fs = fs
        self.stalls = dict(stalls or {})
        self.on_exec = on_exec
        self.execs = 0
        self.enters = 0

    def enter(self):
        self.enters += 1

    def exec_(self, script, timeout=None):
        n = self.execs
        self.execs += 1
        stall = self.stalls.pop(n, None)
        if stall == "before":
            raise TimeoutError("stalled before running")
        out = io.StringIO()
        fake_os = type("os", (), {
            "stat": staticmethod(self.fs.stat),
            "remove": staticmethod(self.fs.remove),
            "mkdir": staticmethod(self.fs.mkdir),
            "rename": staticmethod(self.fs.rename),
        })
        fake_ubinascii = type("ubinascii", (), {
            "a2b_base64": staticmethod(base64.b64decode)})
        modules = {"os": fake_os, "ubinascii": fake_ubinascii}
        env = dict(vars(builtins))
        env["__import__"] = lambda name, *a, **k: modules[name]
        env["open"] = self.fs.open
        env["print"] = lambda *a: out.write(" ".join(map(str, a)) + "\n")
        try:
            exec(script, {"__builtins__": env})
        except Exception as e:
            raise RuntimeError(f"badge raised:\n{e!r}") from e
        if self.on_exec:
            self.on_exec(n)
        if stall == "after":
            raise TimeoutError("reply lost")
        return out.getvalue().encode()


DATA = bytes(range(256)) * 4 + b"tail"  # 1028 bytes: five full chunks and a short one


class PushTests(unittest.TestCase):
    def push(self, repl, path="/treewalk/treewalk.db", data=DATA):
        tool._push_via(repl, path, data)
        return repl.fs.files.get(path)

    def test_writes_file_and_creates_parents(self):
        fs = FakeFatfs()
        repl = FakeRepl(fs)
        self.assertEqual(self.push(repl, "/a/b/c.bin"), DATA)
        self.assertEqual(fs.dirs, {"/", "/a", "/a/b"})
        self.assertNotIn("/a/b/c.bin.tmp", fs.files)
        # begin, six chunks, finish
        self.assertEqual(repl.execs, 8)

    def test_replaces_existing_file(self):
        fs = FakeFatfs()
        fs.dirs.add("/treewalk")
        fs.files["/treewalk/treewalk.db"] = bytearray(b"old")
        fs.files["/treewalk/treewalk.db.tmp"] = bytearray(b"leftover")
        self.assertEqual(self.push(FakeRepl(fs)), DATA)

    def test_empty_file(self):
        self.assertEqual(self.push(FakeRepl(FakeFatfs()), data=b""), b"")

    def test_stall_before_chunk_resends_it(self):
        repl = FakeRepl(FakeFatfs(), stalls={3: "before"})
        self.assertEqual(self.push(repl), DATA)
        self.assertEqual(repl.enters, 1)

    def test_lost_reply_does_not_duplicate_chunk(self):
        repl = FakeRepl(FakeFatfs(), stalls={3: "after"})
        self.assertEqual(self.push(repl), DATA)

    def test_stall_during_begin_restarts(self):
        repl = FakeRepl(FakeFatfs(), stalls={0: "before"})
        self.assertEqual(self.push(repl), DATA)

    def test_stall_during_resync_is_retried(self):
        # exec 2 stalls; exec 3 (the size query) stalls too.
        repl = FakeRepl(FakeFatfs(), stalls={2: "after", 3: "before"})
        self.assertEqual(self.push(repl), DATA)
        self.assertEqual(repl.enters, 2)

    def test_gives_up_after_repeated_stalls(self):
        stalls = {n: "before" for n in range(2, 2 + 2 * (tool.PUSH_RETRIES + 1))}
        repl = FakeRepl(FakeFatfs(), stalls=stalls)
        with self.assertRaisesRegex(TimeoutError, "stalls in a row"):
            self.push(repl)

    def test_progress_resets_stall_count(self):
        # A stall on every other chunk never hits the limit.
        stalls = {n: "after" for n in range(1, 40, 3)}
        self.assertEqual(self.push(FakeRepl(FakeFatfs(), stalls=stalls)), DATA)

    def test_corrupt_tmp_fails_hash_check_and_keeps_old_file(self):
        fs = FakeFatfs()
        fs.dirs.add("/treewalk")
        fs.files["/treewalk/treewalk.db"] = bytearray(b"old")

        def corrupt(n):
            if n == 2:
                fs.files["/treewalk/treewalk.db.tmp"][0] ^= 0xFF

        with self.assertRaisesRegex(RuntimeError, "hash"):
            self.push(FakeRepl(fs, on_exec=corrupt))
        self.assertEqual(fs.files["/treewalk/treewalk.db"], b"old")

    def test_rejects_bad_path(self):
        with self.assertRaises(ValueError):
            self.push(FakeRepl(FakeFatfs()), "relative.db")

    def test_fnv_matches_manifest(self):
        db = SCRIPTS_DIR.parent / "initial_filesystem" / "treewalk" / "treewalk.db"
        manifest = tool.load_manifest(SCRIPTS_DIR.parent / "initial_filesystem")
        self.assertEqual(tool._fnv1a32(db.read_bytes()),
                         manifest["/treewalk/treewalk.db"].fnv1a32)


class FakeSerial:
    """A port holding one canned reply. ``read(n)`` for more than is waiting
    fails, because a real port would block until the timeout."""

    def __init__(self, reply: bytes):
        self.buf = bytearray(reply)

    @property
    def in_waiting(self):
        return len(self.buf)

    def write(self, data):
        pass

    def read(self, n=1):
        if n > max(1, len(self.buf)):
            raise AssertionError(f"read({n}) with {len(self.buf)} waiting would block")
        out = bytes(self.buf[:n])
        del self.buf[:n]
        return out


class ExecStreamingTests(unittest.TestCase):
    def repl(self, reply: bytes):
        repl = tool.RawRepl.__new__(tool.RawRepl)
        repl.ser = FakeSerial(reply)
        repl.timeout = 1.0
        return repl

    def test_short_reply_reads_only_what_is_waiting(self):
        self.assertEqual(self.repl(b"OK\x04\x04>").exec_("pass"), b"")

    def test_output_lines(self):
        self.assertEqual(self.repl(b"OKSZ|42\r\n\x04\x04>").exec_("x"), b"SZ|42\r\n")

    def test_badge_error_raises(self):
        with self.assertRaisesRegex(RuntimeError, "OSError"):
            self.repl(b"OK\x04Traceback: OSError\x04>").exec_("x")


if __name__ == "__main__":
    unittest.main()
