#pragma once
#include "Print.h"

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  size_t readBytes(char* buffer, size_t length) {
    size_t n = 0;
    while (n < length) {
      int c = read();
      if (c < 0) break;
      buffer[n++] = (char)c;
    }
    return n;
  }
  void setTimeout(unsigned long) {}
};
