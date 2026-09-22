#pragma once

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "config.h"
#include "json_util.h"

#ifndef HUE_BRIDGE_IP
#define HUE_BRIDGE_IP ""
#endif
#ifndef HUE_APP_KEY
#define HUE_APP_KEY ""
#endif

// IP y application key en runtime (mDNS / NVS / emparejado).
extern String gHueBridgeIp;
extern String gHueAppKey;

// Estado de pareo para el LED. RAM: se pierde al boot. No hay GET extra.
inline volatile bool gHueKeyUsable = false;
inline volatile bool gHueIpUsable = false;
inline volatile bool gHueAuthRejected = false;
inline volatile bool gHuePairing = false;
inline volatile bool gHuePairTimeout = false;
inline volatile bool gHueBridgeMissing = false;

// 401/403 con key (salvo "link button not pressed") bajan a #3, también un PUT GPIO.
// Sin key (/api/config, discovery) no prueba que la key guardada esté mala.
// Un 200 con key recupera. Timeout, 5xx o Bridge caído no entran aquí.
inline void hueNoteAuth(int code, const String *body, bool withKey) {
  if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN) {
    if (!withKey) {
      return;
    }
    if (body && body->indexOf("link button not pressed") >= 0) {
      return;
    }
    gHueAuthRejected = true;
    return;
  }
  if (withKey && code == HTTP_CODE_OK) {
    gHueAuthRejected = false;
    gHuePairTimeout = false;
    gHueBridgeMissing = false;
  }
}

// El Bridge usa un certificado propio; Clip v2 exige HTTPS local.
// setInsecure() evita validar esa CA (solo LAN, no cloud).

inline int hueHttp(const String &url, const char *method, const char *body, String *response, bool withKey,
                   bool insecure) {
  NetworkClientSecure client;
  if (insecure) {
    client.setInsecure();
  } else {
    client.useBuiltinCACertBundle();
  }
  HTTPClient http;
  if (!http.begin(client, url)) {
    return -1;
  }
  http.setTimeout(8000);
  if (withKey && gHueAppKey.length()) {
    http.addHeader("hue-application-key", gHueAppKey);
  }
  if (body) {
    http.addHeader("Content-Type", "application/json");
  }
  int code = -1;
  if (strcmp(method, "GET") == 0) {
    code = http.GET();
  } else if (strcmp(method, "POST") == 0) {
    code = http.POST(body ? String(body) : String());
  } else {
    code = http.PUT(body ? String(body) : String());
  }
  String denied;
  const String *noted = nullptr;
  if (response) {
    *response = http.getString();
    noted = response;
  } else if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN) {
    denied = http.getString();
    noted = &denied;
  }
  http.end();
  hueNoteAuth(code, noted, withKey);
  return code;
}

inline int hueClipStream(const char *resource, JsonDataSink &sink) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !resource) {
    return -1;
  }
  String url = "https://";
  url += gHueBridgeIp;
  url += "/clip/v2/resource/";
  url += resource;
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) {
    return -1;
  }
  http.setTimeout(20000);
  http.addHeader("hue-application-key", gHueAppKey);
  const int code = http.GET();
  String denied;
  const String *noted = nullptr;
  if (code == HTTP_CODE_OK) {
    http.writeToStream(&sink);
  } else if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN) {
    denied = http.getString();
    noted = &denied;
  }
  http.end();
  hueNoteAuth(code, noted, true);
  return code;
}

inline String hueResourceUrl(const char *rtype, const char *rid) {
  String url = "https://";
  url += gHueBridgeIp;
  url += "/clip/v2/resource/";
  url += rtype;
  url += "/";
  url += rid;
  return url;
}

inline bool hueParseOn(const String &body, bool *on) {
  return jsonHueOn(body.c_str(), on);
}

inline bool hueGetOn(const char *rtype, const char *rid, bool *on) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !rtype || !rid || !on) {
    LOGLN("Hue GET: begin failed");
    return false;
  }
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "GET", nullptr, &body, true, true);
  LOG("Hue GET %s/%s %d\n", rtype, rid, code);
  if (code != HTTP_CODE_OK) {
    LOGLN(body);
    return false;
  }
  if (!jsonHueOn(body.c_str(), on)) {
    LOGLN("Hue GET: could not parse on");
    LOGLN(body);
    return false;
  }
  return true;
}

inline bool hueSetOn(const char *rtype, const char *rid, bool on) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !rtype || !rid) {
    LOGLN("Hue PUT: begin failed");
    return false;
  }
  const char *payload = on ? "{\"on\":{\"on\":true}}" : "{\"on\":{\"on\":false}}";
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "PUT", payload, &body, true, true);
  LOG("Hue PUT %s/%s %d -> %s\n", rtype, rid, code, on ? "on" : "off");
  if (code != HTTP_CODE_OK) {
    LOGLN(body);
    return false;
  }
  return true;
}

inline bool hueRecallScene(const char *rid) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !rid) {
    LOGLN("Hue recall: begin failed");
    return false;
  }
  String body;
  const int code =
      hueHttp(hueResourceUrl("scene", rid), "PUT", "{\"recall\":{\"action\":\"active\"}}", &body, true, true);
  LOG("Hue recall scene/%s %d\n", rid, code);
  if (code != HTTP_CODE_OK) {
    LOGLN(body);
    return false;
  }
  return true;
}

inline bool hueToggle(const char *rtype, const char *rid, bool *nowOn) {
  bool on = false;
  if (!hueGetOn(rtype, rid, &on)) {
    return false;
  }
  if (!hueSetOn(rtype, rid, !on)) {
    return false;
  }
  if (nowOn) {
    *nowOn = !on;
  }
  return true;
}

inline bool hueExecute(const char *action, const char *rtype, const char *rid) {
  if (!action || !rtype || !rid || !rid[0]) {
    return false;
  }
  if (strcmp(action, "on") == 0) {
    return hueSetOn(rtype, rid, true);
  }
  if (strcmp(action, "off") == 0) {
    return hueSetOn(rtype, rid, false);
  }
  if (strcmp(action, "recall_scene") == 0) {
    return hueRecallScene(rid);
  }
  if (strcmp(action, "toggle") == 0) {
    return hueToggle(rtype, rid, nullptr);
  }
  LOG("Hue execute: unknown action %s\n", action);
  return false;
}
