#pragma once

// Hue worker: a FreeRTOS task that runs every gesture's Clip v2 calls, so the GPIO loop never
// waits on the Bridge. The loop reads pins and posts (channel, event); this task looks up the
// recipe, calls the Bridge, and owns the scene cursor and the dim state. Included by
// channels.h after the channel table (kChannels, kChannelCount).
//
// Queue: one short FIFO per channel, served round-robin, so a slow call on one channel delays
// another channel by at most one job, and an event on one channel never drops another's.
// Per channel:
//   - Toggle switch (maintained): the lever position wins. An `off` drops every pending event on
//     the channel (they would be undone by it, and it restarts the scene cycle anyway). An `on`
//     drops a pending `on`/`off`; a dropped `off` still restarts the scene cycle. Double-clicks
//     queue in order. These events never expire: the lights follow the lever, even late.
//   - Push button (momentary): short / double_click / hold queue in order (a click is a toggle, so
//     none may be merged). One that waited more than kHueJobStaleMs for the Bridge is dropped.
//   - Release: queued after the hold it ends, so a dim stop always follows its start. It has a
//     slot of its own and never expires.
// Hold to dim cycle (console docs/specs/simple-dim-cycle.md): the hold job reads the target
// once and starts the first leg; gDim records when the next leg is due. The task waits for its
// next job or for the earliest due turn-around, whichever comes first, and runs a due
// turn-around as one PUT between jobs, so a cycle never blocks other channels and they delay it
// by at most one job. A turn-around is skipped while its channel has a release queued: the
// release cancels it and sends the stop. Without a task, the loop runs due turn-arounds.
// A full channel drops the new event (and the loop does not open a hold for it).
//
// Shared state:
//   - Recipes: looked up under gRecipesMux when the job runs (the console task swaps them).
//   - Bridge IP / key / id: the loop is the only task that changes gHueBridgeIp / gHueAppKey /
//     gHueBridgeId. It
//     publishes a copy (hueWorkerPublishCreds) under gHueJobMux; each job takes that copy, and
//     so does each console sync (hueCredsCopy).
//   - HUECLR bumps gNvsEpoch: a job posted before it is dropped, the RAM scene cursors are reset,
//     and the cursor is written to NVS only under gRecipesMux with the epoch unchanged, so a wipe
//     is never undone.
//   - Scene cursor (gLastScene, NVS recipes/ls_<id>) and gDim: this task only, after setup.

#include <WiFi.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "hue.h"
#include "recipes.h"

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
  HJF_MAY_EXPIRE = 4,   // push-button gesture: drop it when stale
};

struct HueJobEntry {
  uint8_t event;
  uint8_t flags;
  uint32_t atMs;
  uint32_t epoch;
};

// Three events plus the release slot per channel.
static const uint8_t kHueJobDepth = 4;
// A push-button gesture that waited this long for the Bridge is dropped instead of acting late.
static const unsigned long kHueJobStaleMs = 10000;
// Same as the loop task's stack, which ran these calls up to 0.4.2 (TLS handshake included).
// The recipe copy is a global, not on this stack. Debug builds log the free stack after each job.
static const uint32_t kHueWorkerStack = 8192;

struct HueJobQueue {
  HueJobEntry e[kHueJobDepth];
  uint8_t n;
};

// Hold to dim (RAM only). active: a leg was sent and needs its stop on release; the target is
// kept so the turn-arounds and the stop go where the start went, even if recipes change
// mid-hold. cycling: the next leg is due at nextMs (end of the running leg plus the dwell).
struct DimRuntime {
  bool active;
  bool cycling;
  bool up;          // direction of the running leg
  uint32_t holdMs;  // when the hold was detected: the cycle stops kDimMaxHoldMs after it
  uint32_t nextMs;
  char rtype[16];
  char rid[40];
};

inline portMUX_TYPE gHueJobMux = portMUX_INITIALIZER_UNLOCKED;
inline HueJobQueue gHueJobs[kChannelCount];  // gHueJobMux
inline HueCreds gHueCredsShared;             // gHueJobMux; written by the loop only
inline HueCreds gHueCredsLoop;               // loop task only: last published copy
inline TaskHandle_t gHueTask = nullptr;

