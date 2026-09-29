#pragma once

#include <Arduino.h>
#include <Stream.h>
#include <string.h>
#include <stdlib.h>

// Extracts fields from JSON (Clip v2 / console). Accepts a space after `:`. Not a full parser.

inline void jsonAppendEscaped(String &out, const char *s) {
  out += '"';
  if (!s) {
    out += '"';
    return;
  }
  for (; *s; s++) {
    const char c = *s;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else if (static_cast<uint8_t>(c) < 0x20) {
      continue;
    } else {
      out += c;
    }
  }
  out += '"';
}

inline bool jsonGetString(const char *json, const char *key, char *out, size_t outSz) {
  if (!json || !key || !out || outSz < 2) {
    return false;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char *p = strstr(json, needle);
  if (!p) {
    return false;
  }
  p += strlen(needle);
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
    p++;
  }
  if (*p != '"') {
    return false;
  }
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < outSz) {
    if (*p == '\\' && p[1]) {
      p++;
      char c = *p++;
      if (c == 'n') {
        c = '\n';
      } else if (c == 't') {
        c = '\t';
      } else if (c == 'r') {
        c = '\r';
      }
      out[i++] = c;
    } else {
      out[i++] = *p++;
    }
  }
  out[i] = 0;
  return true;
}

inline bool jsonGetObjectString(const char *json, const char *objKey, const char *field, char *out,
                                size_t outSz) {
  if (!json || !objKey) {
    return false;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", objKey);
  const char *p = strstr(json, needle);
  if (!p) {
    return false;
  }
  p += strlen(needle);
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
    p++;
  }
  if (*p != '{') {
    return false;
  }
  return jsonGetString(p, field, out, outSz);
}

