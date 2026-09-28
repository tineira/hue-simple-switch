#pragma once

// Update over Wi-Fi (console docs/specs/ota.md §4). Console task only, except otaBootCheck (setup).
//
// A poll's 200 body may carry `ota` { version, url, sha256, size }. The console task parses it
// before the rev check (the body is dropped first), then, after the poll's connection is closed,
// downloads the app image with the same kind of client and writes it to the inactive slot while
// hashing it. Only a matching length and sha256 reach Update.end(), which makes the slot bootable.
// NVS is never erased. The offer is not stored: it is applied from the poll that carried it, so
// a cancelled offer is simply absent on the next poll.
//
// Confirming: verifyRollbackLater() (the .ino) keeps the core from confirming the image at
// startup; otaConfirm() confirms it after the first 200/204 poll. A restart before that makes the
// bootloader go back to the old slot. NVS ota/try holds the version being installed from just
// before the restart until it confirms; the old app finds it with the last invalid partition set
// and reports ota_error=boot once.
//
// Heap: measured with a Hue call in flight during the download (spec §6): the largest free block
// stayed at 118 KB or more, and one TLS session needs about 50 KB.

#include <Update.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include "json_util.h"

struct OtaOffer {
  char version[16];
  char url[128];
  char sha256[65];
  uint32_t size;
};

// Retry a failed offer at most this often; size and sha block that version until reboot.
static const unsigned long kOtaRetryMs = 60UL * 60UL * 1000UL;
// Largest free block needed to start: one TLS session plus the 4 KB write buffer, with room.
static const size_t kOtaMinBlock = 64 * 1024;
static const unsigned long kOtaStallMs = 20000;

inline bool gOtaOfferValid = false;  // set by the poll that carried `ota`
inline OtaOffer gOtaOffer;
// Sent as ota_error on the next poll, then dropped once the console answered 200/204.
inline char gOtaErrorPending[12] = "";
inline char gOtaFailedVersion[16] = "";
inline unsigned long gOtaFailedMs = 0;
inline char gOtaBlockedVersion[16] = "";  // after size or sha: the image itself is bad
inline bool gOtaConfirmed = false;

inline void otaFail(const char *version, const char *code, bool block) {
  LOG("ota %s failed: %s\n", version, code);
  snprintf(gOtaErrorPending, sizeof(gOtaErrorPending), "%s", code);
  snprintf(gOtaFailedVersion, sizeof(gOtaFailedVersion), "%s", version);
  gOtaFailedMs = millis();
  if (block) {
    snprintf(gOtaBlockedVersion, sizeof(gOtaBlockedVersion), "%s", version);
  }
}

// Setup: a restart before the new image confirmed itself sent the bootloader back here.
inline void otaBootCheck() {
  Preferences p;
  if (!p.begin("ota", true)) {
    return;  // namespace never written: no update tried
  }
  const String tried = p.getString("try", "");
  p.end();
  if (!tried.length() || tried == FIRMWARE_VERSION) {
    return;  // nothing tried, or this is the new image (it clears the key once it confirms)
  }
  if (esp_ota_get_last_invalid_partition()) {
    otaFail(tried.c_str(), "boot", false);
  } else {
    LOG("ota %s: another image runs now, not a rollback\n", tried.c_str());
  }
  if (p.begin("ota", false)) {
    p.remove("try");
    p.end();
  }
}

// After the first 200/204 poll: this image works, keep it.
inline void otaConfirm() {
  if (gOtaConfirmed) {
    return;
  }
  gOtaConfirmed = true;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    LOG("ota: %s confirmed (%d)\n", FIRMWARE_VERSION, static_cast<int>(err));
  }
  Preferences p;
  if (p.begin("ota", true)) {
    const bool pending = p.isKey("try");
    p.end();
    if (pending && p.begin("ota", false)) {
      p.remove("try");
      p.end();
    }
  }
}

// Query suffix for the poll: firmware always, ota_error once.
inline void otaAppendQuery(String &path) {
  path += "&firmware=";
  path += FIRMWARE_VERSION;
  if (gOtaErrorPending[0]) {
    path += "&ota_error=";
    path += gOtaErrorPending;
  }
}

// The console answered 200/204: it has the version and the error.
inline void otaPollAccepted() {
  gOtaErrorPending[0] = 0;
  otaConfirm();
}

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

// Loop and Hue worker state, read without a lock: a stale read only moves the start by a poll.
inline bool otaInputsQuiet() { return !gInputsBusy && hueWorkerIdle(); }

inline bool otaShouldStart(const OtaOffer &o) {
  if (strcmp(o.version, FIRMWARE_VERSION) == 0) {
    return false;
  }
  if (strcmp(o.version, gOtaBlockedVersion) == 0) {
    LOG("ota %s: not retried until reboot\n", o.version);
    return false;
  }
  if (strcmp(o.version, gOtaFailedVersion) == 0 && millis() - gOtaFailedMs < kOtaRetryMs) {
    LOG("ota %s: retry after an hour\n", o.version);
    return false;
  }
  if (!otaInputsQuiet()) {
    LOGLN("ota: input or Hue call busy, next poll");
    return false;
  }
  return true;
}

