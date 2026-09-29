// ESP-IDF subset for the host: heap_caps reporting, NVS and Preferences over one
// key-value store, the register array, and no-op power and radio calls.

#include "Arduino.h"
#include "Preferences.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_pm.h"
#include "esp_bt.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_rom_crc.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/host_reg.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// ── heap_caps ─────────────────────────────────────────────────────────────
// Reporting only. The numbers are plausible, not measured: internal DRAM is
// tight and PSRAM is roomy, as on the badge. The Python heap limit is a
// separate budget (Phase 2), not derived from these.

namespace {
constexpr size_t kIntTotal = 320 * 1024, kIntFree = 200 * 1024, kIntLargest = 110 * 1024;
constexpr size_t kPsramTotal = 8u * 1024 * 1024, kPsramFree = 7u * 1024 * 1024;
bool wantsPsram(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) != 0; }
}  // namespace

extern "C" {
size_t heap_caps_get_free_size(uint32_t caps) { return wantsPsram(caps) ? kPsramFree : kIntFree; }
size_t heap_caps_get_total_size(uint32_t caps) { return wantsPsram(caps) ? kPsramTotal : kIntTotal; }
size_t heap_caps_get_minimum_free_size(uint32_t caps) { return wantsPsram(caps) ? kPsramFree : 150 * 1024; }
// "No limit": the malloc heap has no largest free block to report.
size_t heap_caps_get_largest_free_block(uint32_t caps) {
  if (caps & MALLOC_CAP_INTERNAL) return kIntLargest;
  return SIZE_MAX;
}
void heap_caps_get_info(multi_heap_info_t* info, uint32_t caps) {
  const bool ps = wantsPsram(caps);
  info->total_free_bytes = ps ? kPsramFree : kIntFree;
  info->total_allocated_bytes = (ps ? kPsramTotal : kIntTotal) - info->total_free_bytes;
  info->largest_free_block = ps ? kPsramFree : kIntLargest;
  info->minimum_free_bytes = ps ? kPsramFree : 150 * 1024;
  info->allocated_blocks = 100;
  info->free_blocks = 10;
  info->total_blocks = 110;
}
void heap_caps_print_heap_info(uint32_t caps) {
  printf("[heap] (host) %s: free=%u\n", wantsPsram(caps) ? "PSRAM" : "internal",
         (unsigned)heap_caps_get_free_size(caps));
}
void heap_caps_malloc_extmem_enable(size_t) {}
uint32_t esp_get_free_heap_size(void) { return (uint32_t)kIntFree; }
uint32_t esp_get_minimum_free_heap_size(void) { return 150 * 1024; }
}

// ── errors and log ────────────────────────────────────────────────────────
extern "C" {
const char* esp_err_to_name(esp_err_t c) {
  switch (c) {
    case ESP_OK: return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    default: return "ESP_ERR_UNKNOWN";
  }
}
void esp_log_level_set(const char*, esp_log_level_t) {}
}

// ── power, reset, radios ──────────────────────────────────────────────────
extern "C" {
void esp_restart(void) { host_exit(HOST_EXIT_RESTART); }
esp_reset_reason_t esp_reset_reason(void) { return ESP_RST_POWERON; }
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) { return ESP_SLEEP_WAKEUP_UNDEFINED; }
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t) { return ESP_OK; }
esp_err_t esp_sleep_pd_config(esp_sleep_pd_domain_t, esp_sleep_pd_option_t) { return ESP_OK; }
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t, esp_sleep_ext1_wakeup_mode_t) { return ESP_OK; }
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t) { return ESP_OK; }
esp_err_t esp_light_sleep_start(void) { return ESP_OK; }
void esp_deep_sleep_start(void) { host_exit(HOST_EXIT_DEEP_SLEEP); }

// An IDF build without CONFIG_PM_ENABLE answers this way; Power.cpp already
// handles it by pinning the CPU.
esp_err_t esp_pm_configure(const void*) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_pm_lock_create(esp_pm_lock_type_t, int, const char*, esp_pm_lock_handle_t* out) {
  if (out) *out = nullptr;
  return ESP_OK;
}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t) { return ESP_OK; }
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t) { return ESP_OK; }
esp_err_t esp_bt_mem_release(esp_bt_mode_t) { return ESP_OK; }
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* buf, uint32_t len) {
  crc = ~crc;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
  }
  return ~crc;
}

esp_err_t esp_efuse_mac_get_default(uint8_t* mac) {
  static const uint8_t kMac[6] = {0xA0, 0xB1, 0xC2, 0xD3, 0xE4, 0xF5};
  memcpy(mac, kMac, 6);
  return ESP_OK;
}
}

