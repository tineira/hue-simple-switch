#pragma once

#include <stdint.h>

// Hue job queue rules (one channel's queue), without FreeRTOS or the Bridge, so the host tests
// run them. hue_worker.h owns the queues, the lock and the task; the rules are in its header.

enum HueJobEvent : uint8_t {
  HJ_ON = 0,
  HJ_OFF,
  HJ_DOUBLE,  // double_click
  HJ_SHORT,
  HJ_HOLD,
  HJ_RELEASE,  // end of a hold: stops a dim ramp the hold started
};

enum : uint8_t {
  HJF_RESET_SCENE = 1,  // restart the scene cycle before running (an `off` was dropped)
  HJF_FALLBACK_ON = 2,  // toggle-switch double_click: run `on` when there is no double_click recipe
  HJF_MAY_EXPIRE = 4,   // push-button gesture or toggle-mode flip: drop it when stale
  HJF_TOGGLE = 8,       // toggle-mode flip (`on` / `off`): queued in order, never merged by the lever
};

struct HueJobEntry {
  uint8_t event;
  uint8_t flags;
  uint32_t atMs;
  uint32_t epoch;
};

// Three events plus the release slot per channel.
static const uint8_t kHueJobDepth = 4;
// A gesture marked HJF_MAY_EXPIRE that waited this long for the Bridge is dropped instead of
// acting late.
static const unsigned long kHueJobStaleMs = 10000;

struct HueJobQueue {
  HueJobEntry e[kHueJobDepth];
  uint8_t n;
};

inline const char *hueJobEventName(uint8_t ev) {
  switch (ev) {
    case HJ_ON:
      return "on";
    case HJ_OFF:
      return "off";
    case HJ_DOUBLE:
      return "double_click";
    case HJ_SHORT:
      return "short";
    case HJ_HOLD:
      return "hold";
    default:
      return "release";
  }
}

// Set-mode lever event (`on` / `off` without HJF_TOGGLE): the lever position wins.
inline bool hueJobIsLever(const HueJobEntry &e) {
  return (e.event == HJ_ON || e.event == HJ_OFF) && !(e.flags & HJF_TOGGLE);
}

// Adds e to q under the queue rules. false: the queue is full and e was dropped. Caller holds
// the queue lock.
inline bool hueJobQueuePush(HueJobQueue &q, HueJobEntry e) {
  if (hueJobIsLever(e)) {
    uint8_t kept = 0;
    for (uint8_t k = 0; k < q.n; k++) {
      const HueJobEntry &p = q.e[k];
      const bool drop = (e.event == HJ_OFF) ? (p.event != HJ_RELEASE) : hueJobIsLever(p);
      if (drop) {
        if ((p.event == HJ_OFF && !(p.flags & HJF_TOGGLE)) || (p.flags & HJF_RESET_SCENE)) {
          e.flags |= HJF_RESET_SCENE;
        }
        continue;
      }
      q.e[kept++] = p;
    }
    q.n = kept;
  }
  const uint8_t cap = (e.event == HJ_RELEASE) ? kHueJobDepth : kHueJobDepth - 1;
  if (q.n >= cap) {
    return false;
  }
  q.e[q.n++] = e;
  return true;
}
