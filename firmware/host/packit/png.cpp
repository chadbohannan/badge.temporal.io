#include "png.h"

#include <algorithm>
#include <cstdio>

namespace png {
namespace {

uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c ^ 0xFFFFFFFFu;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

void chunk(std::FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> v;
  put32(v, static_cast<uint32_t>(data.size()));
  v.insert(v.end(), type, type + 4);
  v.insert(v.end(), data.begin(), data.end());
  put32(v, crc32(&v[4], data.size() + 4));
  std::fwrite(v.data(), 1, v.size(), f);
}

}  // namespace

bool write(const std::string& path, int w, int h, const std::vector<uint8_t>& grey) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  std::fwrite(sig, 1, 8, f);

  std::vector<uint8_t> ihdr;
  put32(ihdr, w);
  put32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 0, 0, 0, 0});  // 8-bit grey, no interlace
  chunk(f, "IHDR", ihdr);

  // Scanlines with filter byte 0, wrapped in a zlib stream of stored blocks.
  std::vector<uint8_t> raw;
  raw.reserve((w + 1) * h);
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), grey.begin() + y * w, grey.begin() + (y + 1) * w);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  for (size_t pos = 0; pos < raw.size();) {
    size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n == raw.size() ? 1 : 0);
    z.push_back(n & 0xFF); z.push_back(n >> 8);
    z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  put32(z, (b << 16) | a);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  std::fclose(f);
  return true;
}

}  // namespace png