// ── register array ────────────────────────────────────────────────────────
namespace {
std::unordered_map<uint32_t, uint32_t>& regs() {
  static std::unordered_map<uint32_t, uint32_t> r;
  return r;
}
}  // namespace
extern "C" uint32_t host_reg_read(uint32_t addr) {
  auto it = regs().find(addr);
  return it == regs().end() ? 0 : it->second;
}
extern "C" void host_reg_write(uint32_t addr, uint32_t v) { regs()[addr] = v; }

// ── key-value store (NVS + Preferences) ───────────────────────────────────
// Ephemeral by default. The harness can point it at a file with host_kv_load()
// to keep settings across runs.

namespace {
struct Entry {
  nvs_type_t type;
  std::vector<uint8_t> data;
};
using Space = std::map<std::string, Entry>;
std::map<std::string, Space>& kv() {
  static std::map<std::string, Space> k;
  return k;
}
std::string gKvPath;

struct Handle {
  std::string ns;
  bool rw;
};
std::vector<Handle*>& handles() {
  static std::vector<Handle*> h;
  return h;
}
Handle* handleOf(nvs_handle_t h) { return h && h <= handles().size() ? handles()[h - 1] : nullptr; }

void kvSave() {
  if (gKvPath.empty()) return;
  FILE* f = fopen(gKvPath.c_str(), "w");
  if (!f) return;
  for (auto& ns : kv()) {
    for (auto& e : ns.second) {
      fprintf(f, "%s\t%s\t%d\t", ns.first.c_str(), e.first.c_str(), (int)e.second.type);
      for (uint8_t b : e.second.data) fprintf(f, "%02x", b);
      fprintf(f, "\n");
    }
  }
  fclose(f);
}

esp_err_t setRaw(nvs_handle_t h, const char* key, nvs_type_t t, const void* p, size_t n) {
  Handle* hd = handleOf(h);
  if (!hd || !hd->rw) return ESP_ERR_INVALID_STATE;
  const uint8_t* b = (const uint8_t*)p;
  kv()[hd->ns][key] = Entry{t, std::vector<uint8_t>(b, b + n)};
  return ESP_OK;
}
esp_err_t getRaw(nvs_handle_t h, const char* key, void* p, size_t n) {
  Handle* hd = handleOf(h);
  if (!hd) return ESP_ERR_INVALID_STATE;
  auto ns = kv().find(hd->ns);
  if (ns == kv().end()) return ESP_ERR_NVS_NOT_FOUND;
  auto e = ns->second.find(key);
  if (e == ns->second.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (e->second.data.size() != n) return ESP_ERR_INVALID_SIZE;
  memcpy(p, e->second.data.data(), n);
  return ESP_OK;
}
}  // namespace

extern "C" void host_kv_load(const char* path) {
  gKvPath = path ? path : "";
  kv().clear();
  if (gKvPath.empty()) return;
  FILE* f = fopen(gKvPath.c_str(), "r");
  if (!f) return;
  char line[8192];
  while (fgets(line, sizeof line, f)) {
    char ns[64], key[64], hex[8000] = {0};
    int type = 0;
    if (sscanf(line, "%63[^\t]\t%63[^\t]\t%d\t%7999s", ns, key, &type, hex) < 3) continue;
    Entry e{(nvs_type_t)type, {}};
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
      unsigned v = 0;
      sscanf(hex + i, "%2x", &v);
      e.data.push_back((uint8_t)v);
    }
    kv()[ns][key] = e;
  }
  fclose(f);
}

