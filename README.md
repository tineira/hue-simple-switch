# hue-simple-switch

Wi‑Fi wall switch for the **Seeed XIAO ESP32-C6**. Calls the Hue Bridge's local Clip v2 API (HTTPS). Not Zigbee.

GPIO channels (closed = pin to GND, `INPUT_PULLUP`):

| id | GPIO | default kind | label |
| --- | --- | --- | --- |
| `boot` | 9 | `momentary` | BOOT |
| `d0` | 0 | `maintained` | D0 |
| `d1` | 1 | `maintained` | D1 |
| `d2` | 2 | `maintained` | D2 |
| `d3` | 21 | none | D3 |
| `d4` | 22 | none | D4 |
| `d5` | 23 | none | D5 |

D3–D5 (GPIO 21–23) are channels since 0.5.0, so one board takes up to six wall switches or buttons on D0–D5, plus BOOT. None of D3–D5 is a strapping pin, and this firmware has no I2C on D4/D5. D6–D10 stay unused (D6 is the chip's serial TX at boot).

Since 0.3.0 the console picks each channel's kind (`maintained` = toggle switch, `momentary` = push button) and sends it in the config's `channels[]`; a pin it does not list does nothing. BOOT is always a push button. The default kinds above apply only to the old config payload (no `channels[]`), from a console that predates channel types; there D3–D5 do nothing, so an unwired pin never acts.

Each channel has recipes per event, derived by the console:

- Toggle switch, flip set (the lever sets on or off; no `flip` in `channels[]`): `on` (lever closes), `off` (lever opens, after the ~400 ms double-click window), `double_click` (opens and closes inside the window; runs `on` when there is no `double_click` recipe).
- Toggle switch, flip toggle (`"flip": "toggle"`, since 0.8.0; each flip toggles, the console sends `toggle` for both `on` and `off`): each flip posts `on` (lever closes) or `off` (lever opens) at once when there is no `double_click` recipe. With one, a flip either way waits ~400 ms: a second flip inside it is `double_click`, otherwise the first flip runs. No `on` fallback. A toggle that turned the lights off restarts the scene list. Boot, a config change or a change of the flip setting never toggles.
- Push button: `short` on release, at once when the channel has no `double_click` recipe; with one, a second press inside ~400 ms is `double_click` and an expired window is `short`. `hold` fires once at ~800 ms while pressed, only when the channel has a `hold` recipe.
- BOOT without a `hold` recipe: a 3 s press re-pairs with the Bridge. With one, the button never re-pairs (USB install only).
- A scene list (`recall_scene` with `targets[]`) cycles from the last scene the channel set (kept in NVS), wraps, skips scenes that answer 404, and starts over at the first scene after an `off` on the channel (in flip toggle mode, after a toggle that turned the target off).

The GPIO runs NVS → Bridge; it doesn't wait for Vercel. Since 0.5.0 the config is stored as one NVS blob per channel, so saving the largest config (seven channels with scene lists) needs only one channel's worth of free space; the first boot after the update converts the older single blob.

Wiring for up to six wall switches or buttons on D0–D5: [`docs/wiring-switches.svg`](docs/wiring-switches.svg). Wire only the inputs you use. The step-by-step build guide (what to buy, wiring one input at a time, what never to connect, and when you need the resistors) is at [hue.tineira.com/how-to](https://hue.tineira.com/how-to?product=simple#wire).

To power the switch from the mains inside a wall box, behind the existing switch, there is a carrier board (KiCad, Gerbers for JLCPCB, BOM) and a printable enclosure in [`hardware/`](hardware/README.md). It keeps the same pins and firmware. It is an uncertified, experimental mains design: read its safety notes and have an electrician install it.

> [!WARNING]
> **Safety.** Mains voltage can kill. The designs in this repository are uncertified and some are unproven, and the maintainer is not an electrical engineer. Rules and wire colors differ by country. Read the [Safety notice](https://hue.tineira.com/safety) before you build anything, and never connect any pin of a USB-powered build to anything that is or was on mains. Everything here is provided as is, without warranty, as the licenses say.

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
3. Register: `POST /api/device/register` with `product: "simple"`, MAC, `channels[]` and snapshot (lights/rooms/scenes). Poll: `GET /api/device/config?mac=&rev=&firmware=` at boot, then every `X-Poll-Sec` seconds from the console (clamped 30 s to 1 h; `204` = unchanged, keep NVS), once more right after a new `rev` is saved, and 1 h after a `401`. Without the header (older console): ~1 min without recipes, 1 h with some. With recipes, register (topology) at most hourly.
4. Arduino IDE 2.3.10: open `hue-simple-switch.ino`, board **XIAO_ESP32C6**.
5. Or arduino-cli: `arduino-cli compile --profile xiao-c6 .`

List lamps (diagnostics):

```
curl -k -H "hue-application-key: KEY" https://BRIDGE_IP/clip/v2/resource/light
```

`config.h` is not committed. Product onboarding = the USB installer on hue.tineira.com.

## Release

A push to `main` **is a release**. `.github/workflows/firmware.yml` builds the product image and uploads it to the console (hue.tineira.com) with this version's `CHANGELOG.md` entry as release notes. The console's USB installer then offers that build to every Simple plugged in over USB, and since 0.6.0 the owner can send it over Wi-Fi from the Switches page (below). The workflow also keeps the `usb-installer` GitHub Release as a download link.

- `FIRMWARE_VERSION` in `console.h` is the version the console shows. Bump it for any change a board should pick up, and add its entry to `CHANGELOG.md` in the same commit. A push without a bump re-sends the notes only (`409 version_exists` warning).
- `THIRD_PARTY.json` lists the open-source components linked into the image (the core, ESP-IDF, and every library pinned in `sketch.yaml`), with version, SPDX license and a link to the license at that version. CI sends it as the release `credits` for the console's Credits page, and fails if any `platform:` or `libraries:` entry in `sketch.yaml` is missing from it or has a different version (`scripts/check-credits.py`, run on every pull request too). Update it in the same commit as any core or library bump.
- Update over Wi-Fi (0.6.0+, console `docs/specs/ota.md`): when the owner presses Update, the next poll's `200` carries `ota` (version, path, sha256, size). After the poll's connection is closed, the console task downloads `firmware.bin` from the console URL in NVS, writes it to the inactive app slot while hashing it, checks length and sha256, and restarts into it. It waits for the next poll while an input is pressed or a Hue call is queued or running. The new image confirms itself after its first `200`/`204` poll; a restart before that makes the bootloader go back to the old one, which reports `ota_error=boot`. A failure is reported once as `ota_error` and retried at most hourly (a download cut off by a power cut or restart counts, reported as `size` at the next boot, since 0.6.1) (`size`/`sha` from a bad image: not until reboot). NVS is never erased. Boards before 0.6.0 need one USB update first.
- Image layout and offsets: [`docs/firmware-artifacts.md`](docs/firmware-artifacts.md).
- The pipeline needs the `FIRMWARE_UPLOAD_TOKEN` secret in this repo. Setup and troubleshooting: the console repo's `README.md`, "Firmware release pipeline".

### Build and upload from a fork

A fork builds the same image on every push to its `main`, and can upload it to your own console instead of hue.tineira.com (self-hosting: the console repo's `docs/self-hosting.md`).

- **Build only (default).** With no `FIRMWARE_UPLOAD_TOKEN` secret, the workflow builds, skips the upload with a notice and stays green. The four installer parts are in the run's `usb-installer-simple` artifact and in the fork's `usb-installer` GitHub Release.
- **Upload to your console.** In the fork's **Settings → Secrets and variables → Actions**:
  1. Add the repository **variable** `CONSOLE_UPLOAD_URL` with your console's origin, for example `https://hue.example.com` (no path, no trailing slash). Without it, the upload goes to `https://hue.tineira.com`, which rejects a fork's token.
  2. Add the repository **secret** `FIRMWARE_UPLOAD_TOKEN` with the value of `FIRMWARE_UPLOAD_TOKEN` on your console.
  3. Push to `main` (or run the workflow by hand). The release waits in your console's `/admin` until you make it current; Setup then flashes it.
- Bump `FIRMWARE_VERSION` in `console.h` and add its `CHANGELOG.md` entry for each build you want boards to get, as above: your console keeps the first bins it received for a version.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). Bugs and ideas go in GitHub issues.

## Sponsor

The console and the switch firmwares are free and stay that way. If they are useful
to you, you can [sponsor the project on GitHub](https://github.com/sponsors/tineira).
Sponsorship helps pay for development and does not unlock anything.

## License

Copyright (c) 2026 Tomas Neira and contributors. [MIT](LICENSE), except the
carrier board and enclosure designs in `hardware/`, which are
[CERN-OHL-P-2.0](hardware/LICENSE) (see [`hardware/README.md`](hardware/README.md)).

The image also links open-source components under their own licenses, listed in
[`THIRD_PARTY.json`](THIRD_PARTY.json). The Arduino-ESP32 core is LGPL-2.1-or-later:
because this firmware's source is public, you can rebuild it against a modified core.

The console this firmware talks to, [`hue-switch-console`](https://github.com/tineira/hue-switch-console),
is AGPL-3.0; it also holds the device contract (`docs/device-api.md`).

Not affiliated with or endorsed by Signify. Philips Hue is a trademark of Signify.
