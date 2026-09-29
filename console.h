#pragma once

#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "channels.h"
#include "recipes.h"
#include "snapshot.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.6.3"
#endif

// Fallback cadence when the console sends no X-Poll-Sec (older console).
static const unsigned long kPollEmptyMs = 60UL * 1000UL;
static const unsigned long kPollArmedMs = 60UL * 60UL * 1000UL;
// X-Poll-Sec is clamped to this range.
static const unsigned long kPollMinSec = 30UL;
static const unsigned long kPollMaxSec = 3600UL;

inline bool gConsoleRegistered = false;
inline unsigned long gConsoleLastPollMs = 0;
inline bool gConsolePolledBoot = false;
// The loop's boot sync after Wi-Fi came up (afterWifiUp) was asked for, or it gave up because the
// Bridge did not answer. Until then polls fetch config only: the first poll runs while
// afterWifiUp is still checking the Bridge, and a register there would be followed by a second
// one from the sync that afterWifiUp asks for.
inline volatile bool gConsoleBootSyncDone = false;
// Delay the console last asked for (X-Poll-Sec), or the maximum after a 401. 0 = the fallback
// above. Only 200, 204 and 401 change it; other errors keep it.
inline unsigned long gConsolePollMs = 0;
// NVS took a new rev: poll once more right away so the console sees it applied.
inline bool gConsoleConfirmPoll = false;
inline unsigned long gConsoleLastRegisterMs = 0;
inline String gConsoleUrlNvs;
inline String gConsoleTokenNvs;

// Console 401, sticky in RAM (not NVS). Cleared at boot.
inline volatile bool gConsoleAuthRejected = false;
inline volatile bool gConsoleConfiguredOk = false;
inline volatile bool gConsoleTaskFailed = false;

inline void consoleRefreshConfigured();
inline void consoleNoteHttp(int code);

// Token and URL only come from NVS console (HUESET from the web console).
inline String consoleUrl() { return gConsoleUrlNvs; }

inline String consoleToken() { return gConsoleTokenNvs; }

inline void consoleLoadNvs() {
  Preferences p;
  if (p.begin("console", true)) {
    gConsoleUrlNvs = p.getString("url", "");
    gConsoleTokenNvs = p.getString("token", "");
    p.end();
  }
  consoleRefreshConfigured();
}

inline bool consoleSaveNvs(const char *key, const char *val) {
  if (!key || !val || !val[0]) {
    return false;
  }
  Preferences p;
  if (!p.begin("console", false)) {
    return false;
  }
  const size_t n = p.putString(key, val);
  p.end();
  return n > 0;
}

inline bool consoleSetUrl(const char *url) {
  if (!consoleSaveNvs("url", url)) {
    return false;
  }
  gConsoleUrlNvs = url;
  consoleRefreshConfigured();
  return true;
}

inline bool consoleSetToken(const char *tok) {
  if (!consoleSaveNvs("token", tok)) {
    return false;
  }
  gConsoleTokenNvs = tok;
  // A new HUESET token clears the 401 even before the next response arrives.
  gConsoleAuthRejected = false;
  consoleRefreshConfigured();
  return true;
}