extern "C" {
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { kv().clear(); kvSave(); return ESP_OK; }

esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* out) {
  if (mode == NVS_READONLY && kv().find(name) == kv().end()) return ESP_ERR_NVS_NOT_FOUND;
  handles().push_back(new Handle{name, mode == NVS_READWRITE});
  *out = (nvs_handle_t)handles().size();
  return ESP_OK;
}
void nvs_close(nvs_handle_t) {}
esp_err_t nvs_commit(nvs_handle_t) { kvSave(); return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t h, const char* key) {
  Handle* hd = handleOf(h);
  if (!hd) return ESP_ERR_INVALID_STATE;
  return kv()[hd->ns].erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_erase_all(nvs_handle_t h) {
  Handle* hd = handleOf(h);
  if (!hd) return ESP_ERR_INVALID_STATE;
  kv()[hd->ns].clear();
  return ESP_OK;
}
esp_err_t nvs_set_i16(nvs_handle_t h, const char* k, int16_t v) { return setRaw(h, k, NVS_TYPE_I16, &v, sizeof v); }
esp_err_t nvs_get_i16(nvs_handle_t h, const char* k, int16_t* v) { return getRaw(h, k, v, sizeof *v); }
esp_err_t nvs_set_u8(nvs_handle_t h, const char* k, uint8_t v) { return setRaw(h, k, NVS_TYPE_U8, &v, sizeof v); }
esp_err_t nvs_get_u8(nvs_handle_t h, const char* k, uint8_t* v) { return getRaw(h, k, v, sizeof *v); }
esp_err_t nvs_set_u32(nvs_handle_t h, const char* k, uint32_t v) { return setRaw(h, k, NVS_TYPE_U32, &v, sizeof v); }
esp_err_t nvs_get_u32(nvs_handle_t h, const char* k, uint32_t* v) { return getRaw(h, k, v, sizeof *v); }
esp_err_t nvs_set_blob(nvs_handle_t h, const char* k, const void* v, size_t n) { return setRaw(h, k, NVS_TYPE_BLOB, v, n); }
esp_err_t nvs_get_blob(nvs_handle_t h, const char* k, void* v, size_t* n) {
  Handle* hd = handleOf(h);
  if (!hd) return ESP_ERR_INVALID_STATE;
  auto ns = kv().find(hd->ns);
  if (ns == kv().end() || !ns->second.count(k)) return ESP_ERR_NVS_NOT_FOUND;
  const Entry& e = ns->second[k];
  if (!v) { *n = e.data.size(); return ESP_OK; }
  if (*n < e.data.size()) { *n = e.data.size(); return ESP_ERR_INVALID_SIZE; }
  memcpy(v, e.data.data(), e.data.size());
  *n = e.data.size();
  return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char* k, const char* v) { return setRaw(h, k, NVS_TYPE_STR, v, strlen(v) + 1); }
esp_err_t nvs_get_str(nvs_handle_t h, const char* k, char* v, size_t* n) { return nvs_get_blob(h, k, v, n); }

struct nvs_opaque_iterator_t {
  std::vector<nvs_entry_info_t> items;
  size_t at;
};
esp_err_t nvs_entry_find(const char*, const char* ns, nvs_type_t type, nvs_iterator_t* out) {
  auto* it = new nvs_opaque_iterator_t{{}, 0};
  auto space = kv().find(ns ? ns : "");
  if (space != kv().end()) {
    for (auto& e : space->second) {
      if (type != NVS_TYPE_ANY && e.second.type != type) continue;
      nvs_entry_info_t info{};
      snprintf(info.namespace_name, sizeof info.namespace_name, "%s", ns);
      snprintf(info.key, sizeof info.key, "%s", e.first.c_str());
      info.type = e.second.type;
      it->items.push_back(info);
    }
  }
  if (it->items.empty()) { delete it; *out = nullptr; return ESP_ERR_NVS_NOT_FOUND; }
  *out = it;
  return ESP_OK;
}
esp_err_t nvs_entry_next(nvs_iterator_t* it) {
  if (!it || !*it) return ESP_ERR_NVS_NOT_FOUND;
  if (++(*it)->at >= (*it)->items.size()) { delete *it; *it = nullptr; return ESP_ERR_NVS_NOT_FOUND; }
  return ESP_OK;
}
esp_err_t nvs_entry_info(const nvs_iterator_t it, nvs_entry_info_t* info) {
  if (!it) return ESP_ERR_INVALID_ARG;
  *info = it->items[it->at];
  return ESP_OK;
}
void nvs_release_iterator(nvs_iterator_t it) { delete it; }
}

bool Preferences::begin(const char* name, bool readOnly, const char*) {
  snprintf(ns_, sizeof ns_, "%s", name);
  readOnly_ = readOnly;
  if (readOnly && kv().find(ns_) == kv().end()) { open_ = false; return false; }
  if (!readOnly) kv()[ns_];
  open_ = true;
  return true;
}
void Preferences::end() { if (open_) kvSave(); open_ = false; }
bool Preferences::clear() { if (!open_ || readOnly_) return false; kv()[ns_].clear(); return true; }
bool Preferences::remove(const char* key) { return open_ && !readOnly_ && kv()[ns_].erase(key) > 0; }
bool Preferences::isKey(const char* key) {
  if (!open_) return false;
  auto ns = kv().find(ns_);
  return ns != kv().end() && ns->second.count(key);
}

