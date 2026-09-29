// Host stand-in for micropython/badge_mp_api/mp_api_http.cpp, the only direct
// HTTPClient/WiFiClientSecure user outside ota/. The host has no network, so a
// request reports an error the way the real one does when WiFi is down.

#include "temporalbadge_runtime.h"

namespace {
// Same shape as the real setError() output, so Python apps parse it the same way.
const char* kError = "{\"ok\":false,\"error\":\"no network on the host\"}";
}

extern "C" const char* temporalbadge_runtime_http_get(const char*) { return kError; }
extern "C" const char* temporalbadge_runtime_http_post(const char*, const char*) { return kError; }
