#pragma once

#include <WiFi.h>
#include <Preferences.h>
#include "hue.h"
#include "hue_discover.h"
#include "recipes.h"

// Channels: closed = GPIO to GND (INPUT_PULLUP). The console picks each channel's kind
// (config channels[]); a pin it does not list does nothing. The old config payload has no
// channels[]: every pin runs with its compiled default kind below.

struct ChannelDef {
  const char *id;
  int gpio;
  const char *label;
  ChannelSettingKind defaultKind;
};

static const ChannelDef kChannels[] = {
    {"boot", 9, "BOOT", CHK_MOMENTARY},
    {"d0", 0, "D0", CHK_MAINTAINED},
    {"d1", 1, "D1", CHK_MAINTAINED},
    {"d2", 2, "D2", CHK_MAINTAINED},
};

static const size_t kChannelCount = sizeof(kChannels) / sizeof(kChannels[0]);
static const unsigned long kDebounceMs = 50;
static const unsigned long kDoubleClickMs = 400;
static const unsigned long kHoldMs = 800;

struct ChannelRuntime {
  int lastReading;
  int stable;  // HIGH = open, LOW = closed
  unsigned long lastChangeMs;
  bool primed;
  bool waitOff;  // maintained: opened, waiting for off. momentary: released, waiting for a second press.
  unsigned long waitStartMs;
  unsigned long pressStartMs;
  bool longPressHandled;
};

// Resolved from recipes + channels[] each time gRecipesGen changes (GPIO loop only).
struct ChannelMode {
  uint8_t kind;  // CHK_NONE = ignored
  bool hasDoubleClick;
  bool hasHold;
};

// Hold to dim (RAM only). active: a ramp was started and needs its stop on release; the
// target is kept so the stop goes where the start went, even if recipes change mid-hold.
struct DimRuntime {
  bool active;
  bool lastUp;  // false at boot: the first mid-level hold ramps up
  char rtype[16];
  char rid[40];
};

inline ChannelRuntime gCh[kChannelCount];
inline DimRuntime gDim[kChannelCount];
inline ChannelMode gChMode[kChannelCount];
inline uint32_t gChModeGen = 0;
inline bool gChModeValid = false;
// Last scene rid each channel set (NVS recipes/ls_<id>): the next scene cycle starts after it.
inline char gLastScene[kChannelCount][40];

// Register lists the pins only; the console picks each channel's kind.
inline void channelsAppendJson(String &out) {
  out += '[';
  for (size_t i = 0; i < kChannelCount; i++) {
    if (i) {
      out += ',';
    }
    out += "{\"id\":";
    jsonAppendEscaped(out, kChannels[i].id);
    out += ",\"gpio\":";
    out += String(kChannels[i].gpio);
    out += ",\"label\":";
    jsonAppendEscaped(out, kChannels[i].label);
    out += '}';
  }
  out += ']';
}

inline bool channelIsBoot(size_t i) { return strcmp(kChannels[i].id, "boot") == 0; }

inline void channelLastSceneKey(size_t i, char *key, size_t n) { snprintf(key, n, "ls_%s", kChannels[i].id); }

inline void channelsLoadLastScenes() {
  Preferences prefs;
  const bool ok = prefs.begin("recipes", true);
  for (size_t i = 0; i < kChannelCount; i++) {
    gLastScene[i][0] = 0;
    char key[16];
    channelLastSceneKey(i, key, sizeof(key));
    if (ok && prefs.isKey(key)) {
      prefs.getString(key, gLastScene[i], sizeof(gLastScene[i]));
    }
  }
  if (ok) {
    prefs.end();
  }
}

inline void channelSetLastScene(size_t i, const char *rid) {
  if (strcmp(gLastScene[i], rid) == 0) {
    return;
  }
  recipeCopyField(gLastScene[i], sizeof(gLastScene[i]), rid);
  char key[16];
  channelLastSceneKey(i, key, sizeof(key));
  Preferences prefs;
  if (!prefs.begin("recipes", false)) {
    return;
  }
  if (rid[0]) {
    prefs.putString(key, rid);
  } else if (prefs.isKey(key)) {
    prefs.remove(key);
  }
  prefs.end();
}

// Next scene after the last one this channel set; wrap; the first when there is none.
// A 404 (scene deleted in the Hue app) skips to the next one.
inline bool channelRecallNextScene(size_t i, const HueRecipe &r) {
  uint8_t start = 0;
  for (uint8_t k = 0; k < r.sceneCount; k++) {
    if (gLastScene[i][0] && strcmp(r.scenes[k], gLastScene[i]) == 0) {
      start = (k + 1) % r.sceneCount;
      break;
    }
  }
  for (uint8_t n = 0; n < r.sceneCount; n++) {
    const char *rid = r.scenes[(start + n) % r.sceneCount];
    const int code = hueRecallScene(rid);
    if (code == HTTP_CODE_OK) {
      channelSetLastScene(i, rid);
      return true;
    }
    if (code != HTTP_CODE_NOT_FOUND) {
      return false;
    }
  }
  return false;
}