inline bool jsonHasKey(const char *json, const char *key) {
  if (!json || !key) {
    return false;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  return strstr(json, needle) != nullptr;
}

inline int jsonGetInt(const char *json, const char *key, int defVal) {
  if (!json || !key) {
    return defVal;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char *p = strstr(json, needle);
  if (!p) {
    return defVal;
  }
  p += strlen(needle);
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
    p++;
  }
  if (*p != '-' && (*p < '0' || *p > '9')) {
    return defVal;
  }
  return atoi(p);
}

inline bool jsonHueOn(const char *json, bool *on) {
  if (!json || !on) {
    return false;
  }
  const char *p = strstr(json, "\"on\"");
  while (p) {
    const char *q = p + 4;
    while (*q == ' ') {
      q++;
    }
    if (*q != ':') {
      p = strstr(p + 4, "\"on\"");
      continue;
    }
    q++;
    while (*q == ' ') {
      q++;
    }
    if (*q == '{') {
      const char *close = strchr(q, '}');
      const char *inner = strstr(q, "\"on\"");
      if (inner && (!close || inner < close)) {
        inner += 4;
        while (*inner == ' ') {
          inner++;
        }
        if (*inner == ':') {
          inner++;
          while (*inner == ' ') {
            inner++;
          }
          if (strncmp(inner, "true", 4) == 0) {
            *on = true;
            return true;
          }
          if (strncmp(inner, "false", 5) == 0) {
            *on = false;
            return true;
          }
        }
      }
    }
    p = strstr(p + 4, "\"on\"");
  }
  return false;
}

// dimming.brightness (0–100) of a light or grouped_light. Searched inside the first
// "dimming" object, so powerup.dimming further down is never read.
inline bool jsonHueBrightness(const char *json, float *bri) {
  if (!json || !bri) {
    return false;
  }
  const char *p = strstr(json, "\"dimming\"");
  if (!p) {
    return false;
  }
  p += 9;
  while (*p == ' ') {
    p++;
  }
  if (*p != ':') {
    return false;
  }
  p++;
  while (*p == ' ') {
    p++;
  }
  if (*p != '{') {
    return false;
  }
  const char *close = strchr(p, '}');
  const char *key = strstr(p, "\"brightness\"");
  if (!key || (close && key > close)) {
    return false;
  }
  key += 12;
  while (*key == ' ') {
    key++;
  }
  if (*key != ':') {
    return false;
  }
  key++;
  char *end = nullptr;
  const float v = strtof(key, &end);
  if (end == key) {
    return false;
  }
  *bri = v;
  return true;
}

// Rid whose paired rtype matches (e.g. grouped_light in services[]).
inline bool jsonFindRidByRtype(const char *json, const char *rtype, char *out, size_t outSz) {
  if (!json || !rtype || !out || outSz < 2) {
    return false;
  }
  char needle[64];
  snprintf(needle, sizeof(needle), "\"rtype\":\"%s\"", rtype);
  const char *hit = strstr(json, needle);
  if (!hit) {
    snprintf(needle, sizeof(needle), "\"rtype\": \"%s\"", rtype);
    hit = strstr(json, needle);
  }
  if (!hit) {
    return false;
  }
  const char *start = json;
  if (hit - json > 120) {
    start = hit - 120;
  }
  const char *ridKey = nullptr;
  for (const char *q = start; q < hit; q++) {
    if (strncmp(q, "\"rid\":\"", 7) == 0) {
      ridKey = q;
    }
  }
  if (!ridKey) {
    return false;
  }
  ridKey += 7;
  size_t i = 0;
  while (ridKey[i] && ridKey[i] != '"' && i + 1 < outSz) {
    out[i] = ridKey[i];
    i++;
  }
  out[i] = 0;
  return i > 0;
}

typedef void (*JsonObjFn)(const char *obj, void *ctx);

inline void jsonEachArrayObject(const char *json, const char *key, JsonObjFn fn, void *ctx) {
  if (!json || !key || !fn) {
    return;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char *p = strstr(json, needle);
  if (!p) {
    return;
  }
  p = strchr(p, '[');
  if (!p) {
    return;
  }
  p++;
  const char *start = nullptr;
  int depth = 0;
  bool inString = false;
  bool escape = false;
  for (; *p; p++) {
    const char c = *p;
    if (depth == 0 && !inString) {
      if (c == ']') {
        return;
      }
      if (c == '{') {
        depth = 1;
        start = p;
        inString = false;
        escape = false;
      }
      continue;
    }
    if (escape) {
      escape = false;
      continue;
    }
    if (inString) {
      if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
      continue;
    }
    if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;
      if (depth == 0 && start) {
        const size_t n = static_cast<size_t>(p - start + 1);
        char *tmp = static_cast<char *>(malloc(n + 1));
        if (tmp) {
          memcpy(tmp, start, n);
          tmp[n] = 0;
          fn(tmp, ctx);
          free(tmp);
        }
        start = nullptr;
      }
    }
  }
}

inline bool jsonStringField(const String &body, const char *key, String *out) {
  char buf[96];
  if (!jsonGetString(body.c_str(), key, buf, sizeof(buf)) || !buf[0]) {
    return false;
  }
  *out = buf;
  return true;
}

// Receives the HTTP body (chunked already decoded) and hands over each object of data[].
class JsonDataSink : public Stream {
 public:
  static const size_t kMaxObj = 20480;

  JsonObjFn onObject = nullptr;
  void *ctx = nullptr;
  int objects = 0;
  bool overflow = false;

  JsonDataSink() { buf_ = static_cast<char *>(malloc(kMaxObj)); }

  ~JsonDataSink() {
    free(buf_);
  }

  size_t write(uint8_t c) override {
    feed(static_cast<char>(c));
    return 1;
  }

  size_t write(const uint8_t *data, size_t size) override {
    for (size_t i = 0; i < size; i++) {
      feed(static_cast<char>(data[i]));
    }
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  enum State { kSeekData, kSeekColon, kSeekArray, kScan, kObject, kDone };

  char *buf_ = nullptr;
  size_t len_ = 0;
  int depth_ = 0;
  bool inString_ = false;
  bool escape_ = false;
  State state_ = kSeekData;
  uint8_t match_ = 0;
  // Clip v2 scene.actions can exceed kMaxObj; we don't copy it. The key is matched without its
  // terminating NUL (test/host/test_parsers.cpp covers a scene larger than kMaxObj).
  static constexpr const char kActionsKey[] = "\"actions\":";
  static constexpr size_t kActionsKeyLen = sizeof(kActionsKey) - 1;
  bool skippingActions_ = false;
  bool skipStarted_ = false;
  bool skipPrim_ = false;
  bool skipInString_ = false;
  bool skipEscape_ = false;
  int skipNest_ = 0;

  void finishSkipActions() {
    skippingActions_ = false;
    skipStarted_ = false;
    skipPrim_ = false;
    skipInString_ = false;
    skipEscape_ = false;
    skipNest_ = 0;
    if (buf_ && len_ + 2 < kMaxObj) {
      buf_[len_++] = '[';
      buf_[len_++] = ']';
    }
  }

  // true = this byte was already consumed (including a re-feed to the object).
  bool feedSkipActions(char c) {
    if (!skipStarted_) {
      if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
        return true;
      }
      skipStarted_ = true;
      if (c == '[' || c == '{') {
        skipNest_ = 1;
        return true;
      }
      if (c == '"') {
        skipInString_ = true;
        skipPrim_ = true;
        return true;
      }
      skipPrim_ = true;
      return true;
    }
    if (skipNest_ > 0) {
      if (skipEscape_) {
        skipEscape_ = false;
        return true;
      }
      if (skipInString_) {
        if (c == '\\') {
          skipEscape_ = true;
        } else if (c == '"') {
          skipInString_ = false;
        }
        return true;
      }
      if (c == '"') {
        skipInString_ = true;
        return true;
      }
      if (c == '[' || c == '{') {
        skipNest_++;
      } else if (c == ']' || c == '}') {
        skipNest_--;
        if (skipNest_ == 0) {
          finishSkipActions();
        }
      }
      return true;
    }
    if (skipPrim_) {
      if (skipEscape_) {
        skipEscape_ = false;
        return true;
      }
      if (skipInString_) {
        if (c == '\\') {
          skipEscape_ = true;
        } else if (c == '"') {
          skipInString_ = false;
        }
        return true;
      }
      if (c == ',' || c == '}' || c == ']') {
        finishSkipActions();
        return false;
      }
      return true;
    }
    return true;
  }

  void feed(char c) {
    switch (state_) {
      case kSeekData: {
        static const char kKey[] = "\"data\"";
        if (c == kKey[match_]) {
          match_++;
          if (kKey[match_] == 0) {
            state_ = kSeekColon;
            match_ = 0;
          }
        } else {
          match_ = (c == kKey[0]) ? 1 : 0;
        }
        break;
      }
      case kSeekColon:
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
          break;
        }
        if (c == ':') {
          state_ = kSeekArray;
        } else {
          state_ = kSeekData;
        }
        break;
      case kSeekArray:
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
          break;
        }
        if (c == '[') {
          state_ = kScan;
        } else {
          state_ = kSeekData;
        }
        break;
      case kScan:
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ',') {
          break;
        }
        if (c == ']') {
          state_ = kDone;
          break;
        }
        if (c == '{') {
          if (!buf_) {
            overflow = true;
            break;
          }
          state_ = kObject;
          depth_ = 1;
          len_ = 1;
          buf_[0] = '{';
          inString_ = false;
          escape_ = false;
          skippingActions_ = false;
          skipStarted_ = false;
          skipPrim_ = false;
          skipInString_ = false;
          skipEscape_ = false;
          skipNest_ = 0;
        }
        break;
      case kObject: {
        if (skippingActions_ && feedSkipActions(c)) {
          break;
        }
        if (buf_ && len_ + 1 < kMaxObj) {
          buf_[len_++] = c;
        } else {
          overflow = true;
        }
        if (escape_) {
          escape_ = false;
          break;
        }
        if (inString_) {
          if (c == '\\') {
            escape_ = true;
          } else if (c == '"') {
            inString_ = false;
          }
          break;
        }
        if (c == '"') {
          inString_ = true;
          break;
        }
        if (c == ':' && len_ >= kActionsKeyLen &&
            memcmp(buf_ + len_ - kActionsKeyLen, kActionsKey, kActionsKeyLen) == 0) {
          skippingActions_ = true;
          skipStarted_ = false;
          skipPrim_ = false;
          skipInString_ = false;
          skipEscape_ = false;
          skipNest_ = 0;
          break;
        }
        if (c == '{') {
          depth_++;
        } else if (c == '}') {
          depth_--;
          if (depth_ == 0) {
            if (buf_ && !overflow && onObject) {
              buf_[len_] = 0;
              onObject(buf_, ctx);
              objects++;
            }
            overflow = false;
            skippingActions_ = false;
            state_ = kScan;
            len_ = 0;
          }
        }
        break;
      }
      case kDone:
        break;
    }
  }
};
