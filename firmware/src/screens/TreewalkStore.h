#pragma once
#include <stdint.h>

#include <string>
#include <vector>

struct sqlite3;

// ─── TREEWALK story store ───────────────────────────────────────────────────
//
// Read-only queries over treewalk.db (schema and rules: wiki/components/
// treewalk.md). Every lookup is a full scan; the tables carry no index.
// One connection, used from the UI task only. No firmware dependencies beyond
// SQLite and the badge VFS, so the host harness runs it unchanged.

namespace treewalk {

constexpr int kMaxChoices = 3;

struct Book {
  int32_t id = 0;
  std::string name;
};

struct Choice {
  std::string label;
  int32_t childId = 0;
};

struct Node {
  std::string body;
  bool hasParent = false;
  int32_t parentId = 0;
  std::vector<Choice> choices;  // at most kMaxChoices, in seq order
};

class Store {
 public:
  ~Store() { close(); }
  bool open(const char* path);
  void close();
  bool isOpen() const { return db_ != nullptr; }

  std::vector<Book> books();
  bool rootOf(int32_t bookId, int32_t* nodeId);
  bool nodeExists(int32_t bookId, int32_t nodeId);
  bool loadNode(int32_t bookId, int32_t nodeId, Node* out);

 private:
  sqlite3* db_ = nullptr;
};

}  // namespace treewalk
