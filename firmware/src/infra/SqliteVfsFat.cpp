// FATFS backend for the read-only SQLite VFS (SqliteVfs.h). Every FATFS call
// runs under Filesystem::IOLock, as the rest of the firmware requires.
#include "Filesystem.h"
#include "SqliteVfs.h"

extern "C" {
#include "lib/oofatfs/ff.h"
FATFS* replay_get_fatfs(void);
}

namespace {

struct FatHandle {
  FIL fil;
};

bool fatOpen(const char* path, void* handle) {
  Filesystem::IOLock lock;
  FATFS* fs = replay_get_fatfs();
  if (!fs) return false;
  return f_open(fs, &static_cast<FatHandle*>(handle)->fil, path, FA_READ) == FR_OK;
}

int fatRead(void* handle, void* buf, int n, int64_t offset) {
  Filesystem::IOLock lock;
  FIL* fil = &static_cast<FatHandle*>(handle)->fil;
  if (offset < 0 || offset >= static_cast<int64_t>(f_size(fil))) return 0;
  if (f_lseek(fil, static_cast<FSIZE_t>(offset)) != FR_OK) return -1;
  UINT got = 0;
  if (f_read(fil, buf, static_cast<UINT>(n), &got) != FR_OK) return -1;
  return static_cast<int>(got);
}

int64_t fatSize(void* handle) {
  Filesystem::IOLock lock;
  return static_cast<int64_t>(f_size(&static_cast<FatHandle*>(handle)->fil));
}

void fatClose(void* handle) {
  Filesystem::IOLock lock;
  f_close(&static_cast<FatHandle*>(handle)->fil);
}

bool fatExists(const char* path) { return Filesystem::fileExists(path); }

const SqliteFileOps kFatOps = {
    sizeof(FatHandle), fatOpen, fatRead, fatSize, fatClose, fatExists,
};

}  // namespace

extern "C" const SqliteFileOps* sqliteFileOps() { return &kFatOps; }
