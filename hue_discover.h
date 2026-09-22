#pragma once

#include <ESPmDNS.h>
#include <Preferences.h>
#include "hue.h"
#include "json_util.h"

// Descubrir el Bridge (mDNS _hue._tcp) y emparejar la application key.
// IP y key se guardan en NVS para que un cambio de DHCP no pida recompilar.

static const unsigned long kPairTimeoutMs = 90000;
static const unsigned long kLongPressMs = 3000;

inline String gHueBridgeId;
inline void (*gOnHueWait)() = nullptr;

// Definido en led.h. El .ino lo incluye después; el linker resuelve la llamada.
bool ledPoll(unsigned long now);

inline bool hueLooksLikeIp(const String &s) {
  if (s.length() < 7 || s.indexOf('x') >= 0) {
    return false;
  }
  int dots = 0;
  for (unsigned i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '.') {
      dots++;
    } else if (c < '0' || c > '9') {
      return false;
    }
  }
  return dots == 3;
}

inline bool hueLooksLikeKey(const String &s) {
  return s.length() >= 20 && s.indexOf("your-") < 0;
}

inline void hueRefreshIdentity() {
  gHueKeyUsable = hueLooksLikeKey(gHueAppKey);
  gHueIpUsable = hueLooksLikeIp(gHueBridgeIp);
}

inline bool hueProbeBridge(const String &ip, String *bridgeId) {
  String body;
  const int code = hueHttp("https://" + ip + "/api/config", "GET", nullptr, &body, false, true);
  if (code != HTTP_CODE_OK) {
    return false;
  }
  String id;
  if (!jsonStringField(body, "bridgeid", &id)) {
    return false;
  }
  if (bridgeId) {
    *bridgeId = id;
  }
  return true;
}

inline void hueLoadStore() {
  Preferences prefs;
  prefs.begin("hue", true);
  gHueBridgeIp = prefs.getString("ip", "");
  gHueAppKey = prefs.getString("key", "");
  gHueBridgeId = prefs.getString("bid", "");
  prefs.end();

  if (!hueLooksLikeIp(gHueBridgeIp) && hueLooksLikeIp(HUE_BRIDGE_IP)) {
    gHueBridgeIp = HUE_BRIDGE_IP;
  }
  if (!hueLooksLikeKey(gHueAppKey) && hueLooksLikeKey(HUE_APP_KEY)) {
    gHueAppKey = HUE_APP_KEY;
  }
  hueRefreshIdentity();
}

inline void hueSaveStore() {
  Preferences prefs;
  prefs.begin("hue", false);
  prefs.putString("ip", gHueBridgeIp);
  prefs.putString("key", gHueAppKey);
  prefs.putString("bid", gHueBridgeId);
  prefs.end();
}

inline bool hueDiscoverMdns() {
  String host = "hue-sw-";
  host += String((uint16_t)(ESP.getEfuseMac() & 0xFFFF), HEX);
  if (!MDNS.begin(host.c_str())) {
    LOGLN("mDNS begin failed");
    return false;
  }

  const int n = MDNS.queryService("hue", "tcp");
  LOG("mDNS _hue._tcp: %d\n", n);
  String chosen;
  for (int i = 0; i < n; i++) {
    const IPAddress ip = MDNS.address(i);
    if (ip == IPAddress()) {
      continue;
    }
    const String ipStr = ip.toString();
    const String bid = MDNS.hasTxt(i, "bridgeid") ? MDNS.txt(i, "bridgeid") : String();
    LOG("  %s  %s  bridgeid=%s\n", MDNS.instanceName(i).c_str(), ipStr.c_str(), bid.c_str());
    if (gHueBridgeId.length() && bid.length() && bid.equalsIgnoreCase(gHueBridgeId)) {
      chosen = ipStr;
      break;
    }
    if (!chosen.length()) {
      chosen = ipStr;
    }
  }

  if (!chosen.length()) {
    return false;
  }
  gHueBridgeIp = chosen;
  return true;
}

inline bool hueDiscoverCloud() {
  String body;
  const int code = hueHttp("https://discovery.meethue.com/", "GET", nullptr, &body, false, false);
  LOG("discovery.meethue.com %d\n", code);
  if (code != HTTP_CODE_OK) {
    return false;
  }
  String ip;
  if (!jsonStringField(body, "internalipaddress", &ip) || !hueLooksLikeIp(ip)) {
    return false;
  }
  LOG("  cloud IP %s\n", ip.c_str());
  gHueBridgeIp = ip;
  return true;
}

