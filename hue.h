#pragma once

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "config.h"

// IP y application key en runtime (mDNS / NVS / emparejado).
// config.h sigue siendo semilla opcional.
extern String gHueBridgeIp;
extern String gHueAppKey;

// El Bridge usa un certificado propio; Clip v2 exige HTTPS local.
// setInsecure() evita validar esa CA (solo LAN, no cloud).

inline String hueLightUrl() {
  String url = "https://";
  url += gHueBridgeIp;
  url += "/clip/v2/resource/light/";
  url += HUE_LIGHT_ID;
  return url;
}

inline bool hueParseOn(const String &body, bool *on) {
  String compact = body;
  compact.replace(" ", "");
  compact.replace("\n", "");
  compact.replace("\r", "");
  compact.replace("\t", "");

  const String needle = "\"on\":{\"on\":";
  const int idx = compact.indexOf(needle);
  if (idx < 0) {
    return false;
  }
  const int valueAt = idx + needle.length();
  if (compact.startsWith("true", valueAt)) {
    *on = true;
    return true;
  }
  if (compact.startsWith("false", valueAt)) {
    *on = false;
    return true;
  }
  return false;
}

inline bool hueBegin(HTTPClient &http, NetworkClientSecure &client) {
  if (!gHueBridgeIp.length() || !gHueAppKey.length()) {
    return false;
  }
  client.setInsecure();
  if (!http.begin(client, hueLightUrl())) {
    return false;
  }
  http.setTimeout(8000);
  http.addHeader("hue-application-key", gHueAppKey);
  return true;
}

inline bool hueGetOn(bool *on) {
  NetworkClientSecure client;
  HTTPClient http;
  if (!hueBegin(http, client)) {
    Serial.println("Hue GET: begin failed");
    return false;
  }

  const int code = http.GET();
  const String body = http.getString();
  http.end();

  Serial.printf("Hue GET %d\n", code);
  if (code != HTTP_CODE_OK) {
    Serial.println(body);
    return false;
  }
  if (!hueParseOn(body, on)) {
    Serial.println("Hue GET: could not parse on");
    Serial.println(body);
    return false;
  }
  return true;
}

inline bool hueSetOn(bool on) {
  NetworkClientSecure client;
  HTTPClient http;
  if (!hueBegin(http, client)) {
    Serial.println("Hue PUT: begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");

  const String payload = on ? "{\"on\":{\"on\":true}}" : "{\"on\":{\"on\":false}}";
  const int code = http.PUT(payload);
  const String body = http.getString();
  http.end();

  Serial.printf("Hue PUT %d -> %s\n", code, on ? "on" : "off");
  if (code != HTTP_CODE_OK) {
    Serial.println(body);
    return false;
  }
  return true;
}

// GET estado actual y PUT el inverso (una lámpara).
inline bool hueToggle(bool *nowOn) {
  bool on = false;
  if (!hueGetOn(&on)) {
    return false;
  }
  if (!hueSetOn(!on)) {
    return false;
  }
  *nowOn = !on;
  return true;
}
