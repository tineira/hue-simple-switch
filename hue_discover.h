#pragma once

#include <string.h>
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

// HUEPAIR no bloquea el parser USB: un mDNS o un HTTP por usbPoll.
// gHuePairHold cubre el pair bloqueante del boot para que el paso no reentre.

enum HuePairStep : uint8_t {
  HUE_PAIR_IDLE = 0,
  HUE_PAIR_MDNS,
  HUE_PAIR_PROBE_MDNS,
  HUE_PAIR_PROBE_CACHED,
  HUE_PAIR_PROBE_CONFIG,
  HUE_PAIR_CLOUD,
  HUE_PAIR_PROBE_CLOUD,
  HUE_PAIR_BUTTON,
  HUE_PAIR_CONFIRM,
};

inline HuePairStep gHuePairStep = HUE_PAIR_IDLE;
inline bool gHuePairHold = false;
inline volatile bool gHuePairCancel = false;
inline bool gHuePairSync = false;
inline bool gHueReady = false;
inline String gHuePairCachedIp;
inline unsigned long gHuePairStartMs = 0;
inline unsigned long gHuePairNextPostMs = 0;

inline bool huePairBusy() {
  return gHuePairHold || gHuePairStep != HUE_PAIR_IDLE || gHuePairing;
}

inline void hueClearSavedKey() {
  gHueAppKey = "";
  hueRefreshIdentity();
  gHueAuthRejected = false;
  gHuePairTimeout = false;
  Preferences prefs;
  if (!prefs.begin("hue", false)) {
    return;
  }
  if (prefs.isKey("key")) {
    prefs.remove("key");
  }
  prefs.end();
}

inline void hueForgetSaved() {
  Preferences prefs;
  if (prefs.begin("hue", false)) {
    prefs.clear();
    prefs.end();
  }
  gHueBridgeIp = "";
  gHueAppKey = "";
  gHueBridgeId = "";
  hueRefreshIdentity();
  gHueAuthRejected = false;
  gHuePairTimeout = false;
  gHueBridgeMissing = false;
  gHuePairing = false;
  gHueReady = false;
  gHuePairSync = false;
  gHuePairStep = HUE_PAIR_IDLE;
}

inline void huePairStop() {
  gHuePairCancel = true;
  gHuePairStep = HUE_PAIR_IDLE;
  gHuePairing = false;
  gHuePairTimeout = false;
  gHuePairSync = false;
  gHuePairNextPostMs = 0;
  ledPoll(millis());
}

