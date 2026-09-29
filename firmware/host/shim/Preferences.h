#pragma once
#include <stdint.h>
#include <stddef.h>
#include "Arduino.h"

// Preferences, nvs.h and nvs_flash.h all run over one host key-value store
// (host_kv in esp_shim.cpp), persisted to a file when the harness asks for it.
class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false, const char* partition = nullptr);
  void end();
  bool clear();
  bool remove(const char* key);
  bool isKey(const char* key);
  size_t freeEntries() { return 500; }

  size_t putUChar(const char* key, uint8_t v);
  size_t putChar(const char* key, int8_t v);
  size_t putUShort(const char* key, uint16_t v);
  size_t putShort(const char* key, int16_t v);
  size_t putUInt(const char* key, uint32_t v);
  size_t putInt(const char* key, int32_t v);
  size_t putULong(const char* key, uint32_t v) { return putUInt(key, v); }
  size_t putULong64(const char* key, uint64_t v);
  size_t putBool(const char* key, bool v) { return putUChar(key, v ? 1 : 0); }
  size_t putString(const char* key, const char* v);
  size_t putBytes(const char* key, const void* v, size_t len);

  uint8_t getUChar(const char* key, uint8_t def = 0);
  int8_t getChar(const char* key, int8_t def = 0);
  uint16_t getUShort(const char* key, uint16_t def = 0);
  int16_t getShort(const char* key, int16_t def = 0);
  uint32_t getUInt(const char* key, uint32_t def = 0);
  int32_t getInt(const char* key, int32_t def = 0);
  uint32_t getULong(const char* key, uint32_t def = 0) { return getUInt(key, def); }
  uint64_t getULong64(const char* key, uint64_t def = 0);
  bool getBool(const char* key, bool def = false) { return getUChar(key, def ? 1 : 0) != 0; }
  size_t getString(const char* key, char* out, size_t maxLen);
  String getString(const char* key, const String& def = String());
  size_t getBytesLength(const char* key);
  size_t getBytes(const char* key, void* out, size_t maxLen);

 private:
  char ns_[16] = {0};
  bool open_ = false;
  bool readOnly_ = false;
};
