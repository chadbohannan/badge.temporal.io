# SQLite on the Badge

The firmware carries SQLite 3.53.4 as a read-only database engine, first for the planned [TREEWALK](treewalk.md) story reader. It is built and tested on the host as of 2026-09-30. The device build compiles it, but no badge has run it and nothing on the device links it yet. This page owns the mutable facts: the version, the build flags, the VFS design and the test setup. Other pages link here.

## Where the pieces are

- `firmware/lib/sqlite3/` holds the vendored amalgamation (`src/sqlite3.c`, `src/sqlite3.h`), `library.json` with the build flags, `src/sqlite3_noassert.h`, and a README with the upgrade steps.
- `firmware/src/infra/SqliteVfs.{h,cpp}` is the VFS core. `SqliteVfsFat.cpp` is the FATFS backend.
- `firmware/host/sqlite/` is the standalone test. `firmware/host/harness/sqlite_check.cpp` is the harness check.

Ownership of upgrades is simple: replace the two amalgamation files, then update the version in the lib README, `library.json` and `THIRD_PARTY_NOTICES.md`. Download from sqlite.org and verify the hash: its download page lists **SHA3-256**, not SHA-256, so `sha256sum -c` reports a false failure on a good file (2026-09-30: `sha3_256` of `sqlite-amalgamation-3530400.zip` matched, `sha256sum` did not). SQLite is public domain.

## Configuration

`library.json` selects: `SQLITE_OS_OTHER=1` (no built-in OS layer), `SQLITE_THREADSAFE=0`, `SQLITE_TEMP_STORE=3` (temp tables in memory), a 64 KiB default cache (`SQLITE_DEFAULT_CACHE_SIZE=-64`), no mmap, `SQLITE_DQS=0`, and omits for extension loading, WAL, deprecated APIs, shared cache, authorization, tracing and progress callbacks. Consequences worth knowing:

- Single-threaded means one task at a time may use a connection. The reader is the only user.
- With `SQLITE_OS_OTHER`, SQLite calls `sqlite3_os_init()` and `sqlite3_os_end()` itself from `sqlite3_initialize()`; the firmware defines them in `SqliteVfs.cpp`, and `os_init` registers the VFS as the default. There is no separate registration call.
- The same flags appear in `firmware/host/sqlite/Makefile`, which builds `sqlite3.c` without PlatformIO. Nothing enforces that the two lists match, so a change to one needs the other.

## The read-only VFS

The core implements SQLite's `sqlite3_vfs` and `sqlite3_io_methods` and knows nothing about files. It calls a `SqliteFileOps` table (`open`, `read`, `size`, `close`, `exists`, plus `handleSize`) that a backend supplies through `sqliteFileOps()`. `handleSize` bytes of backend state sit directly after the `sqlite3_file` header inside SQLite's own allocation, so opening a file allocates nothing extra. Two backends exist: FATFS on the badge and the harness, and POSIX in the unit test.

The VFS is read-only by design. Story data never changes on the badge and the reader's one piece of state goes to NVS, so `xOpen` accepts only the main database opened read-only, and write, truncate and delete return `SQLITE_READONLY`. That removes journals, temp files and any lock protocol: locking is a no-op, and the file reports `SQLITE_IOCAP_IMMUTABLE`. It also removes the risk described on the [storage model](storage-model.md) page, where concurrent FATFS writers corrupted directory entries because oofatfs is built without reentrancy. Every FATFS call in the backend still runs under `Filesystem::IOLock`, as any direct `f_*` caller must.

`xRead` must zero the unread tail and return `SQLITE_IOERR_SHORT_READ` on a short read; SQLite relies on that for a database whose file is shorter than its header claims.

## Full scans, on purpose

TREEWALK's tables declare no `PRIMARY KEY` or `UNIQUE`, because SQLite creates an automatic index for each of those, and the brief said no indexes. `EXPLAIN QUERY PLAN` on `WHERE nodeID = 3` reports `SCAN node`, and the unit test asserts both that the file has no index and that the plan is a scan. For a story of a few hundred nodes that is fast enough; a large book would show up as slow node loads on the badge, and the fix would be a one-line `CREATE INDEX` in the build tool.

