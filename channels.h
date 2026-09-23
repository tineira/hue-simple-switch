#pragma once

#include <WiFi.h>
#include "hue.h"
#include "hue_discover.h"
#include "recipes.h"

// Channels v1: closed = GPIO to GND (INPUT_PULLUP).

enum ChannelKind { CH_MAINTAINED, CH_MOMENTARY };

struct ChannelDef {
  const char *id;
  int gpio;
  const char *label;
  ChannelKind kind;
};

static const ChannelDef kChannels[] = {
    {"boot", 9, "BOOT", CH_MOMENTARY},
    {"d0", 0, "D0", CH_MAINTAINED},
    {"d1", 1, "D1", CH_MAINTAINED},
    {"d2", 2, "D2", CH_MAINTAINED},
};

static const size_t kChannelCount = sizeof(kChannels) / sizeof(kChannels[0]);
static const unsigned long kDebounceMs = 50;
static const unsigned long kDoubleClickMs = 400;

struct ChannelRuntime {
  int lastReading;
  int stable;  // HIGH = open, LOW = closed
  unsigned long lastChangeMs;
  bool primed;
  bool waitOff;
  unsigned long waitStartMs;
  unsigned long pressStartMs;
  bool longPressHandled;
};

inline ChannelRuntime gCh[kChannelCount];

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
    out += ",\"kind\":";
    jsonAppendEscaped(out, kChannels[i].kind == CH_MOMENTARY ? "momentary" : "maintained");
    out += '}';
  }
  out += ']';
}

inline void recipeFire(const char *channelId, const char *event) {
  char action[16];
  char rtype[16];
  char rid[40];
  recipesLock();
  const HueRecipe *r = recipesFind(channelId, event);
  if (!r && strcmp(event, "double_click") == 0) {
    LOG("%s double_click: no recipe, fallback on\n", channelId);
    r = recipesFind(channelId, "on");
  }
  if (!r) {
    recipesUnlock();
    return;
  }
  recipeCopyField(action, sizeof(action), r->action);
  recipeCopyField(rtype, sizeof(rtype), r->rtype);
  recipeCopyField(rid, sizeof(rid), r->rid);
  recipesUnlock();
  if (WiFi.status() != WL_CONNECTED) {
    LOG("%s %s skipped: WiFi down\n", channelId, event);
    return;
  }
  LOG("%s %s -> %s %s/%s\n", channelId, event, action, rtype, rid);
  if (!hueExecute(action, rtype, rid)) {
    LOGLN("Hue action failed");
  }
}

inline void channelsPrime() {
  for (size_t i = 0; i < kChannelCount; i++) {
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
}

inline void channelsBegin() {
  for (size_t i = 0; i < kChannelCount; i++) {
    pinMode(kChannels[i].gpio, INPUT_PULLUP);
  }
  channelsPrime();
}

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
    recipeFire(ch.id, "off");
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
    recipeFire(ch.id, "double_click");
    return;
  }
  recipeFire(ch.id, "on");
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

inline void channelMomentary(size_t i, unsigned long now) {
  ChannelRuntime &st = gCh[i];
  const ChannelDef &ch = kChannels[i];
  const int reading = digitalRead(ch.gpio);

  if (reading != st.lastReading) {
    st.lastChangeMs = now;
    st.lastReading = reading;
  }

  if ((now - st.lastChangeMs) < kDebounceMs) {
    if (st.stable == LOW && st.pressStartMs && !st.longPressHandled &&
        (now - st.pressStartMs) >= kLongPressMs) {
      st.longPressHandled = true;
      if (strcmp(ch.id, "boot") == 0) {
        channelBootRePair();
      }
    }
    return;
  }

  if (reading == st.stable) {
    if (st.stable == LOW && st.pressStartMs && !st.longPressHandled &&
        (now - st.pressStartMs) >= kLongPressMs) {
      st.longPressHandled = true;
      if (strcmp(ch.id, "boot") == 0) {
        channelBootRePair();
      }
    }
    return;
  }

  st.stable = reading;
  if (reading == LOW) {
    st.pressStartMs = now;
    st.longPressHandled = false;
    return;
  }

  if (st.longPressHandled || !st.pressStartMs) {
    st.pressStartMs = 0;
    return;
  }
  st.pressStartMs = 0;
  recipeFire(ch.id, "short");
}

inline void channelsPoll(unsigned long now) {
  for (size_t i = 0; i < kChannelCount; i++) {
    if (!gCh[i].primed) {
      continue;
    }
    if (kChannels[i].kind == CH_MAINTAINED) {
      channelMaintained(i, now);
    } else {
      channelMomentary(i, now);
    }
  }
}
