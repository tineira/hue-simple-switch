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
#define FIRMWARE_VERSION "0.2.10"
#endif

static const unsigned long kPollEmptyMs = 60UL * 1000UL;
static const unsigned long kPollArmedMs = 60UL * 60UL * 1000UL;

inline bool gConsoleRegistered = false;
inline unsigned long gConsoleLastPollMs = 0;
inline bool gConsolePolledBoot = false;
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

inline int consoleHttp(const char *method, const String &path, const char *body, String *response) {
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

  int code = -1;
  if (strcmp(method, "GET") == 0) {
    code = http.GET();
  } else {
    code = http.POST(body ? String(body) : String("{}"));
  }
  if (response) {
    *response = http.getString();
  }
  http.end();
  consoleNoteHttp(code);
  return code;
}

inline bool consoleRegister() {
  if (!consoleConfigured()) {
    return false;
  }
  if (!gHueBridgeId.length() || !gHueBridgeIp.length()) {
    LOGLN("console register skipped: no Bridge");
    return false;
  }

  String lights, rooms, scenes;
  if (!hueBuildSnapshot(&lights, &rooms, &scenes)) {
    LOGLN("console register skipped: snapshot failed (keep last good)");
    return false;
  }

  String payload;
  payload.reserve(lights.length() + rooms.length() + scenes.length() + 256);
  payload += "{\"mac\":";
  jsonAppendEscaped(payload, deviceMacHex().c_str());
  payload += ",\"firmware\":";
  jsonAppendEscaped(payload, FIRMWARE_VERSION);
  payload += ",\"bridgeid\":";
  jsonAppendEscaped(payload, gHueBridgeId.c_str());
  payload += ",\"bridge_ip\":";
  jsonAppendEscaped(payload, gHueBridgeIp.c_str());
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
  String path = "/api/device/config?mac=";
  path += deviceMacHex();
  String body;
  const int code = consoleHttp("GET", path, nullptr, &body);
  LOG("console GET config %d\n", code);
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
  recipesLock();
  if (gNvsEpoch != epoch) {
    recipesUnlock();
    return;
  }
  const uint32_t localRev = gRecipeRev;
  const uint8_t localCount = gRecipeCount;
  HueRecipe backup[kMaxRecipes];
  memcpy(backup, gRecipes, sizeof(backup));

  uint32_t rev = 0;
  if (!recipesParseConfig(body.c_str(), &rev)) {
    memcpy(gRecipes, backup, sizeof(backup));
    gRecipeCount = localCount;
    recipesUnlock();
    LOGLN("console config parse failed");
    return;
  }
  if (localRev >= rev) {
    memcpy(gRecipes, backup, sizeof(backup));
    gRecipeCount = localCount;
    recipesUnlock();
    LOG("console rev %u local %u — keep NVS\n", rev, localRev);
    return;
  }
  if (gNvsEpoch != epoch) {
    memcpy(gRecipes, backup, sizeof(backup));
    gRecipeCount = localCount;
    recipesUnlock();
    return;
  }
  gRecipeRev = rev;
  recipesSave();
  recipesUnlock();
  LOG("console rev %u — replaced %u recipes\n", gRecipeRev, gRecipeCount);
}

inline void consoleDoSync() {
  const uint32_t epoch = gNvsEpoch;
  if (!consoleConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (gNvsEpoch != epoch || !gHueBridgeId.length() || !hueLooksLikeKey(gHueAppKey)) {
    return;
  }
  if (recipesBindBridge(gHueBridgeId)) {
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
  if (!gConsoleRegistered || count > 0) {
    consoleRegister();
  }
  if (gNvsEpoch != epoch) {
    return;
  }
  consoleFetchConfig();
}

inline void consoleWorkerTask(void *) {
  for (;;) {
    bool run = false;
    if (gNeedConsoleSync) {
      gNeedConsoleSync = false;
      gConsoleRegistered = false;
      run = true;
    } else if (consoleConfigured() && WiFi.status() == WL_CONNECTED && gHueBridgeId.length() &&
               hueLooksLikeKey(gHueAppKey)) {
      recipesLock();
      const uint8_t count = gRecipeCount;
      recipesUnlock();
      const unsigned long interval = (count == 0) ? kPollEmptyMs : kPollArmedMs;
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
