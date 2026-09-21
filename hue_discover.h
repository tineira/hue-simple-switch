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
    Serial.println("mDNS begin failed");
    return false;
  }

  const int n = MDNS.queryService("hue", "tcp");
  Serial.printf("mDNS _hue._tcp: %d\n", n);
  String chosen;
  for (int i = 0; i < n; i++) {
    const IPAddress ip = MDNS.address(i);
    if (ip == IPAddress()) {
      continue;
    }
    const String ipStr = ip.toString();
    const String bid = MDNS.hasTxt(i, "bridgeid") ? MDNS.txt(i, "bridgeid") : String();
    Serial.printf("  %s  %s  bridgeid=%s\n", MDNS.instanceName(i).c_str(), ipStr.c_str(), bid.c_str());
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
  Serial.printf("discovery.meethue.com %d\n", code);
  if (code != HTTP_CODE_OK) {
    return false;
  }
  String ip;
  if (!jsonStringField(body, "internalipaddress", &ip) || !hueLooksLikeIp(ip)) {
    return false;
  }
  Serial.printf("  cloud IP %s\n", ip.c_str());
  gHueBridgeIp = ip;
  return true;
}

inline bool hueFindBridge() {
  const String cached = gHueBridgeIp;

  if (hueDiscoverMdns()) {
    String id;
    if (hueProbeBridge(gHueBridgeIp, &id)) {
      gHueBridgeId = id;
      Serial.printf("Bridge via mDNS %s id=%s\n", gHueBridgeIp.c_str(), gHueBridgeId.c_str());
      return true;
    }
  }

  if (hueLooksLikeIp(cached) && hueProbeBridge(cached, &gHueBridgeId)) {
    gHueBridgeIp = cached;
    Serial.printf("Bridge via cache %s\n", gHueBridgeIp.c_str());
    return true;
  }

  if (hueLooksLikeIp(HUE_BRIDGE_IP) && hueProbeBridge(HUE_BRIDGE_IP, &gHueBridgeId)) {
    gHueBridgeIp = HUE_BRIDGE_IP;
    Serial.printf("Bridge via config.h %s\n", gHueBridgeIp.c_str());
    return true;
  }

  if (hueDiscoverCloud() && hueProbeBridge(gHueBridgeIp, &gHueBridgeId)) {
    Serial.printf("Bridge via cloud %s\n", gHueBridgeIp.c_str());
    return true;
  }

  Serial.println("Bridge not found");
  return false;
}

inline void hueBlink(unsigned long ms) {
  const bool on = ((ms / 200) % 2) == 0;
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

// POST /api hasta que pulsen el botón del Bridge (o timeout).
inline bool huePairAppKey() {
  if (!hueLooksLikeIp(gHueBridgeIp)) {
    return false;
  }

  Serial.println("Pairing: press the Bridge link button");
  const unsigned long start = millis();
  while (millis() - start < kPairTimeoutMs) {
    hueBlink(millis() - start);
    String body;
    const int code = hueHttp("https://" + gHueBridgeIp + "/api", "POST",
                             "{\"devicetype\":\"hue-simple-switch#xiao\"}", &body, false, true);
    String user;
    if (jsonStringField(body, "username", &user) && hueLooksLikeKey(user)) {
      gHueAppKey = user;
      digitalWrite(LED_BUILTIN, HIGH);
      Serial.println("Paired (key stored in flash)");
      return true;
    }
    if (body.indexOf("link button not pressed") < 0 && code > 0) {
      Serial.printf("Pair POST %d %s\n", code, body.c_str());
    }
    if (gOnHueWait) {
      gOnHueWait();
    }
    delay(400);
  }
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("Pairing timeout");
  return false;
}

inline bool hueKeyWorks() {
  if (!hueLooksLikeKey(gHueAppKey) || !hueLooksLikeIp(gHueBridgeIp)) {
    return false;
  }
  String body;
  const int code = hueHttp("https://" + gHueBridgeIp + "/clip/v2/resource/bridge", "GET", nullptr, &body, true, true);
  Serial.printf("Hue auth GET %d\n", code);
  return code == HTTP_CODE_OK;
}

inline bool hueEnsureReady() {
  hueLoadStore();
  if (!hueFindBridge()) {
    return false;
  }
  if (!hueKeyWorks()) {
    if (!huePairAppKey() || !hueKeyWorks()) {
      return false;
    }
  }
  hueSaveStore();
  return true;
}

inline bool hueRePair() {
  Serial.println("Re-pair requested");
  gHueAppKey = "";
  if (!hueFindBridge()) {
    return false;
  }
  if (!huePairAppKey()) {
    return false;
  }
  hueSaveStore();
  return hueKeyWorks();
}
