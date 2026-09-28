#pragma once

#include <WiFi.h>
#include <Preferences.h>
#include "hue.h"
#include "hue_discover.h"
#include "recipes.h"

// Channels: closed = GPIO to GND (INPUT_PULLUP). The console picks each channel's kind
// (config channels[]); a pin it does not list does nothing. The old config payload has no
// channels[]: every pin runs with its compiled default kind below. D3-D5 (since 0.5.0) default
// to CHK_NONE, so a pin nobody wired never acts on the old payload either.
// D3-D5 are GPIO 21-23: no strapping pins, and this firmware has no I2C on D4/D5.

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
    {"d3", 21, "D3", CHK_NONE},
    {"d4", 22, "D4", CHK_NONE},
    {"d5", 23, "D5", CHK_NONE},
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

inline ChannelRuntime gCh[kChannelCount];
inline ChannelMode gChMode[kChannelCount];
inline uint32_t gChModeGen = 0;
inline bool gChModeValid = false;
// A hold was posted and its release is not yet: the release goes to the Hue worker, which
// stops the dim ramp if the hold started one (GPIO loop only).
inline bool gHoldOpen[kChannelCount];

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

#include "hue_worker.h"

// Post the event to the Hue worker; the recipe is looked up when it runs.
inline bool channelPost(size_t i, uint8_t event, uint8_t flags = 0) { return hueJobPost(i, event, flags); }

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
    channelPost(i, HJ_OFF);
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
    // With no double_click recipe the worker runs on instead.
    channelPost(i, HJ_DOUBLE, HJF_FALLBACK_ON);
    return;
  }
  channelPost(i, HJ_ON);
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
      if (channelPost(i, HJ_HOLD, HJF_MAY_EXPIRE)) {
        gHoldOpen[i] = true;
      }
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
    channelPost(i, HJ_SHORT, HJF_MAY_EXPIRE);
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
      channelPost(i, HJ_DOUBLE, HJF_MAY_EXPIRE);
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
  channelPost(i, HJ_SHORT, HJF_MAY_EXPIRE);
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
    // The release queues behind its hold on this channel, so the stop always follows the start.
    if (gHoldOpen[i] && (gChMode[i].kind != CHK_MOMENTARY || gCh[i].stable == HIGH)) {
      gHoldOpen[i] = false;
      channelPost(i, HJ_RELEASE);
    }
  }
}
