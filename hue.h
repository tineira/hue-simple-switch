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

// IP and application key at runtime (mDNS / NVS / pairing).
extern String gHueBridgeIp;
extern String gHueAppKey;

// Bridge address, application key and Bridge id, copied out of the loop's Strings (these two
// and gHueBridgeId): the Hue worker and console tasks use their own copy while the loop re-pairs
// or clears them. The Clip v2 calls below need only the address and key.
struct HueCreds {
  char ip[16];  // dotted IPv4, at most 15 characters
  char key[64];
  char bid[40];  // Bridge id (16 hex characters on current Bridges)
};

inline bool hueCredsOk(const HueCreds &c) { return c.ip[0] && c.key[0]; }

// Pairing state for the LED. RAM: lost at boot. No extra GET.
inline volatile bool gHueKeyUsable = false;
inline volatile bool gHueIpUsable = false;
inline volatile bool gHueAuthRejected = false;
// After receiving the key from the Bridge button, an immediate 401 does not stick.
inline unsigned long gHueAuthGraceUntil = 0;
inline volatile bool gHuePairing = false;
inline volatile bool gHuePairTimeout = false;
inline volatile bool gHueBridgeMissing = false;

// 401/403 with the key (except "link button not pressed") drop to #3, a GPIO PUT too.
// Without the key (/api/config, discovery) it does not prove the saved key is bad.
// A 200 with the key recovers. Timeout, 5xx or Bridge down do not count here.
inline void hueAuthGraceArm(unsigned long ms) {
  gHueAuthGraceUntil = millis() + ms;
  gHueAuthRejected = false;
}

inline bool hueAuthGraceOpen() {
  return (long)(gHueAuthGraceUntil - millis()) > 0;
}

