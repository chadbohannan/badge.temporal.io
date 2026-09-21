#pragma once
// Minimal 8-bit greyscale PNG writer: stored (uncompressed) deflate blocks,
// so it needs no zlib. Plenty for 512x256 frames.
#include <cstdint>
#include <string>
#include <vector>

namespace png {

// `grey` is w*h bytes, row-major. Returns false if the file can't be opened.
bool write(const std::string& path, int w, int h, const std::vector<uint8_t>& grey);

}  // namespace png
