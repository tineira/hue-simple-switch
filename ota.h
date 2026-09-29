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
// Interrupted download: NVS ota/dl holds the version from just before the slot is first written
// until the attempt ends either way. Found at boot (power cut, crash or reset mid-download), it is
// reported as ota_error=size (fewer bytes than ota.size) and retried after an hour, like a stream
// that ended early.
//
// The `ota` block itself is parsed in ota_offer.h (no device includes, covered by test/host).
//
// Heap: measured with a Hue call in flight during the download (spec §6): the largest free block
// stayed at 118 KB or more, and one TLS session needs about 50 KB.

#include <Update.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include "ota_offer.h"

// Retry a failed offer at most this often; size and sha block that version until reboot.
static const unsigned long kOtaRetryMs = 60UL * 60UL * 1000UL;
// Largest free block needed to start: one TLS session plus the 4 KB write buffer, with room.
static const size_t kOtaMinBlock = 64 * 1024;
static const unsigned long kOtaStallMs = 20000;

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

inline void otaMarkDownload(const char *version) {
  Preferences p;
  if (p.begin("ota", false)) {
    if (version) {
      p.putString("dl", version);
    } else if (p.isKey("dl")) {
      p.remove("dl");
    }
    p.end();
  }
}

// Setup: a download cut off by a restart, or a restart before the new image confirmed itself
// (the bootloader then went back to this one).
inline void otaBootCheck() {
  Preferences p;
  if (!p.begin("ota", true)) {
    return;  // namespace never written: no update tried
  }
  const String dl = p.getString("dl", "");
  const String tried = p.getString("try", "");
  p.end();
  // tried == FIRMWARE_VERSION: this is the new image; it clears the key once it confirms.
  const bool rolledBack = tried.length() && tried != FIRMWARE_VERSION;
  if (!dl.length() && !rolledBack) {
    return;
  }
  if (dl.length()) {
    otaFail(dl.c_str(), "size", false);
  }
  if (rolledBack) {
    if (esp_ota_get_last_invalid_partition()) {
      otaFail(tried.c_str(), "boot", false);
    } else {
      LOG("ota %s: another image runs now, not a rollback\n", tried.c_str());
    }
  }
  if (p.begin("ota", false)) {
    if (dl.length()) {
      p.remove("dl");
    }
    if (rolledBack) {
      p.remove("try");
    }
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
  otaMarkDownload(o.version);
  if (!Update.begin(o.size, U_FLASH)) {
    otaMarkDownload(nullptr);
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
  otaMarkDownload(nullptr);
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