// Hold with dim: one GET, maybe turn on at the minimum, then start the ramp. Off → up;
// ≥ 95 % → down; ≤ 5 % → up; otherwise the other way from the last ramp. A failed GET
// flips direction without turning the light on. Never turns the light off.
inline bool channelDimStart(size_t i, const HueRecipe &r) {
  DimRuntime &d = gDim[i];
  bool up = !d.lastUp;
  bool on = false;
  float bri = -1;
  if (!hueGetOn(r.rtype, r.rid, &on, &bri)) {
    LOGLN("Dim: GET failed, ramping the other way");
  } else if (!on) {
    if (!hueDimFromOff(r.rtype, r.rid)) {
      return false;
    }
    up = true;
  } else if (bri >= 95) {
    up = false;
  } else if (bri >= 0 && bri <= 5) {
    up = true;
  }
  d.lastUp = up;
  if (!hueDimStart(r.rtype, r.rid, up)) {
    return false;
  }
  recipeCopyField(d.rtype, sizeof(d.rtype), r.rtype);
  recipeCopyField(d.rid, sizeof(d.rid), r.rid);
  d.active = true;
  return true;
}

// Release (or the channel stopped being a push button). A failed stop is not retried: the
// ramp ends on its own at full or at the minimum.
inline void channelDimStop(size_t i) {
  DimRuntime &d = gDim[i];
  d.active = false;
  if (WiFi.status() != WL_CONNECTED) {
    LOG("%s dim stop skipped: WiFi down\n", kChannels[i].id);
    return;
  }
  if (!hueDimStop(d.rtype, d.rid)) {
    LOGLN("Hue dim stop failed");
  }
}

// false if the channel has no recipe for this event.
inline bool channelFire(size_t i, const char *event) {
  const char *channelId = kChannels[i].id;
  // An off on the channel restarts its scene cycle, with or without an off recipe.
  if (strcmp(event, "off") == 0) {
    channelSetLastScene(i, "");
  }
  HueRecipe r;
  recipesLock();
  const HueRecipe *found = recipesFind(channelId, event);
  if (found) {
    r = *found;
  }
  recipesUnlock();
  if (!found) {
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG("%s %s skipped: WiFi down\n", channelId, event);
    return true;
  }
  bool ok = false;
  if (r.sceneCount) {
    LOG("%s %s -> recall_scene (%u scenes)\n", channelId, event, r.sceneCount);
    ok = channelRecallNextScene(i, r);
  } else if (strcmp(r.action, "dim") == 0) {
    LOG("%s %s -> dim %s/%s\n", channelId, event, r.rtype, r.rid);
    ok = channelDimStart(i, r);
  } else {
    LOG("%s %s -> %s %s/%s\n", channelId, event, r.action, r.rtype, r.rid);
    ok = hueExecute(r.action, r.rtype, r.rid);
    if (ok && strcmp(r.action, "off") == 0) {
      channelSetLastScene(i, "");
    }
  }
  if (!ok) {
    LOGLN("Hue action failed");
  }
  return true;
}

inline void channelPrime(size_t i) {
  const int v = digitalRead(kChannels[i].gpio);
  gCh[i].lastReading = v;
  gCh[i].stable = v;
  gCh[i].lastChangeMs = millis();
  gCh[i].primed = true;
  gCh[i].waitOff = false;
  gCh[i].waitStartMs = 0;
  gCh[i].pressStartMs = 0;
  gCh[i].longPressHandled = false;
}

inline void channelsPrime() {
  for (size_t i = 0; i < kChannelCount; i++) {
    channelPrime(i);
  }
}

// A channel whose kind changed starts from the current pin state, without firing.
inline void channelsResolveModes() {
  if (gChModeValid && gRecipesGen == gChModeGen) {
    return;
  }
  recipesLock();
  for (size_t i = 0; i < kChannelCount; i++) {
    const char *id = kChannels[i].id;
    uint8_t kind = gChannelsFromConsole ? recipesChannelKind(id) : kChannels[i].defaultKind;
    // BOOT is the pairing button: always a push button, even when not configured, so its
    // 3 s re-pair keeps working.
    if (channelIsBoot(i)) {
      kind = CHK_MOMENTARY;
    }
    if (gChModeValid && kind != gChMode[i].kind) {
      channelPrime(i);
    }
    gChMode[i].kind = kind;
    gChMode[i].hasDoubleClick = recipesFind(id, "double_click") != nullptr;
    gChMode[i].hasHold = recipesFind(id, "hold") != nullptr;
  }
  gChModeGen = gRecipesGen;
  recipesUnlock();
  gChModeValid = true;
  for (size_t i = 0; i < kChannelCount; i++) {
    LOG("%s: %s%s%s\n", kChannels[i].id,
        gChMode[i].kind == CHK_NONE ? "not configured"
                                     : (gChMode[i].kind == CHK_MOMENTARY ? "push button" : "toggle switch"),
        gChMode[i].hasDoubleClick ? ", double-click" : "", gChMode[i].hasHold ? ", hold" : "");
  }
}

