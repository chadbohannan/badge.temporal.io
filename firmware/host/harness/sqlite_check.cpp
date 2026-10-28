// --sqlite-check PATH: opens a database on the FAT volume through the badge's
// read-only SQLite VFS (src/infra/SqliteVfs.cpp over SqliteVfsFat.cpp, i.e. the
// real oofatfs under Filesystem::IOLock) and checks the fixture written by
// host/sqlite/make_fixture.py. Load the fixture with --fs-dir.

#include "harness.h"

#include <cstdio>
#include <cstring>

#include "infra/SqliteVfs.h"
#include "sqlite3.h"

namespace harness {

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  printf("[host] sqlite-check: %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

// First column of the first row as an integer, or -1.
long long scalar(sqlite3* db, const char* sql) {
  sqlite3_stmt* st = nullptr;
  long long v = -1;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
    v = sqlite3_column_int64(st, 0);
  }
  sqlite3_finalize(st);
  return v;
}

}  // namespace

int sqliteCheck(const std::string& path) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, kSqliteBadgeVfs);
  check(rc == SQLITE_OK, "open read-only");
  if (rc == SQLITE_OK) {
    check(scalar(db, "SELECT count(*) FROM node") == 4, "node rows");
    check(scalar(db, "SELECT count(*) FROM choice WHERE bookID=1 AND nodeID=1") == 2, "choices of the root");
    check(scalar(db, "SELECT count(*) FROM filler") == 5000, "scan across many pages");
    check(scalar(db, "SELECT sum(length(pad)) FROM filler") == 5000 * 200, "page contents");
    char* err = nullptr;
    check(sqlite3_exec(db, "INSERT INTO book VALUES (2, 'x')", nullptr, nullptr, &err) == SQLITE_READONLY,
          "write refused");
    sqlite3_free(err);
  }
  sqlite3_close(db);

  sqlite3* missing = nullptr;
  check(sqlite3_open_v2("/no/such.db", &missing, SQLITE_OPEN_READONLY, kSqliteBadgeVfs) == SQLITE_CANTOPEN,
        "missing file is CANTOPEN");
  sqlite3_close(missing);
  return failures == 0 ? kExitOk : kExitAssertion;
}

}  // namespace harness
