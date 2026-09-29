#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "WString.h"

class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper*>(s))

class Print;
class Printable {
 public:
  virtual ~Printable() {}
  virtual size_t printTo(Print& p) const = 0;
};

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (size--) n += write(*buffer++);
    return n;
  }
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
  size_t write(const char* s, size_t size) { return write((const uint8_t*)s, size); }
  virtual void flush() {}

  size_t print(const char* s) { return write(s); }
  size_t print(const String& s) { return write(s.c_str()); }
  size_t print(const __FlashStringHelper* s) { return write(reinterpret_cast<const char*>(s)); }
  size_t print(const Printable& p) { return p.printTo(*this); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v, int base = 10) { return printNumber((long)v, base); }
  size_t print(unsigned int v, int base = 10) { return printNumber((unsigned long)v, base); }
  size_t print(long v, int base = 10) { return printNumber(v, base); }
  size_t print(unsigned long v, int base = 10) { return printNumber(v, base); }
  size_t print(long long v, int base = 10) { return printNumber((long)v, base); }
  size_t print(unsigned long long v, int base = 10) { return printNumber((unsigned long)v, base); }
  size_t print(double v, int digits = 2) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%.*f", digits, v);
    return write(buf);
  }
  size_t println() { return write("\r\n"); }
  template <typename T> size_t println(const T& v) { size_t n = print(v); return n + println(); }
  size_t println(int v, int base) { size_t n = print(v, base); return n + println(); }
  size_t println(unsigned int v, int base) { size_t n = print(v, base); return n + println(); }
  size_t println(unsigned long v, int base) { size_t n = print(v, base); return n + println(); }
  size_t println(double v, int digits) { size_t n = print(v, digits); return n + println(); }

  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char stackBuf[256];
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(stackBuf, sizeof(stackBuf), fmt, ap);
    va_end(ap);
    size_t n = 0;
    if (len < 0) {
      n = 0;
    } else if ((size_t)len < sizeof(stackBuf)) {
      n = write((const uint8_t*)stackBuf, (size_t)len);
    } else {
      char* heap = (char*)malloc((size_t)len + 1);
      if (heap) {
        vsnprintf(heap, (size_t)len + 1, fmt, ap2);
        n = write((const uint8_t*)heap, (size_t)len);
        free(heap);
      }
    }
    va_end(ap2);
    return n;
  }

 private:
  size_t printNumber(long v, int base) {
    char buf[40];
    if (base == 16) snprintf(buf, sizeof(buf), "%lx", v);
    else if (base == 8) snprintf(buf, sizeof(buf), "%lo", v);
    else if (base == 2) {
      unsigned long u = (unsigned long)v; int i = 0; char t[40];
      if (!u) t[i++] = '0';
      while (u) { t[i++] = '0' + (u & 1); u >>= 1; }
      int j = 0; while (i) buf[j++] = t[--i]; buf[j] = 0;
    } else snprintf(buf, sizeof(buf), "%ld", v);
    return write(buf);
  }
};
