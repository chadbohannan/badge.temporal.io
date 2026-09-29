// Copies a host directory tree onto the badge's FAT volume, the way uploadfs
// puts firmware/data/ on the flash. Only files that are not already there are
// written, so a kept --state-dir does not lose the user's edits.

#include "harness.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

extern "C" {
#include "lib/oofatfs/ff.h"
FATFS* replay_get_fatfs(void);
// What badge.rescan_apps() calls: rescans /apps and rebuilds the main-menu grid.
void rebuildMainMenuFromRegistry(void);
}

namespace fs = std::filesystem;

namespace harness {

namespace {
// Copies one tree. `dest` is "" for the volume root or "/apps/foo".
void copyTree(FATFS* vol, const std::string& dir, const std::string& dest, size_t* written, size_t* kept,
              size_t* failed) {
  // Create the destination path itself, one level at a time.
  for (size_t i = 1; i <= dest.size(); i++) {
    if (i == dest.size() || dest[i] == '/') f_mkdir(vol, dest.substr(0, i).c_str());
  }
  for (auto it = fs::recursive_directory_iterator(dir); it != fs::recursive_directory_iterator(); ++it) {
    const std::string rel = dest + "/" + fs::relative(it->path(), dir).generic_string();
    if (it->is_directory()) {
      f_mkdir(vol, rel.c_str());  // already there is fine
      continue;
    }
    FILINFO info;
    if (f_stat(vol, rel.c_str(), &info) == FR_OK) { (*kept)++; continue; }

    std::ifstream in(it->path(), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string data = ss.str();
    FIL f;
    if (f_open(vol, &f, rel.c_str(), FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) { (*failed)++; continue; }
    UINT n = 0;
    const bool ok = f_write(&f, data.data(), (UINT)data.size(), &n) == FR_OK && n == data.size();
    f_close(&f);
    ok ? (*written)++ : (*failed)++;
  }
}
}  // namespace

bool populateFilesystem(const std::vector<std::string>& specs) {
  FATFS* vol = replay_get_fatfs();
  if (!vol) {
    fprintf(stderr, "--fs-dir: the FAT volume is not mounted\n");
    return false;
  }
  size_t written = 0, kept = 0, failed = 0;
  for (const std::string& spec : specs) {
    std::string src = spec, dest;
    const size_t colon = spec.find(':');
    if (colon != std::string::npos) {
      src = spec.substr(0, colon);
      dest = spec.substr(colon + 1);
      while (!dest.empty() && dest.back() == '/') dest.pop_back();
      if (!dest.empty() && dest[0] != '/') dest = "/" + dest;
    }
    std::error_code ec;
    if (!fs::is_directory(src, ec)) {
      fprintf(stderr, "--fs-dir: %s is not a directory\n", src.c_str());
      return false;
    }
    copyTree(vol, src, dest, &written, &kept, &failed);
  }
  printf("[host] --fs-dir: %zu files written, %zu already present, %zu failed\n", written, kept, failed);
  // The menu was built at boot from an empty /apps, so rebuild it as rescan_apps() does.
  rebuildMainMenuFromRegistry();
  return failed == 0;
}

}  // namespace harness
