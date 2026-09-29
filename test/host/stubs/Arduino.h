#pragma once

// Host stub of the Arduino core: only what json_util.h and recipes.h use.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  explicit String(char c) : s_(1, c) {}
  explicit String(unsigned char v) : s_(std::to_string(v)) {}
  explicit String(int v) : s_(std::to_string(v)) {}
  explicit String(unsigned v) : s_(std::to_string(v)) {}
  explicit String(long v) : s_(std::to_string(v)) {}
  explicit String(unsigned long v) : s_(std::to_string(v)) {}

  const char *c_str() const { return s_.c_str(); }
  unsigned length() const { return static_cast<unsigned>(s_.size()); }
  void reserve(unsigned n) { s_.reserve(n); }

  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *o) { s_ += o ? o : ""; return *this; }
  String &operator+=(char c) { s_ += c; return *this; }
  String &operator+=(unsigned char v) { s_ += std::to_string(v); return *this; }
  String &operator+=(int v) { s_ += std::to_string(v); return *this; }
  String &operator+=(unsigned v) { s_ += std::to_string(v); return *this; }
  String &operator+=(long v) { s_ += std::to_string(v); return *this; }
  String &operator+=(unsigned long v) { s_ += std::to_string(v); return *this; }

  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *o) const { return s_ == (o ? o : ""); }
  bool operator!=(const String &o) const { return s_ != o.s_; }
  bool operator!=(const char *o) const { return !(*this == o); }
  bool equalsIgnoreCase(const String &o) const { return strcasecmp(c_str(), o.c_str()) == 0; }

  int indexOf(char c, unsigned from = 0) const {
    const size_t i = s_.find(c, from);
    return i == std::string::npos ? -1 : static_cast<int>(i);
  }
  String substring(unsigned from, unsigned to) const {
    if (from > s_.size()) {
      return String();
    }
    return String(s_.substr(from, to > from ? to - from : 0));
  }
  bool startsWith(const char *p) const { return s_.rfind(p, 0) == 0; }
  bool endsWith(const char *p) const {
    const size_t n = strlen(p);
    return s_.size() >= n && s_.compare(s_.size() - n, n, p) == 0;
  }
  void remove(unsigned index) { s_.erase(index); }
  long toInt() const { return atol(c_str()); }

 private:
  std::string s_;
};

inline String operator+(const String &a, const String &b) {
  String r(a);
  r += b;
  return r;
}
inline String operator+(const String &a, const char *b) {
  String r(a);
  r += b;
  return r;
}
inline String operator+(const char *a, const String &b) {
  String r(a);
  r += b;
  return r;
}
