// POSIX file backend for the standalone VFS test.
#include <stdio.h>
#include <sys/stat.h>

#include "SqliteVfs.h"

namespace {

struct PosixHandle {
  FILE* fp;
};

bool posixOpen(const char* path, void* handle) {
  FILE* fp = fopen(path, "rb");
  static_cast<PosixHandle*>(handle)->fp = fp;
  return fp != nullptr;
}

int posixRead(void* handle, void* buf, int n, int64_t offset) {
  FILE* fp = static_cast<PosixHandle*>(handle)->fp;
  if (fseeko(fp, offset, SEEK_SET) != 0) return -1;
  size_t got = fread(buf, 1, static_cast<size_t>(n), fp);
  if (got < static_cast<size_t>(n) && ferror(fp)) return -1;
  return static_cast<int>(got);
}

int64_t posixSize(void* handle) {
  FILE* fp = static_cast<PosixHandle*>(handle)->fp;
  struct stat st;
  return fstat(fileno(fp), &st) == 0 ? static_cast<int64_t>(st.st_size) : -1;
}

void posixClose(void* handle) { fclose(static_cast<PosixHandle*>(handle)->fp); }

bool posixExists(const char* path) {
  struct stat st;
  return stat(path, &st) == 0;
}

const SqliteFileOps kOps = {
    sizeof(PosixHandle), posixOpen, posixRead, posixSize, posixClose, posixExists,
};

}  // namespace

extern "C" const SqliteFileOps* sqliteFileOps() { return &kOps; }
