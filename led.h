#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_timer.h>
#include "usb.h"

// Orange LED (GPIO15), active low: cathode on the pin, anode to 3.3 V.
// LOW = on, HIGH = off. Spec: docs/specs/finished/led-status.md.
// The 50 ms timer calls ledTick while hueHttp blocks the loop.

static const unsigned long PULSE_ON = 100;
static const unsigned long PULSE_GAP = 200;
static const unsigned long BURST_PAUSE = 1400;
static const unsigned long FAST_ON = 250;
static const unsigned long FAST_OFF = 250;
static const unsigned long HEART_ON = 80;
static const unsigned long HEART_OFF = 2920;

static uint8_t gLedRung = 0;
static unsigned long gLedEpoch = 0;
static uint32_t gLedBusy = 0;
static esp_timer_handle_t gLedTimer = nullptr;
static volatile bool gLedStaUp = false;

static bool ledClaim() {
  uint32_t expected = 0;
  return __atomic_compare_exchange_n(&gLedBusy, &expected, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

static void ledRelease() {
  __atomic_store_n(&gLedBusy, 0u, __ATOMIC_RELEASE);
}

// Top to bottom. The first that matches wins.
static uint8_t ledComputeRung() {
  if (gConsoleTaskFailed || gConsoleAuthRejected) {
    return 6;
  }
  if (gImprovScanPending || !gLedStaUp) {
    return 1;
  }
  if (!gConsoleConfiguredOk) {
    return 2;
  }
  if (gHuePairing || gHuePairTimeout || gHueAuthRejected || gHueBridgeMissing || !gHueKeyUsable ||
      !gHueIpUsable) {
    return 3;
  }
  if (gRecipeCount == 0) {
    return 4;
  }
  return 5;
}

static bool ledBurstOn(uint8_t pulses, unsigned long pos) {
  unsigned long t = 0;
  for (uint8_t i = 0; i < pulses; i++) {
    if (pos < t + PULSE_ON) {
      return true;
    }
    t += PULSE_ON;
    if (i + 1 < pulses) {
      if (pos < t + PULSE_GAP) {
        return false;
      }
      t += PULSE_GAP;
    }
  }
  return false;
}

static bool ledLevel(uint8_t rung, unsigned long now) {
  if (rung == 6) {
    return true;
  }
  const unsigned long elapsed = now - gLedEpoch;
  if (rung == 1) {
    return (elapsed % (FAST_ON + FAST_OFF)) < FAST_ON;
  }
  if (rung == 5) {
    return (elapsed % (HEART_ON + HEART_OFF)) < HEART_ON;
  }
  if (rung < 2 || rung > 4) {
    return false;
  }
  const unsigned long cycle =
      (unsigned long)rung * PULSE_ON + (unsigned long)(rung - 1) * PULSE_GAP + BURST_PAUSE;
  return ledBurstOn(rung, elapsed % cycle);
}

// No delay(). Can be called from the loop and from the timer.
static void ledTick(unsigned long now) {
  if (!ledClaim()) {
    return;
  }
  const uint8_t rung = ledComputeRung();
  if (rung != gLedRung) {
    gLedRung = rung;
    gLedEpoch = now;
  }
  digitalWrite(LED_BUILTIN, ledLevel(rung, now) ? LOW : HIGH);
  ledRelease();
}

static void ledTimerCb(void *) {
  ledTick(millis());
}

bool ledPoll(unsigned long now) {
  gLedStaUp = (WiFi.status() == WL_CONNECTED);
  ledTick(now);
  return gLedStaUp;
}

void ledBegin() {
  if (gLedTimer) {
    return;
  }
  esp_timer_create_args_t args = {};
  args.callback = ledTimerCb;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "led";
  args.skip_unhandled_events = true;
  esp_timer_handle_t timer = nullptr;
  if (esp_timer_create(&args, &timer) != ESP_OK) {
    LOGLN("led timer failed");
    return;
  }
  if (esp_timer_start_periodic(timer, 50 * 1000) != ESP_OK) {
    LOGLN("led timer start failed");
    return;
  }
  gLedTimer = timer;
  ledTick(millis());
}
