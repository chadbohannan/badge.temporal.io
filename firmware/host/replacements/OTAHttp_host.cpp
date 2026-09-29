// Host OTAHttp: every request fails with a network error. AssetRegistry stays
// real above it, so the Apps and registry screens run their offline paths.

#include "ota/OTAHttp.h"

namespace ota {

ThroughputBoost::ThroughputBoost() {}
ThroughputBoost::~ThroughputBoost() {}

HttpResult getJson(const char*, char** outBuf, size_t* outLen, size_t, uint32_t) {
  if (outBuf) *outBuf = nullptr;
  if (outLen) *outLen = 0;
  return HttpResult{-1, 0, false, "no network on the host"};
}

bool resolveRedirect(const char*, char*, size_t, uint32_t) { return false; }

Stream::Stream() { snprintf(lastError_, sizeof lastError_, "no network on the host"); }
Stream::~Stream() {}
bool Stream::open(const char*, uint32_t, size_t) {
  httpCode_ = -1;
  snprintf(lastError_, sizeof lastError_, "no network on the host");
  return false;
}
bool Stream::connected() const { return false; }
int Stream::read(uint8_t*, size_t) { return -1; }
void Stream::close() {}

}  // namespace ota
