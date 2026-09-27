# hue-simple-switch

Wi‑Fi wall switch for the **Seeed XIAO ESP32-C6**. Calls the Hue Bridge's local Clip v2 API (HTTPS). Not Zigbee.

GPIO channels (closed = pin to GND, `INPUT_PULLUP`):

| id | GPIO | default kind | label |
| --- | --- | --- | --- |
| `boot` | 9 | `momentary` | BOOT |
| `d0` | 0 | `maintained` | D0 |
| `d1` | 1 | `maintained` | D1 |
| `d2` | 2 | `maintained` | D2 |

Since 0.3.0 the console picks each channel's kind (`maintained` = toggle switch, `momentary` = push button) and sends it in the config's `channels[]`; a pin it does not list does nothing. BOOT is always a push button. The default kinds above apply only to the old config payload (no `channels[]`), from a console that predates channel types.

Each channel has recipes per event, derived by the console:

- Toggle switch: `on` (lever closes), `off` (lever opens, after the ~400 ms double-click window), `double_click` (opens and closes inside the window; runs `on` when there is no `double_click` recipe).
- Push button: `short` on release, at once when the channel has no `double_click` recipe; with one, a second press inside ~400 ms is `double_click` and an expired window is `short`. `hold` fires once at ~800 ms while pressed, only when the channel has a `hold` recipe.
- BOOT without a `hold` recipe: a 3 s press re-pairs with the Bridge. With one, the button never re-pairs (USB install only).
- A scene list (`recall_scene` with `targets[]`) cycles from the last scene the channel set (kept in NVS), wraps, skips scenes that answer 404, and starts over at the first scene after an `off` on the channel.

The GPIO runs NVS → Bridge; it doesn't wait for Vercel.

Wiring for three wall switches: [`docs/wiring-3-switches.svg`](docs/wiring-3-switches.svg).

**BOOT** (GPIO9) and **RST** (CHIP_PU) are **buttons**, not LEDs. The board has two lights:

The orange LED's alphabet is **implemented** in firmware 0.2.7: `docs/specs/finished/led-status.md`.

| Light | Where | Driven by |
| --- | --- | --- |
| Orange (user, GPIO15 `LED_BUILTIN`) | Right side, next to RST | This firmware |
| Red (charging) | Next to the USB-C | The charger hardware; the sketch doesn't touch it |

### Orange LED (firmware)

One pattern at a time, top to bottom. When the step changes, the burst starts from zero. `LOW` on GPIO15 turns the orange on (active low).

| What you see | Meaning |
| --- | --- |
| Solid on | System error: the console task wasn't created, or the console answered 401. The 401 stays solid until a response with a code other than 401, or a new `HUESET token`. Wins even without Wi‑Fi. |
| Continuous blink ~2 Hz (250 ms on / 250 ms off) | No Wi‑Fi STA, or Improv scan. Even with a saved SSID. |
| 2 flashes and a pause | Wi‑Fi ok, no console (URL or token empty, or a `your-…` placeholder). |
| 3 flashes and a pause | Wi‑Fi and console, Hue not paired, or pairing in progress / timed out. Holding BOOT 3 s (re-pair) uses this pattern. |
| 4 flashes and a pause | Wi‑Fi, console and Hue, no recipes. |
| One short flash every ~3 s | Armed: one or more recipes. |

Burst (#2–#4): 100 ms on, 200 ms between flashes, 1400 ms pause. The armed flash lasts 80 ms and repeats every 3 s.

### Red LED (charging, not the sketch)

| What you see | Meaning |
| --- | --- |
| On for ~30 s when USB is plugged in without a battery | USB present; then it turns off. |
| Blinking | LiPo battery connected and charging over USB. |
| Off with USB and battery | Charge complete (or no charge cycle). |

Contract: `hue-switch-console/docs/definitions.md` and `docs/device-api.md`.

## Setup

1. Flash and provision from Chrome on [hue.tineira.com](https://hue.tineira.com) → Devices (USB). Improv saves the **2.4 GHz** network and `HUESET` stores the token (`hsw_…`) and url in NVS `console`. None of that is compiled in. For development, copy `config.example.h` to `config.h` (only `SERIAL_DEBUG`); `arduino-cli upload` doesn't erase NVS, so the network and token survive every flash.
2. The XIAO discovers the Bridge (mDNS `_hue._tcp`, NVS, `discovery.meethue.com`) and pairs the Hue key (three orange flashes → Bridge button). IP and key stay in NVS, not in `config.h`.
3. Register: `POST /api/device/register` with `product: "simple"`, MAC, `channels[]` and snapshot (lights/rooms/scenes). Poll: `GET /api/device/config?mac=&rev=` at boot, then every `X-Poll-Sec` seconds from the console (clamped 30 s to 1 h; `204` = unchanged, keep NVS), once more right after a new `rev` is saved, and 1 h after a `401`. Without the header (older console): ~1 min without recipes, 1 h with some. With recipes, register (topology) at most hourly.
4. Arduino IDE 2.3.10: open `hue-simple-switch.ino`, board **XIAO_ESP32C6**.
5. Or arduino-cli: `arduino-cli compile --profile xiao-c6 .`

List lamps (diagnostics):

```
curl -k -H "hue-application-key: KEY" https://BRIDGE_IP/clip/v2/resource/light
```

`config.h` is not committed. Product onboarding = the USB installer on hue.tineira.com.

## Release

A push to `main` **is a release**. `.github/workflows/firmware.yml` builds the product image and uploads it to the console (hue.tineira.com) with this version's `CHANGELOG.md` entry as release notes. The console's USB installer then offers that build to every Simple plugged in over USB. The workflow also keeps the `usb-installer` GitHub Release as a download link.

- `FIRMWARE_VERSION` in `console.h` is the version the console shows. Bump it for any change a board should pick up, and add its entry to `CHANGELOG.md` in the same commit. A push without a bump re-sends the notes only (`409 version_exists` warning).
- `THIRD_PARTY.json` lists the open-source components linked into the image (the core, ESP-IDF, and every library pinned in `sketch.yaml`), with version, SPDX license and a link to the license at that version. CI sends it as the release `credits` for the console's Credits page, and fails if any `platform:` or `libraries:` entry in `sketch.yaml` is missing from it or has a different version. Update it in the same commit as any core or library bump.
- Image layout and offsets: [`docs/firmware-artifacts.md`](docs/firmware-artifacts.md).
- The pipeline needs the `FIRMWARE_UPLOAD_TOKEN` secret in this repo. Setup and troubleshooting: the console repo's `README.md`, "Firmware release pipeline".

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). Bugs and ideas go in GitHub issues.

## License

Copyright (c) 2026 Tomas Neira and contributors. [MIT](LICENSE).

The image also links open-source components under their own licenses, listed in
[`THIRD_PARTY.json`](THIRD_PARTY.json). The Arduino-ESP32 core is LGPL-2.1-or-later:
because this firmware's source is public, you can rebuild it against a modified core.

The console this firmware talks to, [`hue-switch-console`](https://github.com/tineira/hue-switch-console),
is AGPL-3.0; it also holds the device contract (`docs/device-api.md`).

Not affiliated with or endorsed by Signify. Philips Hue is a trademark of Signify.
