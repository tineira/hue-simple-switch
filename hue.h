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
  if (response) {
    *response = http.getString();
  }
  http.end();
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
  if (code == HTTP_CODE_OK) {
    http.writeToStream(&sink);
  }
  http.end();
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
    Serial.println("Hue GET: begin failed");
    return false;
  }
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "GET", nullptr, &body, true, true);
  Serial.printf("Hue GET %s/%s %d\n", rtype, rid, code);
  if (code != HTTP_CODE_OK) {
    Serial.println(body);
    return false;
  }
  if (!jsonHueOn(body.c_str(), on)) {
    Serial.println("Hue GET: could not parse on");
    Serial.println(body);
    return false;
  }
  return true;
}

inline bool hueSetOn(const char *rtype, const char *rid, bool on) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !rtype || !rid) {
    Serial.println("Hue PUT: begin failed");
    return false;
  }
  const char *payload = on ? "{\"on\":{\"on\":true}}" : "{\"on\":{\"on\":false}}";
  String body;
  const int code = hueHttp(hueResourceUrl(rtype, rid), "PUT", payload, &body, true, true);
  Serial.printf("Hue PUT %s/%s %d -> %s\n", rtype, rid, code, on ? "on" : "off");
  if (code != HTTP_CODE_OK) {
    Serial.println(body);
    return false;
  }
  return true;
}

inline bool hueRecallScene(const char *rid) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length() || !rid) {
    Serial.println("Hue recall: begin failed");
    return false;
  }
  String body;
  const int code =
      hueHttp(hueResourceUrl("scene", rid), "PUT", "{\"recall\":{\"action\":\"active\"}}", &body, true, true);
  Serial.printf("Hue recall scene/%s %d\n", rid, code);
  if (code != HTTP_CODE_OK) {
    Serial.println(body);
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
  Serial.printf("Hue execute: unknown action %s\n", action);
  return false;
}