// Worker only (setup loads gLastScene before the task starts).
inline DimRuntime gDim[kChannelCount];
// Last scene rid each channel set (NVS recipes/ls_<id>): the next scene cycle starts after it.
inline char gLastScene[kChannelCount][40];
inline HueRecipe gHueJobRecipe;  // the running job's recipe, off the task stack
inline uint32_t gHueJobEpoch = 0;
// A job is running (worker task). With a queued job it keeps an update from starting (ota.h).
inline volatile bool gHueJobRunning = false;

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

// ---------------------------------------------------------------- loop side

// Loop task only: it is the only writer of gHueBridgeIp / gHueAppKey / gHueBridgeId. Call it
// each loop pass, before posting jobs, and before setting gNeedConsoleSync. A value too long for
// the copy publishes as empty (the calls then fail).
inline void hueWorkerPublishCreds() {
  HueCreds c;
  memset(&c, 0, sizeof(c));
  if (gHueBridgeIp.length() < sizeof(c.ip)) {
    memcpy(c.ip, gHueBridgeIp.c_str(), gHueBridgeIp.length());
  }
  if (gHueAppKey.length() < sizeof(c.key)) {
    memcpy(c.key, gHueAppKey.c_str(), gHueAppKey.length());
  }
  if (gHueBridgeId.length() < sizeof(c.bid)) {
    memcpy(c.bid, gHueBridgeId.c_str(), gHueBridgeId.length());
  }
  if (memcmp(&c, &gHueCredsLoop, sizeof(c)) == 0) {
    return;
  }
  gHueCredsLoop = c;
  portENTER_CRITICAL(&gHueJobMux);
  gHueCredsShared = c;
  portEXIT_CRITICAL(&gHueJobMux);
}

// Any task: the last copy the loop published. The console task reads the Bridge through this,
// never through the Strings, which the loop can free mid-read when it re-pairs or clears them.
inline HueCreds hueCredsCopy() {
  HueCreds c;
  portENTER_CRITICAL(&gHueJobMux);
  c = gHueCredsShared;
  portEXIT_CRITICAL(&gHueJobMux);
  return c;
}

inline void hueWorkerRunPending();

// Loop task only. false: the channel's queue was full and the event was dropped.
inline bool hueJobPost(size_t i, uint8_t event, uint8_t flags) {
  if (i >= kChannelCount) {
    return false;
  }
  HueJobEntry e = {event, flags, millis(), gNvsEpoch};
  bool ok = false;
  portENTER_CRITICAL(&gHueJobMux);
  HueJobQueue &q = gHueJobs[i];
  if (event == HJ_OFF || event == HJ_ON) {
    uint8_t kept = 0;
    for (uint8_t k = 0; k < q.n; k++) {
      const HueJobEntry &p = q.e[k];
      const bool drop = (event == HJ_OFF) ? (p.event != HJ_RELEASE) : (p.event == HJ_ON || p.event == HJ_OFF);
      if (drop) {
        if (p.event == HJ_OFF || (p.flags & HJF_RESET_SCENE)) {
          e.flags |= HJF_RESET_SCENE;
        }
        continue;
      }
      q.e[kept++] = p;
    }
    q.n = kept;
  }
  const uint8_t cap = (event == HJ_RELEASE) ? kHueJobDepth : kHueJobDepth - 1;
  if (q.n < cap) {
    q.e[q.n++] = e;
    ok = true;
  }
  portEXIT_CRITICAL(&gHueJobMux);
  if (!ok) {
    LOG("%s %s dropped: Hue queue full\n", kChannels[i].id, hueJobEventName(event));
    return false;
  }
  if (gHueTask) {
    xTaskNotifyGive(gHueTask);
  } else {
    hueWorkerRunPending();  // no task (create failed at boot): run here, as up to 0.4.2
  }
  return true;
}

// ---------------------------------------------------------------- worker side

inline void channelLastSceneKey(size_t i, char *key, size_t n) { snprintf(key, n, "ls_%s", kChannels[i].id); }

// Setup, before the task starts.
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

// NVS under gRecipesMux, and only if HUECLR has not wiped the namespace since this job was posted.
inline void channelSetLastScene(size_t i, const char *rid, uint32_t epoch) {
  if (strcmp(gLastScene[i], rid) == 0) {
    return;
  }
  recipeCopyField(gLastScene[i], sizeof(gLastScene[i]), rid);
  char key[16];
  channelLastSceneKey(i, key, sizeof(key));
  recipesLock();
  if (gNvsEpoch == epoch) {
    Preferences prefs;
    if (prefs.begin("recipes", false)) {
      if (rid[0]) {
        prefs.putString(key, rid);
      } else if (prefs.isKey(key)) {
        prefs.remove(key);
      }
      prefs.end();
    }
  }
  recipesUnlock();
}

