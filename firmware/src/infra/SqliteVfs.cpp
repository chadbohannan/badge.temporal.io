#include "SqliteVfs.h"

#include <string.h>
#include <time.h>

#include "sqlite3.h"

namespace {

struct BadgeFile {
  sqlite3_file base;
  // Backend state follows; sqlite3_vfs::szOsFile covers it.
};

inline void* handleOf(sqlite3_file* f) {
  return reinterpret_cast<char*>(f) + sizeof(BadgeFile);
}

const SqliteFileOps* ops() { return sqliteFileOps(); }

// ── sqlite3_io_methods ──────────────────────────────────────────────────────

int ioClose(sqlite3_file* f) {
  ops()->close(handleOf(f));
  return SQLITE_OK;
}

int ioRead(sqlite3_file* f, void* buf, int n, sqlite3_int64 offset) {
  int got = ops()->read(handleOf(f), buf, n, offset);
  if (got < 0) return SQLITE_IOERR_READ;
  if (got < n) {
    // SQLite requires the unread tail to be zeroed on a short read.
    memset(static_cast<char*>(buf) + got, 0, static_cast<size_t>(n - got));
    return SQLITE_IOERR_SHORT_READ;
  }
  return SQLITE_OK;
}

int ioWrite(sqlite3_file*, const void*, int, sqlite3_int64) { return SQLITE_READONLY; }
int ioTruncate(sqlite3_file*, sqlite3_int64) { return SQLITE_READONLY; }
int ioSync(sqlite3_file*, int) { return SQLITE_OK; }

int ioFileSize(sqlite3_file* f, sqlite3_int64* out) {
  int64_t size = ops()->size(handleOf(f));
  if (size < 0) return SQLITE_IOERR_FSTAT;
  *out = size;
  return SQLITE_OK;
}

// One reader, no other process: locking is a no-op.
int ioLock(sqlite3_file*, int) { return SQLITE_OK; }
int ioUnlock(sqlite3_file*, int) { return SQLITE_OK; }
int ioCheckReservedLock(sqlite3_file*, int* out) {
  *out = 0;
  return SQLITE_OK;
}

int ioFileControl(sqlite3_file*, int, void*) { return SQLITE_NOTFOUND; }
int ioSectorSize(sqlite3_file*) { return 512; }
int ioDeviceCharacteristics(sqlite3_file*) { return SQLITE_IOCAP_IMMUTABLE; }

const sqlite3_io_methods kIoMethods = {
    1,
    ioClose,
    ioRead,
    ioWrite,
    ioTruncate,
    ioSync,
    ioFileSize,
    ioLock,
    ioUnlock,
    ioCheckReservedLock,
    ioFileControl,
    ioSectorSize,
    ioDeviceCharacteristics,
};

// ── sqlite3_vfs ─────────────────────────────────────────────────────────────

int vfsOpen(sqlite3_vfs*, sqlite3_filename name, sqlite3_file* f, int flags, int*) {
  // Only the main database, read-only. Journals and temp files are never
  // needed (no writes; SQLITE_TEMP_STORE=3 keeps temp tables in memory).
  const int kWriteFlags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  if (name == nullptr || (flags & kWriteFlags) || !(flags & SQLITE_OPEN_MAIN_DB)) {
    return SQLITE_CANTOPEN;
  }
  f->pMethods = nullptr;
  memset(handleOf(f), 0, ops()->handleSize);
  if (!ops()->open(name, handleOf(f))) return SQLITE_CANTOPEN;
  f->pMethods = &kIoMethods;
  return SQLITE_OK;
}

int vfsDelete(sqlite3_vfs*, const char*, int) { return SQLITE_READONLY; }

int vfsAccess(sqlite3_vfs*, const char* name, int flags, int* out) {
  // A missing "-journal" file is how SQLite learns there is no hot journal.
  *out = (flags == SQLITE_ACCESS_EXISTS) ? ops()->exists(name) : 0;
  return SQLITE_OK;
}

int vfsFullPathname(sqlite3_vfs*, const char* name, int nOut, char* out) {
  size_t len = strlen(name);
  if (len >= static_cast<size_t>(nOut)) return SQLITE_CANTOPEN;
  memcpy(out, name, len + 1);
  return SQLITE_OK;
}

int vfsRandomness(sqlite3_vfs*, int n, char* out) {
  // Only seeds PRNG uses SQLite makes for its own scratch; quality is irrelevant
  // on a read-only database.
  static uint32_t state = 0x9e3779b9u;
  for (int i = 0; i < n; i++) {
    state = state * 1664525u + 1013904223u;
    out[i] = static_cast<char>(state >> 24);
  }
  return n;
}

int vfsSleep(sqlite3_vfs*, int micros) { return micros; }

int vfsCurrentTime(sqlite3_vfs*, double* out) {
  *out = 2440587.5 + static_cast<double>(time(nullptr)) / 86400.0;
  return SQLITE_OK;
}

sqlite3_vfs gVfs = {
    1,                 // iVersion
    0,                 // szOsFile, set in sqlite3_os_init()
    1024,              // mxPathname
    nullptr,           // pNext
    kSqliteBadgeVfs,   // zName
    nullptr,           // pAppData
    vfsOpen,
    vfsDelete,
    vfsAccess,
    vfsFullPathname,
    nullptr,           // xDlOpen
    nullptr,           // xDlError
    nullptr,           // xDlSym
    nullptr,           // xDlClose
    vfsRandomness,
    vfsSleep,
    vfsCurrentTime,
    nullptr,           // xGetLastError
};

}  // namespace

// With SQLITE_OS_OTHER, SQLite calls these from sqlite3_initialize()/shutdown().
extern "C" int sqlite3_os_init(void) {
  gVfs.szOsFile = static_cast<int>(sizeof(BadgeFile) + ops()->handleSize);
  return sqlite3_vfs_register(&gVfs, 1);
}

extern "C" int sqlite3_os_end(void) { return SQLITE_OK; }
