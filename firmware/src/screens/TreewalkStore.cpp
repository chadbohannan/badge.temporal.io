#include "TreewalkStore.h"

#include "../infra/SqliteVfs.h"
#include "sqlite3.h"

namespace treewalk {

namespace {

// Prepares `sql`, binds up to two integers, and hands the statement to `fn`.
template <typename Fn>
bool query(sqlite3* db, const char* sql, int a, int b, int binds, Fn fn) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
  if (binds > 0) sqlite3_bind_int(st, 1, a);
  if (binds > 1) sqlite3_bind_int(st, 2, b);
  fn(st);
  sqlite3_finalize(st);
  return true;
}

std::string text(sqlite3_stmt* st, int col) {
  const unsigned char* s = sqlite3_column_text(st, col);
  return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
}

}  // namespace

bool Store::open(const char* path) {
  close();
  if (sqlite3_open_v2(path, &db_, SQLITE_OPEN_READONLY, kSqliteBadgeVfs) != SQLITE_OK) {
    close();
    return false;
  }
  return true;
}

void Store::close() {
  if (db_) sqlite3_close(db_);
  db_ = nullptr;
}

std::vector<Book> Store::books() {
  std::vector<Book> out;
  if (!db_) return out;
  query(db_, "SELECT bookID, name FROM book ORDER BY name", 0, 0, 0, [&](sqlite3_stmt* st) {
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back({sqlite3_column_int(st, 0), text(st, 1)});
  });
  return out;
}

bool Store::rootOf(int32_t bookId, int32_t* nodeId) {
  bool found = false;
  if (!db_) return false;
  query(db_, "SELECT nodeID FROM node WHERE bookID = ?1 AND parentID IS NULL LIMIT 1", bookId, 0, 1,
        [&](sqlite3_stmt* st) {
          if (sqlite3_step(st) == SQLITE_ROW) {
            *nodeId = sqlite3_column_int(st, 0);
            found = true;
          }
        });
  return found;
}

bool Store::nodeExists(int32_t bookId, int32_t nodeId) {
  bool found = false;
  if (!db_) return false;
  query(db_, "SELECT 1 FROM node WHERE bookID = ?1 AND nodeID = ?2 LIMIT 1", bookId, nodeId, 2,
        [&](sqlite3_stmt* st) { found = sqlite3_step(st) == SQLITE_ROW; });
  return found;
}

bool Store::loadNode(int32_t bookId, int32_t nodeId, Node* out) {
  bool found = false;
  if (!db_) return false;
  Node n;
  query(db_, "SELECT body, parentID FROM node WHERE bookID = ?1 AND nodeID = ?2 LIMIT 1", bookId, nodeId, 2,
        [&](sqlite3_stmt* st) {
          if (sqlite3_step(st) != SQLITE_ROW) return;
          found = true;
          n.body = text(st, 0);
          n.hasParent = sqlite3_column_type(st, 1) != SQLITE_NULL;
          n.parentId = sqlite3_column_int(st, 1);
        });
  if (!found) return false;
  query(db_, "SELECT label, childID FROM choice WHERE bookID = ?1 AND nodeID = ?2 ORDER BY seq LIMIT 3",
        bookId, nodeId, 2, [&](sqlite3_stmt* st) {
          while (sqlite3_step(st) == SQLITE_ROW) n.choices.push_back({text(st, 0), sqlite3_column_int(st, 1)});
        });
  *out = std::move(n);
  return true;
}

}  // namespace treewalk