inline bool hueFindBridge() {
  const String cached = gHueBridgeIp;
  bool ok = false;

  if (hueDiscoverMdns()) {
    String id;
    if (hueProbeBridge(gHueBridgeIp, &id)) {
      gHueBridgeId = id;
      LOG("Bridge via mDNS %s id=%s\n", gHueBridgeIp.c_str(), gHueBridgeId.c_str());
      ok = true;
    }
  }

  if (!ok && hueLooksLikeIp(cached) && hueProbeBridge(cached, &gHueBridgeId)) {
    gHueBridgeIp = cached;
    LOG("Bridge via cache %s\n", gHueBridgeIp.c_str());
    ok = true;
  }

  if (!ok && hueLooksLikeIp(HUE_BRIDGE_IP) && hueProbeBridge(HUE_BRIDGE_IP, &gHueBridgeId)) {
    gHueBridgeIp = HUE_BRIDGE_IP;
    LOG("Bridge via config.h %s\n", gHueBridgeIp.c_str());
    ok = true;
  }

  if (!ok && hueDiscoverCloud() && hueProbeBridge(gHueBridgeIp, &gHueBridgeId)) {
    LOG("Bridge via cloud %s\n", gHueBridgeIp.c_str());
    ok = true;
  }

  if (!ok) {
    LOGLN("Bridge not found");
  }
  hueRefreshIdentity();
  return ok;
}

// Pausa entre POST de pairing. El tick del LED no lleva delay(); el timer
// cubre el hueHttp bloqueante y aquí se llama ledPoll entre intentos.
inline void hueWaitMs(unsigned long ms) {
  const unsigned long start = millis();
  while (millis() - start < ms) {
    ledPoll(millis());
    if (gOnHueWait) {
      gOnHueWait();
    }
    const unsigned long elapsed = millis() - start;
    if (elapsed >= ms) {
      break;
    }
    unsigned long slice = ms - elapsed;
    if (slice > 10) {
      slice = 10;
    }
    delay(slice);
  }
}

// POST /api hasta que pulsen el botón del Bridge (o timeout).
// El naranja es el patrón #3 del clasificador (gHuePairing), no un blink propio.
inline bool huePairAppKey() {
  if (!hueLooksLikeIp(gHueBridgeIp)) {
    return false;
  }

  LOGLN("Pairing: press the Bridge link button");
  gHuePairing = true;
  gHuePairTimeout = false;
  ledPoll(millis());

  bool paired = false;
  const unsigned long start = millis();
  while (millis() - start < kPairTimeoutMs) {
    ledPoll(millis());
    String body;
    // hueHttp bloquea este hilo. ledTick sigue en el timer de 50 ms.
    const int code = hueHttp("https://" + gHueBridgeIp + "/api", "POST",
                             "{\"devicetype\":\"hue-simple-switch#xiao\"}", &body, false, true);
    String user;
    if (jsonStringField(body, "username", &user) && hueLooksLikeKey(user)) {
      gHueAppKey = user;
      hueRefreshIdentity();
      gHueAuthRejected = false;
      gHuePairTimeout = false;
      gHueBridgeMissing = false;
      paired = true;
      LOGLN("Paired (key stored in flash)");
      break;
    }
    if (body.indexOf("link button not pressed") < 0 && code > 0) {
      LOG("Pair POST %d %s\n", code, body.c_str());
    }
    hueWaitMs(400);
  }

  if (!paired) {
    gHuePairTimeout = true;
    LOGLN("Pairing timeout");
  }
  gHuePairing = false;
  ledPoll(millis());
  return paired;
}

inline bool hueKeyWorks() {
  if (!hueLooksLikeKey(gHueAppKey) || !hueLooksLikeIp(gHueBridgeIp)) {
    return false;
  }
  String body;
  const int code = hueHttp("https://" + gHueBridgeIp + "/clip/v2/resource/bridge", "GET", nullptr, &body, true, true);
  LOG("Hue auth GET %d\n", code);
  return code == HTTP_CODE_OK;
}

inline bool hueEnsureReady() {
  hueLoadStore();
  if (!hueFindBridge()) {
    gHueBridgeMissing = true;
    ledPoll(millis());
    return false;
  }
  gHueBridgeMissing = false;
  if (!hueKeyWorks()) {
    if (!huePairAppKey() || !hueKeyWorks()) {
      ledPoll(millis());
      return false;
    }
  }
  hueSaveStore();
  ledPoll(millis());
  return true;
}

inline bool hueRePair() {
  LOGLN("Re-pair requested");
  gHueAppKey = "";
  hueRefreshIdentity();
  gHuePairTimeout = false;
  ledPoll(millis());
  if (!hueFindBridge()) {
    gHueBridgeMissing = true;
    ledPoll(millis());
    return false;
  }
  gHueBridgeMissing = false;
  if (!huePairAppKey()) {
    return false;
  }
  hueSaveStore();
  const bool works = hueKeyWorks();
  ledPoll(millis());
  return works;
}
