#pragma once

// Host stub of the Arduino Print / Stream bases that JsonDataSink derives from.

#include "Arduino.h"

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t *data, size_t size) {
    size_t n = 0;
    while (size--) {
      n += write(*data++);
    }
    return n;
  }
  virtual void flush() {}
};

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
};
