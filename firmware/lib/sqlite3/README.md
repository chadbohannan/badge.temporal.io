# sqlite3

Vendored SQLite amalgamation, version 3.53.4 (`sqlite-amalgamation-3530400.zip`
from sqlite.org, SHA3-256 `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e`).
SQLite is public domain.

To upgrade, replace `src/sqlite3.c` and `src/sqlite3.h` and update the version
here and in `library.json`. Do not edit them by hand.

The build flags in `library.json` select a small, single-threaded, read-only
configuration with no OS layer: `SQLITE_OS_OTHER=1` means SQLite has no built-in
VFS, and `sqlite3_os_init()` in `src/infra/SqliteVfs.cpp` registers the badge one.
`host/sqlite/Makefile` repeats the same flags for the standalone test; keep the
two lists in step.
