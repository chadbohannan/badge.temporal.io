// Tests for the read-only SQLite VFS over a POSIX backend.
//   ./sqlite-test <fixture.db>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "SqliteVfs.h"
#include "sqlite3.h"

static int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      failures++;                                                     \
    }                                                                 \
  } while (0)

static sqlite3* openDb(const char* path, int flags = SQLITE_OPEN_READONLY, int* rcOut = nullptr) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path, &db, flags, kSqliteBadgeVfs);
  if (rcOut) *rcOut = rc;
  return db;
}

// Runs a query that returns one integer column in one row.
static long long scalar(sqlite3* db, const char* sql, int* rcOut = nullptr) {
  sqlite3_stmt* st = nullptr;
  int rc = sqlite3_prepare_v2(db, sql, -1, &st, nullptr);
  long long v = -1;
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) v = sqlite3_column_int64(st, 0);
  }
  sqlite3_finalize(st);
  if (rcOut) *rcOut = rc;
  return v;
}

static std::string text(sqlite3* db, const char* sql) {
  sqlite3_stmt* st = nullptr;
  std::string out;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK &&
      sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* t = sqlite3_column_text(st, 0);
    if (t) out = reinterpret_cast<const char*>(t);
  }
  sqlite3_finalize(st);
  return out;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("usage: sqlite-test <fixture.db>\n");
    return 2;
  }
  const char* path = argv[1];

  // The VFS is registered by sqlite3_os_init() during sqlite3_initialize().
  CHECK(sqlite3_initialize() == SQLITE_OK);
  CHECK(sqlite3_vfs_find(kSqliteBadgeVfs) != nullptr);

  // Reads: rows, ordering, and a scan across many pages.
  {
    int rc = -1;
    sqlite3* db = openDb(path, SQLITE_OPEN_READONLY, &rc);
    CHECK(rc == SQLITE_OK);
    CHECK(text(db, "SELECT name FROM book WHERE bookID = 1") == "Test Book");
    CHECK(text(db, "SELECT body FROM node WHERE bookID = 1 AND nodeID = 1") == "You stand at a door.");
    CHECK(text(db, "SELECT label FROM choice WHERE bookID=1 AND nodeID=1 ORDER BY seq") == "Go right");
    CHECK(scalar(db, "SELECT count(*) FROM choice WHERE bookID=1 AND nodeID=4") == 0);  // an ending
    CHECK(scalar(db, "SELECT count(*) FROM filler") == 5000);
    CHECK(scalar(db, "SELECT sum(length(pad)) FROM filler") == 5000 * 200);

    // The tables carry no index, so a lookup is a full scan.
    CHECK(scalar(db, "SELECT count(*) FROM sqlite_master WHERE type = 'index'") == 0);
    {
      sqlite3_stmt* st = nullptr;
      CHECK(sqlite3_prepare_v2(db, "EXPLAIN QUERY PLAN SELECT * FROM node WHERE nodeID = 3", -1, &st, nullptr) == SQLITE_OK);
      CHECK(sqlite3_step(st) == SQLITE_ROW);
      const char* detail = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
      CHECK(detail && strncmp(detail, "SCAN", 4) == 0);
      sqlite3_finalize(st);
    }

    // Writes are refused.
    char* err = nullptr;
    rc = sqlite3_exec(db, "INSERT INTO book VALUES (2, 'x')", nullptr, nullptr, &err);
    CHECK(rc == SQLITE_READONLY);
    sqlite3_free(err);
    sqlite3_close(db);
  }

  // The VFS contract itself: a read past the end is SHORT_READ with the tail zeroed.
  {
    sqlite3_vfs* vfs = sqlite3_vfs_find(kSqliteBadgeVfs);
    std::string file(static_cast<size_t>(vfs->szOsFile), '\0');
    sqlite3_file* f = reinterpret_cast<sqlite3_file*>(&file[0]);
    int outFlags = 0;
    CHECK(vfs->xOpen(vfs, path, f, SQLITE_OPEN_READONLY | SQLITE_OPEN_MAIN_DB, &outFlags) == SQLITE_OK);
    sqlite3_int64 size = 0;
    CHECK(f->pMethods->xFileSize(f, &size) == SQLITE_OK);
    CHECK(size > 4096);
    char buf[100];
    memset(buf, 0xAB, sizeof(buf));
    CHECK(f->pMethods->xRead(f, buf, 100, size - 40) == SQLITE_IOERR_SHORT_READ);
    bool tailZero = true;
    for (int i = 40; i < 100; i++) tailZero = tailZero && buf[i] == 0;
    CHECK(tailZero);
    memset(buf, 0xAB, sizeof(buf));
    CHECK(f->pMethods->xRead(f, buf, 100, size + 500) == SQLITE_IOERR_SHORT_READ);  // wholly past the end
    CHECK(buf[0] == 0 && buf[99] == 0);
    CHECK(f->pMethods->xRead(f, buf, 16, 0) == SQLITE_OK);
    CHECK(memcmp(buf, "SQLite format 3", 15) == 0);
    CHECK(f->pMethods->xWrite(f, buf, 16, 0) == SQLITE_READONLY);
    f->pMethods->xClose(f);
  }

  // A missing database is CANTOPEN, not a crash.
  {
    int rc = -1;
    sqlite3* db = openDb("/nonexistent/none.db", SQLITE_OPEN_READONLY, &rc);
    CHECK(rc == SQLITE_CANTOPEN);
    sqlite3_close(db);
  }

  // Asking for write access is refused at open.
  {
    int rc = -1;
    sqlite3* db = openDb(path, SQLITE_OPEN_READWRITE, &rc);
    CHECK(rc == SQLITE_CANTOPEN);
    sqlite3_close(db);
    db = openDb(path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &rc);
    CHECK(rc == SQLITE_CANTOPEN);
    sqlite3_close(db);
  }

  // A truncated database (a partial copy) fails cleanly instead of crashing.
  {
    std::string cut = std::string(path) + ".cut";
    FILE* in = fopen(path, "rb");
    FILE* out = fopen(cut.c_str(), "wb");
    CHECK(in && out);
    if (in && out) {
      static char buf[4096];
      for (int i = 0; i < 40; i++) {  // 160 KiB of a ~1.1 MB file
        size_t n = fread(buf, 1, sizeof(buf), in);
        fwrite(buf, 1, n, out);
      }
    }
    if (in) fclose(in);
    if (out) fclose(out);
    int rc = -1;
    sqlite3* db = openDb(cut.c_str(), SQLITE_OPEN_READONLY, &rc);
    int qrc = SQLITE_OK;
    scalar(db, "SELECT sum(length(pad)) FROM filler", &qrc);
    CHECK(qrc != SQLITE_ROW && qrc != SQLITE_DONE);
    sqlite3_close(db);
    remove(cut.c_str());
  }

  // A zero-length file is not a database.
  {
    std::string empty = std::string(path) + ".empty";
    FILE* out = fopen(empty.c_str(), "wb");
    if (out) fclose(out);
    int rc = -1;
    sqlite3* db = openDb(empty.c_str(), SQLITE_OPEN_READONLY, &rc);
    int qrc = SQLITE_OK;
    scalar(db, "SELECT count(*) FROM book", &qrc);
    CHECK(qrc != SQLITE_ROW);
    sqlite3_close(db);
    remove(empty.c_str());
  }

  if (failures == 0) printf("sqlite VFS: all tests passed\n");
  return failures == 0 ? 0 : 1;
}
