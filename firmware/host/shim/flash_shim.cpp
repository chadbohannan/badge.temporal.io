// A RAM-backed "ffat" partition under the ESP-IDF wear-levelling API. The real
// replay_bdev.c runs over it unchanged, so the firmware's own mount and format
// logic, MicroPython's VFS, and every direct oofatfs caller behave as on the badge.
// Sizes match the badge: a 0x600000-byte partition with 4096-byte sectors.

#include "wear_levelling.h"
#include <stdio.h>
#include <string.h>
#include <vector>

namespace {
constexpr uint32_t kPartitionBytes = 0x600000;
constexpr size_t kSector = 4096;
std::vector<uint8_t>& image() {
  static std::vector<uint8_t> img(kPartitionBytes, 0xFF);  // erased flash reads 0xFF
  return img;
}
esp_partition_t gFat = {0x7D0000, kPartitionBytes, "ffat"};
}  // namespace

extern "C" {

const esp_partition_t* esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char* label) {
  if (type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_DATA_FAT &&
      (!label || strcmp(label, "ffat") == 0)) {
    return &gFat;
  }
  return nullptr;
}

esp_err_t wl_mount(const esp_partition_t* p, wl_handle_t* out) {
  if (p != &gFat) return ESP_ERR_NOT_FOUND;
  *out = 0;
  return ESP_OK;
}
esp_err_t wl_unmount(wl_handle_t) { return ESP_OK; }

esp_err_t wl_erase_range(wl_handle_t, size_t start, size_t size) {
  if (start + size > image().size()) return ESP_ERR_INVALID_ARG;
  memset(image().data() + start, 0xFF, size);
  return ESP_OK;
}
esp_err_t wl_write(wl_handle_t, size_t dest, const void* src, size_t size) {
  if (dest + size > image().size()) return ESP_ERR_INVALID_ARG;
  memcpy(image().data() + dest, src, size);
  return ESP_OK;
}
esp_err_t wl_read(wl_handle_t, size_t src, void* dest, size_t size) {
  if (src + size > image().size()) return ESP_ERR_INVALID_ARG;
  memcpy(dest, image().data() + src, size);
  return ESP_OK;
}
size_t wl_size(wl_handle_t) { return image().size(); }
size_t wl_sector_size(wl_handle_t) { return kSector; }

void host_flash_load(const char* path) {
  if (FILE* f = fopen(path, "rb")) {
    if (fread(image().data(), 1, image().size(), f) != image().size()) {
      fprintf(stderr, "[host] %s is not a full image; ignoring it\n", path);
      std::fill(image().begin(), image().end(), 0xFF);
    }
    fclose(f);
  }
}
void host_flash_save(const char* path) {
  if (FILE* f = fopen(path, "wb")) {
    fwrite(image().data(), 1, image().size(), f);
    fclose(f);
  }
}

}  // extern "C"