## Tests

`make test` in `firmware/host/sqlite/` builds the VFS core over a POSIX backend with ASan and UBSan and runs against `build/fixture/fixture.db`, which `make_fixture.py` writes with Python's `sqlite3` module (the badge VFS cannot create a file). It checks row reads and ordering, an ending (a node with no choices), a scan of 5,000 wide rows across many pages, refused writes, a missing file (`SQLITE_CANTOPEN`), `READWRITE` and `CREATE` opens (`CANTOPEN`), a truncated copy, an empty file, and the raw VFS contract for short reads.

`host/run-tests.sh` runs that test as `sqlite-vfs`, then loads the same fixture onto the [host harness](../systems/host-test-harness.md)'s real FAT volume and runs `--sqlite-check` as `sqlite-fat`, which is the only test of the FATFS backend. It exits 3 on a failed check.

## Learnings

- **A test that passes can still be blind.** The first version of the tests passed while a deliberately broken short-read return (`SQLITE_OK` in place of `SQLITE_IOERR_SHORT_READ`) went unnoticed, because SQLite's own header and page checks reject a truncated file anyway. Only a direct call to `xRead` past the end of the file caught it. Mutation-testing each contract, not just running the suite, is what found this. The write-refusal test did catch its mutation.
- **A vacuous assertion looks like a pass.** An early `EXPLAIN QUERY PLAN` check used `||` with a fallback that was always true; it was rewritten to read the plan's detail column.
- **ESP-IDF turns assertions into evaluations.** Under `NDEBUG`, IDF's `assert.h` defines `assert(e)` as `((void)(e))` when `CONFIG_COMPILER_ASSERT_NDEBUG_EVALUATE` is set, so the argument is still compiled. SQLite compiles its debug-only helpers out and expects `assert` to vanish, so the device build failed with implicit-declaration errors (`sqlite3PcachePageSanity`, `assert_pager_state`, `cursorHoldsMutex`, ...) on lines that look like plain asserts. The host build did not fail, because glibc's `assert` honors `NDEBUG`. The fix is `lib/sqlite3/src/sqlite3_noassert.h`, force-included by `library.json`. It works because IDF's wrapper is `#pragma once`: including it first and then redefining `assert` holds when `sqlite3.c` includes `<assert.h>` itself. `-DSQLITE_DEBUG` is not a route; SQLite's own header turns `NDEBUG` on unless it is set.
- **PlatformIO splits a two-token flag.** In `library.json`, `"-include"` and `"sqlite3_noassert.h"` as two list entries did not reach the compiler; `"-include sqlite3_noassert.h"` as one string did. Verify a new flag by reading the `-v` compile line, not by assuming.
- **PlatformIO library flags stay with the library.** They apply to the library's own objects, so the harness and the firmware share one `library.json`, but the standalone Makefile cannot use it.
- **`--fs-dir` copies a whole tree onto a small FAT image.** Pointing it at `host/sqlite/build/` would have copied every object file; the fixture has its own `build/fixture/` directory for that reason.
- **Size.** `sqlite3.c.o` is 472,476 bytes of text before section trimming, against about 1.5 MB of free app partition on `replay2026` (61.2% of the 4,063,232-byte partition used before SQLite links).
- **Memory is untested.** SQLite allocates through `malloc`, not `BadgeMemory::allocPreferPsram`, so its page cache may land in internal RAM; see the [internal DRAM contention](../concepts/internal-dram-contention.md) page. Measure it once the reader runs.
- **Standalone builds warn.** Building with `-Wextra` prints missing-initializer warnings for the trailing fields of `sqlite3_io_methods` and `sqlite3_vfs`. They are harmless (the fields are correctly zero), and the firmware build does not use `-Wextra`.
