#pragma once

// Host stub: NVS is never available, so the save and load paths fail cleanly. The tests only
// cover parsing.

#include "Arduino.h"

class Preferences {
 public:
  bool begin(const char *, bool = false) { return false; }
  void end() {}
  bool clear() { return false; }
  bool isKey(const char *) { return false; }
  bool remove(const char *) { return false; }
  size_t getBytesLength(const char *) { return 0; }
  size_t getBytes(const char *, void *, size_t) { return 0; }
  size_t putBytes(const char *, const void *, size_t) { return 0; }
  String getString(const char *, const String &def = String()) { return def; }
  size_t putString(const char *, const String &) { return 0; }
  uint32_t getUInt(const char *, uint32_t def = 0) { return def; }
  size_t putUInt(const char *, uint32_t) { return 0; }
};