inline void channelsBegin() {
  for (size_t i = 0; i < kChannelCount; i++) {
    pinMode(kChannels[i].gpio, INPUT_PULLUP);
  }
  channelsLoadLastScenes();
  channelsPrime();
}

// Toggle switch: closed = on; opened = off after the window; opened and closed again
// inside the window = double_click (on when there is no double_click recipe).
inline void channelMaintained(size_t i, unsigned long now) {
  ChannelRuntime &st = gCh[i];
  const ChannelDef &ch = kChannels[i];
  const int reading = digitalRead(ch.gpio);

  if (reading != st.lastReading) {
    st.lastChangeMs = now;
    st.lastReading = reading;
  }

  if (st.waitOff && (now - st.waitStartMs) >= kDoubleClickMs && st.stable == HIGH &&
      st.lastReading == HIGH) {
    st.waitOff = false;
    channelFire(i, "off");
  }

  if ((now - st.lastChangeMs) < kDebounceMs) {
    return;
  }
  if (reading == st.stable) {
    return;
  }

  st.stable = reading;
  const bool closed = (reading == LOW);

  if (!closed) {
    // Closed → open: don't fire off yet; start the double-click window.
    st.waitOff = true;
    st.waitStartMs = now;
    return;
  }

  if (st.waitOff) {
    st.waitOff = false;
    if (!channelFire(i, "double_click")) {
      LOG("%s double_click: no recipe, fallback on\n", ch.id);
      channelFire(i, "on");
    }
    return;
  }
  channelFire(i, "on");
}

inline void channelBootRePair() {
  if (WiFi.status() != WL_CONNECTED) {
    LOGLN("Re-pair skipped: WiFi down");
    return;
  }
  if (huePairBusy()) {
    return;
  }
  huePairSessionBegin();
}

// Still pressed. With a hold recipe: hold at kHoldMs (BOOT then never re-pairs from the
// button). Without one: no hold threshold, except BOOT's 3 s re-pair.
inline void channelMomentaryHeld(size_t i, unsigned long now) {
  ChannelRuntime &st = gCh[i];
  const unsigned long held = now - st.pressStartMs;
  if (gChMode[i].hasHold) {
    if (held >= kHoldMs) {
      st.longPressHandled = true;
      channelFire(i, "hold");
    }
    return;
  }
  if (channelIsBoot(i) && held >= kLongPressMs) {
    st.longPressHandled = true;
    channelBootRePair();
  }
}

// Push button: short on release, at once when there is no double_click recipe; otherwise a
// second press inside the window is double_click and an expired window is short.
inline void channelMomentary(size_t i, unsigned long now) {
  ChannelRuntime &st = gCh[i];
  const ChannelDef &ch = kChannels[i];
  const int reading = digitalRead(ch.gpio);

  if (reading != st.lastReading) {
    st.lastChangeMs = now;
    st.lastReading = reading;
  }

  if (st.waitOff && (now - st.waitStartMs) >= kDoubleClickMs && st.stable == HIGH &&
      st.lastReading == HIGH) {
    st.waitOff = false;
    channelFire(i, "short");
  }

  if (st.stable == LOW && st.pressStartMs && !st.longPressHandled) {
    channelMomentaryHeld(i, now);
  }

  if ((now - st.lastChangeMs) < kDebounceMs) {
    return;
  }
  if (reading == st.stable) {
    return;
  }

  st.stable = reading;
  if (reading == LOW) {
    st.pressStartMs = now;
    st.longPressHandled = false;
    if (st.waitOff) {
      // Second press inside the window: its release and hold do nothing.
      st.waitOff = false;
      st.longPressHandled = true;
      channelFire(i, "double_click");
    }
    return;
  }

  const bool handled = st.longPressHandled || !st.pressStartMs;
  st.pressStartMs = 0;
  if (handled) {
    return;
  }
  if (gChMode[i].hasDoubleClick) {
    st.waitOff = true;
    st.waitStartMs = now;
    return;
  }
  channelFire(i, "short");
}

inline void channelsPoll(unsigned long now) {
  channelsResolveModes();
  for (size_t i = 0; i < kChannelCount; i++) {
    if (!gCh[i].primed) {
      continue;
    }
    if (gChMode[i].kind == CHK_MAINTAINED) {
      channelMaintained(i, now);
    } else if (gChMode[i].kind == CHK_MOMENTARY) {
      channelMomentary(i, now);
    }
    // The GPIO loop blocks during the start PUT, so a release that came meanwhile is seen here
    // right after it: the stop always follows the start.
    if (gDim[i].active && (gChMode[i].kind != CHK_MOMENTARY || gCh[i].stable == HIGH)) {
      channelDimStop(i);
    }
  }
}