// Pausa entre POST de pairing. Sale si HUECLR pidió cancelar.
inline void hueWaitMs(unsigned long ms) {
  const unsigned long start = millis();
  while (!gHuePairCancel && (millis() - start) < ms) {
    ledPoll(millis());
    if (gOnHueWait) {
      gOnHueWait();
    }
    if (gHuePairCancel) {
      break;
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

// Un POST de pairing. No loguea el body si trae username (la key).
inline bool huePairPostOnce() {
  if (gHuePairCancel || !hueLooksLikeIp(gHueBridgeIp)) {
    return false;
  }
  String body;
  const int code = hueHttp("https://" + gHueBridgeIp + "/api", "POST",
                           "{\"devicetype\":\"hue-simple-switch#xiao\"}", &body, false, true);
  if (gHuePairCancel) {
    return false;
  }
  String user;
  if (jsonStringField(body, "username", &user) && hueLooksLikeKey(user)) {
    gHueAppKey = user;
    hueRefreshIdentity();
    gHueAuthRejected = false;
    gHuePairTimeout = false;
    gHueBridgeMissing = false;
    LOGLN("Paired (key stored in flash)");
    return true;
  }
  if (body.indexOf("username") < 0 && body.indexOf("link button not pressed") < 0 && code > 0) {
    LOG("Pair POST %d %s\n", code, body.c_str());
  }
  return false;
}

// POST /api hasta que pulsen el botón del Bridge (o timeout).
// El naranja es el patrón #3 del clasificador (gHuePairing), no un blink propio.
// Solo lo usa el boot (hueEnsureReady), nunca el parser USB.
inline bool huePairAppKey() {
  if (gHuePairCancel || !hueLooksLikeIp(gHueBridgeIp)) {
    return false;
  }

  LOGLN("Pairing: press the Bridge link button");
  gHuePairing = true;
  gHuePairTimeout = false;
  ledPoll(millis());

  bool paired = false;
  const unsigned long start = millis();
  while (!gHuePairCancel && (millis() - start) < kPairTimeoutMs) {
    ledPoll(millis());
    if (huePairPostOnce()) {
      paired = true;
      break;
    }
    if (gHuePairCancel) {
      break;
    }
    hueWaitMs(400);
  }

  gHuePairing = false;
  if (gHuePairCancel) {
    ledPoll(millis());
    return false;
  }
  if (!paired) {
    gHuePairTimeout = true;
    LOGLN("Pairing timeout");
  }
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
  if (gHuePairHold || gHuePairStep != HUE_PAIR_IDLE) {
    return false;
  }
  gHuePairHold = true;
  gHuePairCancel = false;
  bool ok = false;
  hueLoadStore();
  if (!gHuePairCancel && hueFindBridge()) {
    gHueBridgeMissing = false;
    const bool works = !gHuePairCancel && hueKeyWorks();
    if (!gHuePairCancel && (works || (huePairAppKey() && !gHuePairCancel && hueKeyWorks()))) {
      ok = true;
    }
  } else if (!gHuePairCancel) {
    gHueBridgeMissing = true;
  }
  if (ok && !gHuePairCancel) {
    hueSaveStore();
  } else {
    ok = false;
  }
  ledPoll(millis());
  gHuePairHold = false;
  return ok;
}

inline void huePairBeginButton() {
  gHuePairing = true;
  gHuePairTimeout = false;
  gHueBridgeMissing = false;
  gHuePairStartMs = millis();
  gHuePairNextPostMs = 0;
  gHuePairStep = HUE_PAIR_BUTTON;
  LOGLN("Pairing: press the Bridge link button");
  ledPoll(millis());
}

inline void huePairFailSearch() {
  gHueBridgeIp = gHuePairCachedIp;
  hueRefreshIdentity();
  gHueBridgeMissing = true;
  gHuePairing = false;
  gHuePairStep = HUE_PAIR_IDLE;
  LOGLN("Bridge not found");
  ledPoll(millis());
}

inline void huePairAcceptProbe(const String &ip, const String &id, const char *how) {
  gHueBridgeIp = ip;
  gHueBridgeId = id;
  hueRefreshIdentity();
  if (strcmp(how, "mDNS") == 0) {
    LOG("Bridge via mDNS %s id=%s\n", gHueBridgeIp.c_str(), gHueBridgeId.c_str());
  } else if (strcmp(how, "cache") == 0) {
    LOG("Bridge via cache %s\n", gHueBridgeIp.c_str());
  } else if (strcmp(how, "config") == 0) {
    LOG("Bridge via config.h %s\n", gHueBridgeIp.c_str());
  } else {
    LOG("Bridge via cloud %s\n", gHueBridgeIp.c_str());
  }
  huePairBeginButton();
}

inline void huePairPoll() {
  if (gHuePairHold || gHuePairStep == HUE_PAIR_IDLE) {
    return;
  }
  if (gHuePairCancel) {
    gHuePairStep = HUE_PAIR_IDLE;
    gHuePairing = false;
    return;
  }

  switch (gHuePairStep) {
    case HUE_PAIR_IDLE:
      return;
    case HUE_PAIR_MDNS: {
      const bool found = hueDiscoverMdns();
      if (gHuePairCancel) {
        gHuePairStep = HUE_PAIR_IDLE;
        gHuePairing = false;
        return;
      }
      gHuePairStep = found ? HUE_PAIR_PROBE_MDNS : HUE_PAIR_PROBE_CACHED;
      return;
    }
    case HUE_PAIR_PROBE_MDNS: {
      String id;
      const String ip = gHueBridgeIp;
      if (hueProbeBridge(ip, &id)) {
        huePairAcceptProbe(ip, id, "mDNS");
        return;
      }
      if (gHuePairCancel) {
        gHuePairStep = HUE_PAIR_IDLE;
        gHuePairing = false;
        return;
      }
      gHuePairStep = HUE_PAIR_PROBE_CACHED;
      return;
    }
    case HUE_PAIR_PROBE_CACHED: {
      if (hueLooksLikeIp(gHuePairCachedIp)) {
        String id;
        if (hueProbeBridge(gHuePairCachedIp, &id)) {
          huePairAcceptProbe(gHuePairCachedIp, id, "cache");
          return;
        }
      }
      if (gHuePairCancel) {
        gHuePairStep = HUE_PAIR_IDLE;
        gHuePairing = false;
        return;
      }
      gHuePairStep = HUE_PAIR_PROBE_CONFIG;
      return;
    }
    case HUE_PAIR_PROBE_CONFIG: {
      if (hueLooksLikeIp(HUE_BRIDGE_IP)) {
        String id;
        if (hueProbeBridge(String(HUE_BRIDGE_IP), &id)) {
          huePairAcceptProbe(String(HUE_BRIDGE_IP), id, "config");
          return;
        }
      }
      if (gHuePairCancel) {
        gHuePairStep = HUE_PAIR_IDLE;
        gHuePairing = false;
        return;
      }
      gHuePairStep = HUE_PAIR_CLOUD;
      return;
    }
    case HUE_PAIR_CLOUD: {
      const bool found = hueDiscoverCloud();
      if (gHuePairCancel) {
        gHuePairStep = HUE_PAIR_IDLE;
        gHuePairing = false;
        return;
      }
      if (!found) {
        huePairFailSearch();
        return;
      }
      gHuePairStep = HUE_PAIR_PROBE_CLOUD;
      return;
    }
    case HUE_PAIR_PROBE_CLOUD: {
      String id;
      const String ip = gHueBridgeIp;
      if (hueProbeBridge(ip, &id)) {
        huePairAcceptProbe(ip, id, "cloud");
        return;
      }
      huePairFailSearch();
      return;
    }
    case HUE_PAIR_BUTTON: {
      const unsigned long now = millis();
      if (gHuePairNextPostMs != 0 && (long)(gHuePairNextPostMs - now) > 0) {
        unsigned long wait = gHuePairNextPostMs - now;
        if (wait > 10) {
          wait = 10;
        }
        delay(wait);
        return;
      }
      if ((unsigned long)(now - gHuePairStartMs) >= kPairTimeoutMs) {
        gHuePairTimeout = true;
        gHuePairing = false;
        gHuePairStep = HUE_PAIR_IDLE;
        LOGLN("Pairing timeout");
        ledPoll(millis());
        return;
      }
      if (huePairPostOnce()) {
        gHuePairing = false;
        if (gHuePairCancel) {
          gHuePairStep = HUE_PAIR_IDLE;
          return;
        }
        hueSaveStore();
        gHuePairStep = HUE_PAIR_CONFIRM;
        ledPoll(millis());
        return;
      }
      if (gHuePairCancel) {
        gHuePairing = false;
        gHuePairStep = HUE_PAIR_IDLE;
        return;
      }
      gHuePairNextPostMs = millis() + 400;
      return;
    }
    case HUE_PAIR_CONFIRM: {
      const bool works = hueKeyWorks();
      gHuePairStep = HUE_PAIR_IDLE;
      gHuePairing = false;
      if (gHuePairCancel) {
        return;
      }
      if (works) {
        LOG("Using Bridge %s\n", gHueBridgeIp.c_str());
        gHuePairSync = true;
        gHueReady = true;
      }
      ledPoll(millis());
      return;
    }
  }
}

inline void huePairSessionBegin() {
  if (huePairBusy()) {
    return;
  }
  gHuePairCancel = false;
  gHuePairSync = false;
  gHueReady = false;
  hueClearSavedKey();
  gHueBridgeMissing = false;
  gHuePairing = false;
  gHuePairCachedIp = gHueBridgeIp;
  gHuePairStep = HUE_PAIR_MDNS;
  LOGLN("Re-pair requested");
  ledPoll(millis());
}