inline void otaHex(const uint8_t *d, char *out) {
  for (int i = 0; i < 32; i++) {
    snprintf(out + i * 2, 3, "%02x", d[i]);
  }
}

// Streams the image into the inactive slot. true: written, checked and bootable.
inline bool otaDownload(const OtaOffer &o) {
  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  if (!slot || o.size > slot->size) {
    otaFail(o.version, "size", true);
    return false;
  }
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kOtaMinBlock) {
    otaFail(o.version, "heap", false);
    return false;
  }
  const String url = consoleBaseUrl() + o.url;
  HTTPClient http;
  http.setTimeout(15000);
  NetworkClientSecure secure;
  NetworkClient plain;
  bool began = false;
  if (url.startsWith("https://")) {
    secure.useBuiltinCACertBundle();
    began = http.begin(secure, url);
  } else {
    began = http.begin(plain, url);
  }
  if (!began) {
    otaFail(o.version, "connect", false);
    return false;
  }
  const int code = http.GET();
  LOG("ota GET %s %d\n", o.url, code);
  if (code <= 0) {
    http.end();
    otaFail(o.version, "connect", false);
    return false;
  }
  if (code != HTTP_CODE_OK) {
    http.end();
    otaFail(o.version, "http", false);
    return false;
  }
  const int len = http.getSize();
  if (len != static_cast<int>(o.size)) {
    LOG("ota length %d, offer %u\n", len, static_cast<unsigned>(o.size));
    http.end();
    otaFail(o.version, "size", true);
    return false;
  }
  uint8_t *buf = static_cast<uint8_t *>(malloc(4096));
  if (!buf) {
    http.end();
    otaFail(o.version, "heap", false);
    return false;
  }
  if (!Update.begin(o.size, U_FLASH)) {
    LOG("ota Update.begin error %u\n", Update.getError());
    free(buf);
    http.end();
    otaFail(o.version, "write", false);
    return false;
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  NetworkClient *s = http.getStreamPtr();
  size_t got = 0;
  bool writeFailed = false;
  unsigned long lastData = millis();
  const unsigned long t0 = lastData;
  while (got < o.size && http.connected()) {
    const int avail = s->available();
    if (avail <= 0) {
      if (millis() - lastData > kOtaStallMs) {
        LOGLN("ota: download stalled");
        break;
      }
      delay(2);
      continue;
    }
    size_t want = static_cast<size_t>(avail);
    if (want > 4096) {
      want = 4096;
    }
    if (want > o.size - got) {
      want = o.size - got;
    }
    const int n = s->read(buf, want);
    if (n <= 0) {
      continue;
    }
    lastData = millis();
    mbedtls_sha256_update(&sha, buf, n);
    if (Update.write(buf, n) != static_cast<size_t>(n)) {
      LOG("ota Update.write error %u\n", Update.getError());
      writeFailed = true;
      break;
    }
    got += n;
  }
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  free(buf);
  http.end();
  LOG("ota %u of %u bytes in %lu ms\n", static_cast<unsigned>(got), static_cast<unsigned>(o.size), millis() - t0);
  if (writeFailed) {
    Update.abort();
    otaFail(o.version, "write", false);
    return false;
  }
  if (got != o.size) {
    // The stream ended early (Wi-Fi, server): reported as size, but the image is not known bad.
    Update.abort();
    otaFail(o.version, "size", false);
    return false;
  }
  char hex[65];
  otaHex(digest, hex);
  if (strcmp(hex, o.sha256) != 0) {
    LOG("ota sha256 %s, offer %s\n", hex, o.sha256);
    Update.abort();
    otaFail(o.version, "sha", true);
    return false;
  }
  if (!Update.end()) {
    LOG("ota Update.end error %u\n", Update.getError());
    otaFail(o.version, "write", false);
    return false;
  }
  return true;
}

// After the poll that carried the offer, its connection closed and its body freed.
inline void otaMaybeApply() {
  if (!gOtaOfferValid) {
    return;
  }
  gOtaOfferValid = false;
  const OtaOffer o = gOtaOffer;
  if (!otaShouldStart(o)) {
    return;
  }
  LOG("ota: %s -> %s (%u bytes)\n", FIRMWARE_VERSION, o.version, static_cast<unsigned>(o.size));
  if (!otaDownload(o)) {
    return;
  }
  Preferences p;
  if (p.begin("ota", false)) {
    p.putString("try", o.version);
    p.end();
  }
  LOG("ota: %s written, restarting\n", o.version);
  delay(200);
  ESP.restart();
}
