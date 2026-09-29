#pragma once

// The `ota` block of a config poll's 200 body (console docs/specs/ota.md): parsed into gOtaOffer.
// No device includes, so test/host builds it; the download and install are in ota.h. LOG and
// LOGLN come from the includer (log.h on the board).

#include <stdint.h>
#include <string.h>
#include "json_util.h"

struct OtaOffer {
  char version[16];
  char url[128];
  char sha256[65];
  uint32_t size;
};

inline bool gOtaOfferValid = false;  // set by the poll that carried `ota`
inline OtaOffer gOtaOffer;

inline bool otaLooksLikeVersion(const char *v) {
  int dots = 0;
  if (!v[0]) {
    return false;
  }
  for (const char *p = v; *p; p++) {
    if (*p == '.') {
      dots++;
    } else if (*p < '0' || *p > '9') {
      return false;
    }
  }
  return dots == 2;
}

inline bool otaLooksLikeSha(const char *s) {
  if (strlen(s) != 64) {
    return false;
  }
  for (const char *p = s; *p; p++) {
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) {
      return false;
    }
  }
  return true;
}

// Any 200 body, whatever its rev. Absent or malformed `ota`: no offer.
inline void otaParseOffer(const char *body) {
  gOtaOfferValid = false;
  const char *p = body ? strstr(body, "\"ota\":") : nullptr;
  if (!p) {
    return;
  }
  p += 6;
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
    p++;
  }
  // No nested objects and no braces in its strings: the block ends at the first '}'.
  const char *end = (*p == '{') ? strchr(p, '}') : nullptr;
  if (!end || end - p > 400) {
    LOGLN("ota: offer not an object");
    return;
  }
  char block[402];
  const size_t n = static_cast<size_t>(end - p + 1);
  memcpy(block, p, n);
  block[n] = 0;
  OtaOffer o;
  memset(&o, 0, sizeof(o));
  const int size = jsonGetInt(block, "size", -1);
  if (!jsonGetString(block, "version", o.version, sizeof(o.version)) ||
      !jsonGetString(block, "url", o.url, sizeof(o.url)) ||
      !jsonGetString(block, "sha256", o.sha256, sizeof(o.sha256)) || size <= 0 ||
      !otaLooksLikeVersion(o.version) || o.url[0] != '/' || !otaLooksLikeSha(o.sha256)) {
    LOGLN("ota: offer malformed, ignored");
    return;
  }
  o.size = static_cast<uint32_t>(size);
  gOtaOffer = o;
  gOtaOfferValid = true;
}