// Next scene after the last one this channel set; wrap; the first when there is none.
// A 404 (scene deleted in the Hue app) skips to the next one.
inline bool channelRecallNextScene(size_t i, const HueRecipe &r, const HueCreds &c, uint32_t epoch) {
  uint8_t start = 0;
  for (uint8_t k = 0; k < r.sceneCount; k++) {
    if (gLastScene[i][0] && strcmp(r.scenes[k], gLastScene[i]) == 0) {
      start = (k + 1) % r.sceneCount;
      break;
    }
  }
  for (uint8_t n = 0; n < r.sceneCount; n++) {
    const char *rid = r.scenes[(start + n) % r.sceneCount];
    const int code = hueRecallScene(c, rid);
    if (code == HTTP_CODE_OK) {
      channelSetLastScene(i, rid, epoch);
      return true;
    }
    if (code != HTTP_CODE_NOT_FOUND) {
      return false;
    }
  }
  return false;
}

// Hold with dim: one GET (the only read in the gesture), maybe turn on at the minimum, then
// start the first leg, timed from the distance left. Off -> on at the minimum, up; below
// kDimUpBelow -> up; otherwise, or when the GET failed or gave no brightness -> down, as a
// full sweep. Never turns the light off. holdMs: when the hold was detected (start of the cap).
inline bool channelDimStart(size_t i, const HueRecipe &r, const HueCreds &c, uint32_t holdMs) {
  DimRuntime &d = gDim[i];
  d.active = false;
  d.cycling = false;
  bool up = false;
  bool on = false;
  float bri = -1;
  if (!hueGetOn(c, r.rtype, r.rid, &on, &bri)) {
    LOGLN("Dim: GET failed, going down with a full sweep");
    bri = -1;
  } else if (!on) {
    if (!hueDimFromOff(c, r.rtype, r.rid)) {
      return false;
    }
    bri = kDimMinBrightness;
    up = true;
  } else if (bri >= 0 && bri < kDimUpBelow) {
    up = true;
  }
  const float distance = bri < 0 ? 100.0f : (up ? 100.0f - bri : bri - kDimMinBrightness);
  const unsigned long legMs = hueDimLegMs(distance);
  recipeCopyField(d.rtype, sizeof(d.rtype), r.rtype);
  recipeCopyField(d.rid, sizeof(d.rid), r.rid);
  d.up = up;
  d.holdMs = holdMs;
  // The release sends its stop even when this PUT fails: it may have reached the Bridge.
  d.active = true;
  if (!hueDimStart(c, r.rtype, r.rid, up, legMs)) {
    LOGLN("Dim: start failed, no cycle");
    return false;
  }
  d.nextMs = millis() + legMs + kDimDwellMs;
  d.cycling = true;
  return true;
}

// When the channel's cycle next needs a call: the turn-around, or the cap if it comes first.
inline uint32_t channelDimDueMs(const DimRuntime &d) {
  const uint32_t cap = d.holdMs + kDimMaxHoldMs;
  return static_cast<int32_t>(d.nextMs - cap) < 0 ? d.nextMs : cap;
}

// Release (or the channel stopped being a push button): cancels any pending turn-around and
// stops the running leg. A failed stop is not retried: the leg ends on its own at full or at
// the minimum.
inline void channelDimStop(size_t i, const HueCreds &c) {
  DimRuntime &d = gDim[i];
  d.cycling = false;
  if (!d.active) {
    return;
  }
  d.active = false;
  if (WiFi.status() != WL_CONNECTED) {
    LOG("%s dim stop skipped: WiFi down\n", kChannels[i].id);
    return;
  }
  if (!hueDimStop(c, d.rtype, d.rid)) {
    LOGLN("Hue dim stop failed");
  }
}

// HUECLR wiped NVS: forget the cursors and dim state it no longer backs.
inline void hueWorkerSyncEpoch() {
  const uint32_t epoch = gNvsEpoch;
  if (epoch != gHueJobEpoch) {
    gHueJobEpoch = epoch;
    for (size_t k = 0; k < kChannelCount; k++) {
      gLastScene[k][0] = 0;
      gDim[k].active = false;
      gDim[k].cycling = false;
    }
  }
}