namespace {
template <typename T>
size_t putNum(Preferences& p, const char* ns, bool open, bool ro, const char* key, nvs_type_t t, T v) {
  (void)p;
  if (!open || ro) return 0;
  const uint8_t* b = (const uint8_t*)&v;
  kv()[ns][key] = Entry{t, std::vector<uint8_t>(b, b + sizeof v)};
  return sizeof v;
}
template <typename T>
T getNum(const char* ns, bool open, const char* key, T def) {
  if (!open) return def;
  auto space = kv().find(ns);
  if (space == kv().end()) return def;
  auto e = space->second.find(key);
  if (e == space->second.end() || e->second.data.size() != sizeof(T)) return def;
  T v;
  memcpy(&v, e->second.data.data(), sizeof v);
  return v;
}
}  // namespace

size_t Preferences::putUChar(const char* k, uint8_t v) { return putNum(*this, ns_, open_, readOnly_, k, NVS_TYPE_U8, v); }
size_t Preferences::putChar(const char* k, int8_t v) { return putNum(*this, ns_, open_, readOnly_, k, NVS_TYPE_I8, v); }
size_t Preferences::putUShort(const char* k, uint16_t v) { return putNum(*this, ns_, open_, readOnly_, k, NVS_TYPE_U16, v); }
size_t Preferences::putShort(const char* k, int16_t v) { return putNum(*this, ns_, open_, readOnly_, k, NVS_TYPE_I16, v); }
size_t Preferences::putUInt(const char* k, uint32_t v) { return putNum(*this, ns_, open_, readOnly_, k, NVS_TYPE_U32, v); }
size_t Preferences::putInt(const char* k, int32_t v) { return putNum(*this, ns_, open_, readOnly_, k, NVS_TYPE_I32, v); }
size_t Preferences::putULong64(const char* k, uint64_t v) { return putNum(*this, ns_, open_, readOnly_, k, NVS_TYPE_U64, v); }
uint64_t Preferences::getULong64(const char* k, uint64_t d) { return getNum<uint64_t>(ns_, open_, k, d); }
size_t Preferences::putString(const char* k, const char* v) {
  if (!open_ || readOnly_) return 0;
  kv()[ns_][k] = Entry{NVS_TYPE_STR, std::vector<uint8_t>(v, v + strlen(v) + 1)};
  return strlen(v);
}
size_t Preferences::putBytes(const char* k, const void* v, size_t n) {
  if (!open_ || readOnly_) return 0;
  const uint8_t* b = (const uint8_t*)v;
  kv()[ns_][k] = Entry{NVS_TYPE_BLOB, std::vector<uint8_t>(b, b + n)};
  return n;
}
uint8_t Preferences::getUChar(const char* k, uint8_t d) { return getNum<uint8_t>(ns_, open_, k, d); }
int8_t Preferences::getChar(const char* k, int8_t d) { return getNum<int8_t>(ns_, open_, k, d); }
uint16_t Preferences::getUShort(const char* k, uint16_t d) { return getNum<uint16_t>(ns_, open_, k, d); }
int16_t Preferences::getShort(const char* k, int16_t d) { return getNum<int16_t>(ns_, open_, k, d); }
uint32_t Preferences::getUInt(const char* k, uint32_t d) { return getNum<uint32_t>(ns_, open_, k, d); }
int32_t Preferences::getInt(const char* k, int32_t d) { return getNum<int32_t>(ns_, open_, k, d); }
size_t Preferences::getString(const char* k, char* out, size_t maxLen) {
  if (!open_ || maxLen == 0) return 0;
  auto space = kv().find(ns_);
  if (space == kv().end() || !space->second.count(k)) { out[0] = 0; return 0; }
  const Entry& e = space->second[k];
  size_t n = e.data.size() < maxLen ? e.data.size() : maxLen;
  memcpy(out, e.data.data(), n);
  out[n - 1] = 0;
  return n ? n - 1 : 0;
}
String Preferences::getString(const char* k, const String& def) {
  if (!open_) return def;
  auto space = kv().find(ns_);
  if (space == kv().end() || !space->second.count(k)) return def;
  return String((const char*)space->second[k].data.data());
}
size_t Preferences::getBytesLength(const char* k) {
  if (!open_) return 0;
  auto space = kv().find(ns_);
  return space == kv().end() || !space->second.count(k) ? 0 : space->second[k].data.size();
}
size_t Preferences::getBytes(const char* k, void* out, size_t maxLen) {
  if (!open_) return 0;
  auto space = kv().find(ns_);
  if (space == kv().end() || !space->second.count(k)) return 0;
  const Entry& e = space->second[k];
  size_t n = e.data.size() < maxLen ? e.data.size() : maxLen;
  memcpy(out, e.data.data(), n);
  return n;
}
