#include "WString.h"
#include <algorithm>
#include <ctype.h>

bool String::equalsIgnoreCase(const String& o) const {
  if (s_.size() != o.s_.size()) return false;
  for (size_t i = 0; i < s_.size(); i++) {
    if (tolower((unsigned char)s_[i]) != tolower((unsigned char)o.s_[i])) return false;
  }
  return true;
}

void String::replace(const String& a, const String& b) {
  if (a.s_.empty()) return;
  size_t pos = 0;
  while ((pos = s_.find(a.s_, pos)) != std::string::npos) {
    s_.replace(pos, a.s_.size(), b.s_);
    pos += b.s_.size();
  }
}

void String::trim() {
  size_t b = 0, e = s_.size();
  while (b < e && isspace((unsigned char)s_[b])) b++;
  while (e > b && isspace((unsigned char)s_[e - 1])) e--;
  s_ = s_.substr(b, e - b);
}

void String::toLowerCase() { std::transform(s_.begin(), s_.end(), s_.begin(), [](unsigned char c) { return tolower(c); }); }
void String::toUpperCase() { std::transform(s_.begin(), s_.end(), s_.begin(), [](unsigned char c) { return toupper(c); }); }
