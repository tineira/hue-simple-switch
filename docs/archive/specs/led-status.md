# Simple switch — orange LED (status)

**Requirements** document. Covers only `hue-simple-switch` (XIAO ESP32-C6, GPIO15 `LED_BUILTIN`, orange). Round uses the disc, not this alphabet.

The red charging LED and the BOOT/RST buttons are not part of this spec.

**Status:** implemented (firmware 0.2.7). Spec archived. Not an implementation gap.

**Closed on 2026-09-21 (grilling):** tick during the POST, rejected key vs PUT, #6 only 401/task, burst restarts from zero. Do not reopen.

---

## 1. Who it's for

A **bench setup** checklist (the XIAO is visible). Inside the wall box the LED is hardly ever looked at; it's not everyday UI.

It doesn't teach *how* to configure. Only *which step you're on* or *there's a system fault*.

---

## 2. Steps (priority)

Evaluated **top to bottom**. The first that matches wins. One pattern at a time.

| # | Condition | Pattern |
| --- | --- | --- |
| 6 | **System** error (below) | Solid on |
| 1 | No Wi‑Fi STA (`WL_CONNECTED` false) | Continuous fast ~2 Hz |
| 2 | Wi‑Fi ok, no console URL or token in NVS `console` | **2** flashes, pause |
| 3 | Wi‑Fi + console, no paired Bridge (no usable Hue key in NVS, or pairing in progress / timed out) | **3** flashes, pause |
| 4 | Wi‑Fi + console + Hue, **zero** recipes in NVS | **4** flashes, pause |
| 5 | Armed: Wi‑Fi + console + Hue + ≥1 recipe | One short flash every ~3 s |

A 3 s BOOT hold (re-pair) shows **#3** while pairing lasts, not an extra pattern.

When the **step changes**, the pattern starts from zero (first flash, or the first half-cycle of #1/#5). The rest of a previous burst is not shown.

Improv scan or association (`WL_CONNECTED` false, even if NVS has an SSID) is **#1**. A token or URL that `consoleConfigured()` rejects (empty or malformed) is "no console" → **#2** if there is Wi‑Fi.

---

## 3. Timing (countable, not different frequencies)

Constants (ms), one table in code:

| Symbol | ms | Use |
| --- | --- | --- |
| `PULSE_ON` | 100 | Burst flash (#2–4) |
| `PULSE_GAP` | 200 | Off between flashes of the same burst |
| `BURST_PAUSE` | 1400 | Off after the last flash, before repeating |
| `FAST_ON` / `FAST_OFF` | 250 / 250 | #1 (~2 Hz). Not counted. |
| `HEART_ON` | 80 | #5 |
| `HEART_OFF` | 2920 | #5 → 3 s period |

Burst of *n* flashes: `n × PULSE_ON + (n − 1) × PULSE_GAP`, then `BURST_PAUSE`.

Example #3: on 100 — off 200 — on 100 — off 200 — on 100 — off 1400 — repeat. You count **three**.

#6: pin `LOW` (on), no blink tick.

Don't use 5 Hz vs 10 Hz vs 1 Hz. A human can't tell them apart on a 3 mm LED.

---

## 4. What a system error is (#6)

Closed list. Nothing else turns on the solid light:

- The console task was not created.
- The console answered **401** (token rejected). Recipes in NVS are kept.

The 401 **sticks** until a real console response with a code **> 0 and other than 401**, or until a new `HUESET token`. An attempt that doesn't arrive (timeout, `code == -1`, Wi‑Fi down) does **not** clear it. While it sticks, the solid light wins even without Wi‑Fi (#1 is not shown).

**Not** #6:

- Pairing timeout or Bridge not found → **#3**.
- Wi‑Fi down, no sticky 401 → **#1**.
- Failed Hue PUT on a GPIO touch (on/off/double/short), or unreachable Bridge → **the LED doesn't change**.
- Console poll, `rev`, snapshot, debounce, any `LOG`.

---

## 5. Hue "paired"

For #3 vs #4/#5: there is a key in NVS that `hueLooksLikeKey` accepts **and** a Bridge IP. Do **not** GET Clip on every LED tick.

It is lost, dropping to **#3**, only if a Hue HTTP call **that sent the key** says it's no good: **401 or 403**, and the body isn't "link button not pressed". A 401/403 from a request without the key (`/api/config`, discovery) doesn't count. A PUT that did carry the key and returns 401/403 does drop to #3. A timeout, a 5xx or the Bridge being off do **not** drop the step. Never #6.

For **20 s** after the pairing POST has just delivered the key, a 401 or 403 does **not** drop to #3. The key just came out of the Bridge. A later call, outside those 20 s, does.

---

## 6. Empty recipes

#4 = `gRecipeCount == 0` (no channel). One recipe on a single channel is already armed (#5). Don't distinguish "the other three are missing".

---

## 7. Boot

After `pinMode(LED_BUILTIN, OUTPUT)` the same classifier runs. The first few hundred ms may look like #1 (no STA yet). There is no separate `UI_BOOT` screen.

USB Improv / `HUESET` add no pattern: no STA → #1; STA without token/url (not yet in NVS) → #2.

---

## 8. Done in 0.2.7

- LED tick on a timer, **no `delay()`**, every **~50 ms**, also during the pairing POST. The GPIO doesn't wait for the LED (`channelsPoll` first).
- Pattern #3 is set by the classifier, not `hueBlink`.
- Polarity: `LOW` = on (GPIO15, active low). `HIGH` turns it off.
- The red charging LED is untouched.
- `FIRMWARE_VERSION` 0.2.7. The README table matches.

Outside this slice: Round (disc), Improv copy, console.

---

## 9. Bench test (acceptance)

With the XIAO in view, without opening Serial:

1. No SSID / Wi‑Fi down → continuous ~2 Hz.
2. Wi‑Fi, delete or empty URL/token → count **2**.
3. Wi‑Fi + console, BOOT hold 3 s or no key → count **3**; pressing the Bridge button moves to 4 or 5.
4. Paired, console without recipes → count **4**.
5. One recipe on any channel → one flash every ~3 s.
6. Invalid console token (401) → solid. Restore the token → back to 4 or 5.
7. During 4 or 5, a GPIO whose PUT fails → the pattern does **not** go solid.