inline void hueNoteAuth(int code, const String *body, bool withKey) {
  if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN) {
    if (!withKey || hueAuthGraceOpen()) {
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

// The Bridge uses a self-signed certificate; Clip v2 requires local HTTPS.
// setInsecure() skips validating that CA (LAN only, not cloud).

// key: sent as hue-application-key; nullptr = a call without the key (discovery, pairing).
inline int hueHttpKey(const String &url, const char *method, const char *body, String *response, const char *key,
                      bool insecure) {
  const bool withKey = key != nullptr;
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
  if (withKey && key[0]) {
    http.addHeader("hue-application-key", key);
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

// Loop task only (discovery, pairing): reads the saved key, which only the loop changes.
inline int hueHttp(const String &url, const char *method, const char *body, String *response, bool withKey,
                   bool insecure) {
  return hueHttpKey(url, method, body, response, withKey ? gHueAppKey.c_str() : nullptr, insecure);
}

inline int hueClipStream(const HueCreds &c, const char *resource, JsonDataSink &sink) {
  if (!hueCredsOk(c) || !resource) {
    return -1;
  }
  String url = "https://";
  url += c.ip;
  url += "/clip/v2/resource/";
  url += resource;
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) {
    return -1;
  }
  http.setTimeout(20000);
  http.addHeader("hue-application-key", c.key);
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

inline String hueResourceUrl(const HueCreds &c, const char *rtype, const char *rid) {
  String url = "https://";
  url += c.ip;
  url += "/clip/v2/resource/";
  url += rtype;
  url += "/";
  url += rid;
  return url;
}

inline bool hueParseOn(const String &body, bool *on) {
  return jsonHueOn(body.c_str(), on);
}

// bri (optional): dimming.brightness, -1 when the resource has none.
inline bool hueGetOn(const HueCreds &c, const char *rtype, const char *rid, bool *on, float *bri = nullptr) {
  if (!hueCredsOk(c) || !rtype || !rid || !on) {
    LOGLN("Hue GET: begin failed");
    return false;
  }
  String body;
  const int code = hueHttpKey(hueResourceUrl(c, rtype, rid), "GET", nullptr, &body, c.key, true);
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
  if (bri && !jsonHueBrightness(body.c_str(), bri)) {
    *bri = -1;
  }
  return true;
}

inline bool huePut(const HueCreds &c, const char *rtype, const char *rid, const char *payload, const char *what) {
  if (!hueCredsOk(c) || !rtype || !rid) {
    LOGLN("Hue PUT: begin failed");
    return false;
  }
  String body;
  const int code = hueHttpKey(hueResourceUrl(c, rtype, rid), "PUT", payload, &body, c.key, true);
  LOG("Hue PUT %s/%s %d -> %s\n", rtype, rid, code, what);
  if (code != HTTP_CODE_OK) {
    LOGLN(body);
    return false;
  }
  return true;
}

inline bool hueSetOn(const HueCreds &c, const char *rtype, const char *rid, bool on) {
  return huePut(c, rtype, rid, on ? "{\"on\":{\"on\":true}}" : "{\"on\":{\"on\":false}}", on ? "on" : "off");
}

// Hold to dim: the Bridge runs each leg (dimming_delta over dynamics.duration); the switch
// starts the legs, turns around at the ends while the button is held, and stops on release
// (console docs/specs/simple-dim-cycle.md). Tunable after testing on the wall.
static const unsigned long kDimSweepMs = 3000;    // full sweep, minimum -> 100 %
static const unsigned long kDimMinLegMs = 150;    // shortest leg, however little distance is left
static const unsigned long kDimDwellMs = 250;     // pause at each end before turning around
static const unsigned long kDimMaxHoldMs = 30000; // continuous hold after which the cycle stops
static const int kDimMinBrightness = 1;           // start level when the target was off
static const int kDimUpBelow = 30;                // below this brightness (%) the first leg goes up

// Leg duration for the distance left (brightness points): same speed from any level.
inline unsigned long hueDimLegMs(float distance) {
  if (distance < 0) {
    distance = 0;
  }
  if (distance > 100) {
    distance = 100;
  }
  const unsigned long ms = static_cast<unsigned long>(kDimSweepMs * distance / 100.0f);
  return ms < kDimMinLegMs ? kDimMinLegMs : ms;
}

inline bool hueDimFromOff(const HueCreds &c, const char *rtype, const char *rid) {
  char payload[64];
  snprintf(payload, sizeof(payload), "{\"on\":{\"on\":true},\"dimming\":{\"brightness\":%d}}", kDimMinBrightness);
  return huePut(c, rtype, rid, payload, "on at minimum");
}

inline bool hueDimStart(const HueCreds &c, const char *rtype, const char *rid, bool up, unsigned long durationMs) {
  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"dimming_delta\":{\"action\":\"%s\",\"brightness_delta\":100},\"dynamics\":{\"duration\":%lu}}",
           up ? "up" : "down", durationMs);
  return huePut(c, rtype, rid, payload, up ? "dim up" : "dim down");
}

inline bool hueDimStop(const HueCreds &c, const char *rtype, const char *rid) {
  return huePut(c, rtype, rid, "{\"dimming_delta\":{\"action\":\"stop\"}}", "dim stop");
}

// HTTP code (-1 if it could not start): a scene list skips a 404 and tries the next one.
inline int hueRecallScene(const HueCreds &c, const char *rid) {
  if (!hueCredsOk(c) || !rid) {
    LOGLN("Hue recall: begin failed");
    return -1;
  }
  String body;
  const int code =
      hueHttpKey(hueResourceUrl(c, "scene", rid), "PUT", "{\"recall\":{\"action\":\"active\"}}", &body, c.key,
                 true);
  LOG("Hue recall scene/%s %d\n", rid, code);
  if (code != HTTP_CODE_OK) {
    LOGLN(body);
  }
  return code;
}

inline bool hueToggle(const HueCreds &c, const char *rtype, const char *rid, bool *nowOn) {
  bool on = false;
  if (!hueGetOn(c, rtype, rid, &on)) {
    return false;
  }
  if (!hueSetOn(c, rtype, rid, !on)) {
    return false;
  }
  if (nowOn) {
    *nowOn = !on;
  }
  return true;
}

inline bool hueExecute(const HueCreds &c, const char *action, const char *rtype, const char *rid) {
  if (!action || !rtype || !rid || !rid[0]) {
    return false;
  }
  if (strcmp(action, "on") == 0) {
    return hueSetOn(c, rtype, rid, true);
  }
  if (strcmp(action, "off") == 0) {
    return hueSetOn(c, rtype, rid, false);
  }
  if (strcmp(action, "recall_scene") == 0) {
    return hueRecallScene(c, rid) == HTTP_CODE_OK;
  }
  if (strcmp(action, "toggle") == 0) {
    return hueToggle(c, rtype, rid, nullptr);
  }
  LOG("Hue execute: unknown action %s\n", action);
  return false;
}
