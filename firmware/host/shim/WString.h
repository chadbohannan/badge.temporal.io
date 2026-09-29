#pragma once
#include <string>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

// Small Arduino String: a std::string with the methods the firmware calls.
class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) { char b[24]; snprintf(b, sizeof b, "%d", v); s_ = b; }
  String(unsigned int v) { char b[24]; snprintf(b, sizeof b, "%u", v); s_ = b; }
  String(long v) { char b[24]; snprintf(b, sizeof b, "%ld", v); s_ = b; }
  String(unsigned long v) { char b[24]; snprintf(b, sizeof b, "%lu", v); s_ = b; }
  String(float v, unsigned char d = 2) { char b[48]; snprintf(b, sizeof b, "%.*f", d, (double)v); s_ = b; }
  String(double v, unsigned char d = 2) { char b[48]; snprintf(b, sizeof b, "%.*f", d, v); s_ = b; }

  const char* c_str() const { return s_.c_str(); }
  unsigned int length() const { return (unsigned int)s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  bool reserve(unsigned int n) { s_.reserve(n); return true; }
  char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
  char operator[](unsigned int i) const { return charAt(i); }
  char& operator[](unsigned int i) { return s_[i]; }
  void setCharAt(unsigned int i, char c) { if (i < s_.size()) s_[i] = c; }

  String& operator=(const char* s) { s_ = s ? s : ""; return *this; }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* o) { if (o) s_ += o; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  bool concat(const char* o) { if (o) s_ += o; return true; }
  bool concat(const String& o) { s_ += o.s_; return true; }
  bool concat(char c) { s_ += c; return true; }
  friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
  friend String operator+(const String& a, const char* b) { return String(a.s_ + (b ? b : "")); }
  friend String operator+(const char* a, const String& b) { return String(std::string(a ? a : "") + b.s_); }
  friend String operator+(const String& a, char b) { return String(a.s_ + b); }

  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* o) const { return s_ == (o ? o : ""); }
  bool operator!=(const String& o) const { return s_ != o.s_; }
  bool operator!=(const char* o) const { return s_ != (o ? o : ""); }
  bool operator<(const String& o) const { return s_ < o.s_; }
  bool equals(const String& o) const { return s_ == o.s_; }
  bool equals(const char* o) const { return s_ == (o ? o : ""); }
  bool equalsIgnoreCase(const String& o) const;
  operator bool() const = delete;

  int indexOf(char c, unsigned int from = 0) const { size_t p = s_.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String& t, unsigned int from = 0) const { size_t p = s_.find(t.s_, from); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { size_t p = s_.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  bool startsWith(const String& p) const { return s_.compare(0, p.s_.size(), p.s_) == 0; }
  bool endsWith(const String& p) const { return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0; }
  String substring(unsigned int from) const { return from >= s_.size() ? String() : String(s_.substr(from)); }
  String substring(unsigned int from, unsigned int to) const { return from >= s_.size() || to <= from ? String() : String(s_.substr(from, to - from)); }
  void remove(unsigned int idx) { if (idx < s_.size()) s_.erase(idx); }
  void remove(unsigned int idx, unsigned int count) { if (idx < s_.size()) s_.erase(idx, count); }
  void replace(const String& a, const String& b);
  void trim();
  void toLowerCase();
  void toUpperCase();
  long toInt() const { return strtol(s_.c_str(), nullptr, 10); }
  float toFloat() const { return strtof(s_.c_str(), nullptr); }
  double toDouble() const { return strtod(s_.c_str(), nullptr); }

 private:
  std::string s_;
};
