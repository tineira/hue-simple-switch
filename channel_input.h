#pragma once

#include <Arduino.h>
#include "hue_jobs.h"

// Pin state and the toggle switch's event rules, without pin reads or the Hue worker, so the
// host tests run them. channels.h reads the pin and posts what this returns.

static const unsigned long kDebounceMs = 50;
static const unsigned long kDoubleClickMs = 400;
static const unsigned long kHoldMs = 800;

struct ChannelRuntime {
  int lastReading;
  int stable;  // HIGH = open, LOW = closed
  unsigned long lastChangeMs;
  bool primed;
  // A double-click window is open. Toggle switch, flip set: opened, waiting for off. Flip
  // toggle: flipped, waiting for a second flip (waitEvent runs if none comes). Push button:
  // released, waiting for a second press.
  bool waitOff;
  uint8_t waitEvent;
  unsigned long waitStartMs;
  unsigned long pressStartMs;
  bool longPressHandled;
};

// Toggle switch, one pass with the pin's reading. true: post *event with *flags.
//
// Flip set (the lever sets on or off): closed = on; opened = off after the window; opened and
// closed again inside the window = double_click (on when there is no double_click recipe).
//
// Flip toggle (each flip toggles; console docs/specs/toggle-on-flip.md): the lever position
// means nothing. A flip posts its event (closed -> on, open -> off; the console maps both to
// toggle) at once when there is no double_click recipe. With one, a flip either way starts the
// window: a second flip inside it is double_click, and an expired window posts the first flip.
// No fallback: without a recipe a quick flick is two toggles.
inline bool channelMaintainedStep(ChannelRuntime &st, int reading, unsigned long now, bool flipToggle,
                                  bool hasDoubleClick, uint8_t *event, uint8_t *flags) {
  if (reading != st.lastReading) {
    st.lastChangeMs = now;
    st.lastReading = reading;
  }

  // The window expired with the lever still where the flip left it (not mid-bounce). In set
  // mode the window only opens on an open lever, and waitEvent is off.
  if (st.waitOff && (now - st.waitStartMs) >= kDoubleClickMs && st.lastReading == st.stable) {
    st.waitOff = false;
    *event = st.waitEvent;
    *flags = flipToggle ? (HJF_TOGGLE | HJF_MAY_EXPIRE) : 0;
    return true;
  }

  if ((now - st.lastChangeMs) < kDebounceMs) {
    return false;
  }
  if (reading == st.stable) {
    return false;
  }

  st.stable = reading;
  const bool closed = (reading == LOW);

  if (flipToggle) {
    if (st.waitOff) {
      st.waitOff = false;
      *event = HJ_DOUBLE;
      *flags = HJF_MAY_EXPIRE;
      return true;
    }
    const uint8_t ev = closed ? HJ_ON : HJ_OFF;
    if (hasDoubleClick) {
      st.waitOff = true;
      st.waitEvent = ev;
      st.waitStartMs = now;
      return false;
    }
    *event = ev;
    *flags = HJF_TOGGLE | HJF_MAY_EXPIRE;
    return true;
  }

  if (!closed) {
    // Closed -> open: don't fire off yet; start the double-click window.
    st.waitOff = true;
    st.waitEvent = HJ_OFF;
    st.waitStartMs = now;
    return false;
  }

  if (st.waitOff) {
    st.waitOff = false;
    // With no double_click recipe the worker runs on instead.
    *event = HJ_DOUBLE;
    *flags = HJF_FALLBACK_ON;
    return true;
  }
  *event = HJ_ON;
  *flags = 0;
  return true;
}