// A due turn-around (or the cap) on a channel still held. The switch keeps its own estimate
// of where the light is: after a turn-around the leg is a full sweep, with no GET. A failed
// PUT stops the cycle; the release still sends its stop.
inline void channelDimTurn(size_t i, const HueCreds &c) {
  hueWorkerSyncEpoch();
  DimRuntime &d = gDim[i];
  if (!d.cycling) {
    return;
  }
  if (static_cast<int32_t>(millis() - (d.holdMs + kDimMaxHoldMs)) >= 0) {
    // A stuck button or a noisy pin must not cycle forever: stop, and ignore the rest of the hold.
    LOG("%s dim: held %lu s, stopping\n", kChannels[i].id, kDimMaxHoldMs / 1000);
    channelDimStop(i, c);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG("%s dim turn-around skipped: WiFi down, cycle stopped\n", kChannels[i].id);
    d.cycling = false;
    return;
  }
  d.up = !d.up;
  if (!hueDimStart(c, d.rtype, d.rid, d.up, kDimSweepMs)) {
    LOGLN("Dim: turn-around failed, cycle stopped");
    d.cycling = false;
    return;
  }
  d.nextMs = millis() + kDimSweepMs + kDimDwellMs;
}

// Copies the (channel, event) recipe into gHueJobRecipe. false: none.
inline bool hueJobFindRecipe(const char *channelId, const char *event) {
  recipesLock();
  const HueRecipe *found = recipesFind(channelId, event);
  if (found) {
    gHueJobRecipe = *found;
  }
  recipesUnlock();
  return found != nullptr;
}

inline void hueJobRun(size_t i, const HueJobEntry &job, const HueCreds &c) {
  const char *channelId = kChannels[i].id;
  const char *event = hueJobEventName(job.event);
  hueWorkerSyncEpoch();
  const uint32_t epoch = gHueJobEpoch;
  if (job.epoch != epoch) {
    LOG("%s %s dropped: settings cleared since\n", channelId, event);
    return;
  }
  if (job.event == HJ_RELEASE) {
    channelDimStop(i, c);
    return;
  }
  // An off on the channel restarts its scene cycle, with or without an off recipe.
  if (job.event == HJ_OFF || (job.flags & HJF_RESET_SCENE)) {
    channelSetLastScene(i, "", job.epoch);
  }
  if ((job.flags & HJF_MAY_EXPIRE) && (millis() - job.atMs) > kHueJobStaleMs) {
    LOG("%s %s dropped: waited too long for the Bridge\n", channelId, event);
    return;
  }
  if (!hueJobFindRecipe(channelId, event)) {
    if (!(job.flags & HJF_FALLBACK_ON)) {
      return;
    }
    LOG("%s double_click: no recipe, fallback on\n", channelId);
    event = "on";
    if (!hueJobFindRecipe(channelId, event)) {
      return;
    }
  }
  const HueRecipe &r = gHueJobRecipe;
  if (WiFi.status() != WL_CONNECTED) {
    LOG("%s %s skipped: WiFi down\n", channelId, event);
    return;
  }
  bool ok = false;
  if (r.sceneCount) {
    LOG("%s %s -> recall_scene (%u scenes)\n", channelId, event, r.sceneCount);
    ok = channelRecallNextScene(i, r, c, job.epoch);
  } else if (strcmp(r.action, "dim") == 0) {
    LOG("%s %s -> dim %s/%s\n", channelId, event, r.rtype, r.rid);
    ok = channelDimStart(i, r, c, job.atMs);
  } else {
    LOG("%s %s -> %s %s/%s\n", channelId, event, r.action, r.rtype, r.rid);
    ok = hueExecute(c, r.action, r.rtype, r.rid);
    if (ok && strcmp(r.action, "off") == 0) {
      channelSetLastScene(i, "", job.epoch);
    }
  }
  if (!ok) {
    LOGLN("Hue action failed");
  }
}

// Pops the next job, round-robin from *next, with the current Bridge credentials.
inline bool hueJobTake(size_t *next, size_t *ch, HueJobEntry *job, HueCreds *creds) {
  bool found = false;
  portENTER_CRITICAL(&gHueJobMux);
  for (size_t n = 0; n < kChannelCount; n++) {
    const size_t i = (*next + n) % kChannelCount;
    HueJobQueue &q = gHueJobs[i];
    if (!q.n) {
      continue;
    }
    *job = q.e[0];
    for (uint8_t k = 1; k < q.n; k++) {
      q.e[k - 1] = q.e[k];
    }
    q.n--;
    *ch = i;
    *next = (i + 1) % kChannelCount;
    *creds = gHueCredsShared;
    gHueJobRunning = true;  // under the lock: the job is never both dequeued and not running
    found = true;
    break;
  }
  portEXIT_CRITICAL(&gHueJobMux);
  return found;
}

