#ifndef SQLITE_VFS_H
#define SQLITE_VFS_H

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
//  Read-only SQLite VFS for the badge.
//
//  The VFS core (SqliteVfs.cpp) knows nothing about the filesystem. It calls
//  a SqliteFileOps table supplied by a backend: SqliteVfsFat.cpp on the badge
//  and the host harness (oofatfs under Filesystem::IOLock), or a POSIX
//  backend in host/sqlite/ for the standalone test.
//
//  SQLite is built with SQLITE_OS_OTHER, so it has no default VFS. Its
//  sqlite3_os_init() is defined in SqliteVfs.cpp and registers this one under
//  the name kSqliteBadgeVfs, so callers open a database with
//
//    sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, kSqliteBadgeVfs);
//
//  Writes, journals and temp files are refused; story data is read-only and
//  the reader keeps its state in NVS. The library is single-threaded
//  (SQLITE_THREADSAFE=0), so one task at a time may use a connection.
// ---------------------------------------------------------------------------

constexpr const char* kSqliteBadgeVfs = "badge";

struct SqliteFileOps {
  // Bytes of per-file state the core reserves for the backend.
  size_t handleSize;
  // Open `path` read-only into `handle`. False if it is missing or unreadable.
  bool (*open)(const char* path, void* handle);
  // Read up to `n` bytes at `offset`. Returns bytes read (0 at or past the end),
  // or -1 on an I/O error.
  int (*read)(void* handle, void* buf, int n, int64_t offset);
  // File size in bytes, or -1 on error.
  int64_t (*size)(void* handle);
  void (*close)(void* handle);
  // True iff `path` exists.
  bool (*exists)(const char* path);
};

// Supplied by exactly one backend in the final binary.
extern "C" const SqliteFileOps* sqliteFileOps();

#endif