// Minted keys are hsw_ plus base64url. Anything else is not a console token.
inline bool consoleLooksLikeToken(const String &tok) {
  if (!tok.startsWith("hsw_")) {
    return false;
  }
  if (tok.length() < 20 || tok.length() > 80) {
    return false;
  }
  for (unsigned i = 4; i < tok.length(); i++) {
    const char c = tok[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

inline bool consoleConfigured() {
  const String url = consoleUrl();
  const String tok = consoleToken();
  if (!url.length() || !consoleLooksLikeToken(tok)) {
    return false;
  }
  return true;
}

inline void consoleRefreshConfigured() {
  gConsoleConfiguredOk = consoleConfigured();
}

inline void consoleForgetSaved() {
  Preferences p;
  if (p.begin("console", false)) {
    p.clear();
    p.end();
  }
  gConsoleUrlNvs = "";
  gConsoleTokenNvs = "";
  gConsoleRegistered = false;
  gConsoleAuthRejected = false;
  consoleRefreshConfigured();
}

inline void consoleNoteHttp(int code) {
  if (code == HTTP_CODE_UNAUTHORIZED) {
    gConsoleAuthRejected = true;
    return;
  }
  // code <= 0 (timeout, -1, no Wi-Fi) does not clear it.
  if (code > 0) {
    gConsoleAuthRejected = false;
  }
}

inline String deviceMacHex() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[13];
  snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

inline String consoleBaseUrl() {
  String url = consoleUrl();
  while (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  return url;
}

// pollSec (optional): X-Poll-Sec from the response, 0 when missing or not a number.
inline int consoleHttp(const char *method, const String &path, const char *body, String *response,
                       unsigned long *pollSec = nullptr) {
  const String url = consoleBaseUrl() + path;
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
    return -1;
  }

  http.addHeader("Authorization", String("Bearer ") + consoleToken());
  http.addHeader("Content-Type", "application/json");
  if (pollSec) {
    static const char *kPollHeader[] = {"X-Poll-Sec"};
    http.collectHeaders(kPollHeader, 1);
  }

  int code = -1;
  if (strcmp(method, "GET") == 0) {
    code = http.GET();
  } else {
    code = http.POST(body ? String(body) : String("{}"));
  }
  // 204 has no body; reading one would wait for the socket to close.
  if (response && code != HTTP_CODE_NO_CONTENT) {
    *response = http.getString();
  }
  if (pollSec) {
    const long sec = http.header("X-Poll-Sec").toInt();
    *pollSec = sec > 0 ? static_cast<unsigned long>(sec) : 0;
  }
  http.end();
  consoleNoteHttp(code);
  return code;
}

#include "ota.h"

inline bool consoleRegister(const HueCreds &c) {
  if (!consoleConfigured()) {
    return false;
  }
  if (!c.bid[0] || !c.ip[0]) {
    LOGLN("console register skipped: no Bridge");
    return false;
  }

  String lights, rooms, scenes;
  if (!hueBuildSnapshot(c, &lights, &rooms, &scenes)) {
    LOGLN("console register skipped: snapshot failed (keep last good)");
    return false;
  }

  String payload;
  // Fixed part: ~150 bytes of ids plus ~40 per channel (7 channels), with room to spare, so the
  // big snapshot String is not reallocated while it is appended.
  payload.reserve(lights.length() + rooms.length() + scenes.length() + 512);
  payload += "{\"mac\":";
  jsonAppendEscaped(payload, deviceMacHex().c_str());
  payload += ",\"firmware\":";
  jsonAppendEscaped(payload, FIRMWARE_VERSION);
  payload += ",\"bridgeid\":";
  jsonAppendEscaped(payload, c.bid);
  payload += ",\"bridge_ip\":";
  jsonAppendEscaped(payload, c.ip);
  payload += ",\"product\":\"simple\",\"source\":\"xiao\",\"channels\":";
  channelsAppendJson(payload);
  payload += ",\"lights\":";
  payload += lights;
  payload += ",\"rooms\":";
  payload += rooms;
  payload += ",\"scenes\":";
  payload += scenes;
  payload += '}';

  String body;
  const int code = consoleHttp("POST", "/api/device/register", payload.c_str(), &body);
  LOG("console POST register %d\n", code);
  if (code != HTTP_CODE_OK) {
    LOGLN(body);
    return false;
  }
  gConsoleRegistered = true;
  LOG("console registered mac=%s\n", deviceMacHex().c_str());
  return true;
}

inline void consoleFetchConfig() {
  const uint32_t epoch = gNvsEpoch;
  if (!consoleConfigured()) {
    return;
  }
  recipesLock();
  const uint32_t sentRev = gRecipeRev;
  recipesUnlock();
  String path = "/api/device/config?mac=";
  path += deviceMacHex();
  path += "&rev=";
  path += sentRev;
  otaAppendQuery(path);
  String body;
  unsigned long pollSec = 0;
  const int code = consoleHttp("GET", path, nullptr, &body, &pollSec);
  LOG("console GET config %d rev=%u poll=%lus\n", code, static_cast<unsigned>(sentRev), pollSec);
  if (code == HTTP_CODE_OK || code == HTTP_CODE_NO_CONTENT) {
    // No header (older console): back to the fallback cadence.
    gConsolePollMs = pollSec ? constrain(pollSec, kPollMinSec, kPollMaxSec) * 1000UL : 0;
    otaPollAccepted();
  } else if (code == HTTP_CODE_UNAUTHORIZED) {
    gConsolePollMs = kPollMaxSec * 1000UL;
  }
  // Other errors (5xx, network) keep the last delay, so one failure does not push the next poll out.
  if (code == HTTP_CODE_NO_CONTENT) {
    return;  // Nothing newer than sentRev: keep NVS.
  }
  if (code != HTTP_CODE_OK) {
    if (code == HTTP_CODE_UNAUTHORIZED) {
      LOGLN("console unauthorized — NVS recipes kept");
    }
    if (body.length()) {
      LOGLN(body);
    }
    return;
  }

  if (gNvsEpoch != epoch) {
    return;
  }
  // Whatever the rev: while an update is offered the console answers 200 with an unchanged rev.
  otaParseOffer(body.c_str());
  // Parse into the stage buffer (only this task writes it), then swap under the lock.
  uint32_t rev = 0;
  if (!recipesParseConfig(body.c_str(), &rev)) {
    LOGLN("console config parse failed");
    return;
  }
  recipesLock();
  if (gNvsEpoch != epoch) {
    recipesUnlock();
    return;
  }
  const uint32_t localRev = gRecipeRev;
  if (localRev >= rev) {
    recipesUnlock();
    LOG("console rev %u local %u — keep NVS\n", rev, localRev);
    return;
  }
  recipesApply(gRecipeStage);
  gRecipeRev = rev;
  const bool saved = recipesSave();
  recipesUnlock();
  gConsoleConfirmPoll = saved;
  if (!saved) {
    LOGLN("console config NVS write failed");
  }
  LOG("console rev %u — replaced %u recipes, channels=%s\n", gRecipeRev, gRecipeCount,
      gChannelsFromConsole ? "console" : "defaults");
}

// A Bridge id and a usable key in the loop's published copy.
inline bool consoleBridgeKnown(const HueCreds &c) {
  return c.bid[0] && hueLooksLikeKey(String(c.key));
}

inline void consoleDoSync() {
  const uint32_t epoch = gNvsEpoch;
  if (!consoleConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  // The loop can re-pair or clear the Bridge meanwhile: this sync keeps the copy it started with.
  const HueCreds creds = hueCredsCopy();
  if (gNvsEpoch != epoch || !consoleBridgeKnown(creds)) {
    return;
  }
  if (recipesBindBridge(String(creds.bid))) {
    gConsoleRegistered = false;
  }
  if (gNvsEpoch != epoch) {
    return;
  }
  recipesLock();
  const uint8_t count = gRecipeCount;
  recipesUnlock();
  if (gNvsEpoch != epoch) {
    return;
  }
  // With recipes, refresh the topology at most hourly, however often the console asks for a poll.
  // Timed from the poll start, so the hourly fallback poll still registers every time.
  const unsigned long now = gConsoleLastPollMs;
  if (gConsoleBootSyncDone &&
      (!gConsoleRegistered || (count > 0 && now - gConsoleLastRegisterMs >= kPollArmedMs))) {
    gConsoleLastRegisterMs = now;
    consoleRegister(creds);
  }
  if (gNvsEpoch != epoch) {
    return;
  }
  consoleFetchConfig();
  // The poll's connection is closed and its body freed: the download runs alone (spec §4.1).
  if (gNvsEpoch == epoch && consoleConfigured()) {
    otaMaybeApply();
  }
  gOtaOfferValid = false;
}

inline void consoleWorkerTask(void *) {
  for (;;) {
    bool run = false;
    if (gNeedConsoleSync) {
      gNeedConsoleSync = false;
      gConsoleBootSyncDone = true;
      gConsoleRegistered = false;
      gConsoleConfirmPoll = false;
      run = true;
    } else if (gConsoleConfirmPoll) {
      gConsoleConfirmPoll = false;
      run = true;
    } else if (consoleConfigured() && WiFi.status() == WL_CONNECTED &&
               consoleBridgeKnown(hueCredsCopy())) {
      recipesLock();
      const uint8_t count = gRecipeCount;
      recipesUnlock();
      const unsigned long fallback = (count == 0) ? kPollEmptyMs : kPollArmedMs;
      const unsigned long interval = gConsolePollMs ? gConsolePollMs : fallback;
      const unsigned long now = millis();
      if (!gConsolePolledBoot || (now - gConsoleLastPollMs) >= interval) {
        run = true;
      }
    }
    if (run) {
      gConsoleLastPollMs = millis();
      gConsolePolledBoot = true;
      consoleDoSync();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

inline TaskHandle_t gConsoleTask = nullptr;

inline void consoleWorkerBegin() {
  if (gConsoleTask) {
    return;
  }
  const BaseType_t ok = xTaskCreate(consoleWorkerTask, "console", 16384, nullptr, 1, &gConsoleTask);
  if (ok != pdPASS) {
    gConsoleTask = nullptr;
    gConsoleTaskFailed = true;
    LOGLN("console task failed");
    return;
  }
  gConsoleTaskFailed = false;
}