// Picks a channel whose dim turn-around (or cap) is due and that has no release queued, with
// the current Bridge credentials. *waitMs: time until the earliest turn-around still to come
// (UINT32_MAX when none), for the task's wait.
inline bool hueDimTakeDue(size_t *ch, HueCreds *creds, uint32_t *waitMs) {
  const uint32_t now = millis();
  uint32_t wait = UINT32_MAX;
  bool found = false;
  portENTER_CRITICAL(&gHueJobMux);
  for (size_t i = 0; i < kChannelCount; i++) {
    const DimRuntime &d = gDim[i];
    if (!d.cycling) {
      continue;
    }
    bool released = false;
    const HueJobQueue &q = gHueJobs[i];
    for (uint8_t k = 0; k < q.n && !released; k++) {
      released = q.e[k].event == HJ_RELEASE;
    }
    if (released) {
      continue;  // the release runs first and cancels the turn-around
    }
    const int32_t left = static_cast<int32_t>(channelDimDueMs(d) - now);
    if (left > 0) {
      if (static_cast<uint32_t>(left) < wait) {
        wait = static_cast<uint32_t>(left);
      }
      continue;
    }
    if (!found) {
      *ch = i;
      *creds = gHueCredsShared;
      gHueJobRunning = true;  // under the lock, as in hueJobTake
      found = true;
    }
  }
  portEXIT_CRITICAL(&gHueJobMux);
  *waitMs = wait;
  return found;
}

inline size_t gHueJobNext = 0;  // the task, or the loop when there is no task

// No task: runs from hueJobPost, and from the loop each pass for the turn-arounds.
inline void hueWorkerRunPending() {
  size_t ch = 0;
  HueJobEntry job;
  HueCreds creds;
  uint32_t wait = 0;
  for (;;) {
    if (hueDimTakeDue(&ch, &creds, &wait)) {
      channelDimTurn(ch, creds);
      continue;
    }
    if (!hueJobTake(&gHueJobNext, &ch, &job, &creds)) {
      break;
    }
    hueJobRun(ch, job, creds);
  }
  gHueJobRunning = false;
}

// Loop task, each pass: without a Hue task, due turn-arounds run here.
inline void hueWorkerLoopPoll() {
  if (!gHueTask) {
    hueWorkerRunPending();
  }
}

// Any task: nothing queued and nothing running.
inline bool hueWorkerIdle() {
  bool queued = false;
  portENTER_CRITICAL(&gHueJobMux);
  for (size_t i = 0; i < kChannelCount && !queued; i++) {
    queued = gHueJobs[i].n > 0;
  }
  portEXIT_CRITICAL(&gHueJobMux);
  return !queued && !gHueJobRunning;
}

inline void hueWorkerTask(void *) {
  for (;;) {
    size_t ch = 0;
    HueJobEntry job;
    HueCreds creds;
    uint32_t wait = UINT32_MAX;
    gHueJobRunning = false;
    // A due turn-around first: it is one PUT, and that channel's next one is a leg away.
    if (hueDimTakeDue(&ch, &creds, &wait)) {
      channelDimTurn(ch, creds);
      continue;
    }
    if (!hueJobTake(&gHueJobNext, &ch, &job, &creds)) {
      // Sleep until the next job (every post notifies) or the next turn-around.
      ulTaskNotifyTake(pdTRUE, wait == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(wait) + 1);
      continue;
    }
    hueJobRun(ch, job, creds);
    LOG("hue task: stack free %u, heap min %u\n", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
        static_cast<unsigned>(ESP.getMinFreeHeap()));
  }
}

// Setup, after channelsBegin and the first hueWorkerPublishCreds. Same priority as the loop:
// the TLS handshake shares the CPU instead of stopping pin reads, and network waits block
// only this task.
inline void hueWorkerBegin() {
  if (gHueTask) {
    return;
  }
  gHueJobEpoch = gNvsEpoch;
  if (xTaskCreate(hueWorkerTask, "hue", kHueWorkerStack, nullptr, 1, &gHueTask) != pdPASS) {
    gHueTask = nullptr;
    LOGLN("hue task failed: Hue calls run in the loop");
  }
}
