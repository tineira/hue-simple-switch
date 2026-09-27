# hue-simple-switch

Wi-Fi wall switch firmware for Seeed XIAO ESP32-C6. Calls the Philips Hue local API. Not Zigbee.

## Contract

This firmware is one of several switches for one console. The console repo [`hue-switch-console`](https://github.com/tineira/hue-switch-console) owns the contract. Local checkout paths on the maintainer's machine are in `AGENTS.local.md` (gitignored) when it exists; read it to find the sibling repos.

- `docs/device-api.md`: endpoints, auth, payloads, error codes. Authoritative.
- `docs/definitions.md`: product model (recipes, channels, pages).
- `docs/changelog.md`: the console's own release notes. This firmware's notes live in this repo's `CHANGELOG.md`.
- `docs/specs/`: cross-repo specs, each with a checklist per repo.

Rules:

- Do not change what this firmware sends to or expects from the console (endpoints, JSON fields, error handling, NVS keys the console writes over USB) unless `docs/device-api.md` or an approved spec in `docs/specs/` says so. If the work needs a protocol change, stop and propose it for the console repo; do not invent it here.
- When working from a cross-repo spec, do only this repo's checklist section and tick it. The console ships first and stays backward compatible, so boards already on the wall keep working.
- Read the console docs from its checkout (or GitHub); do not copy them into this tree.
- The other switch firmwares (`hue-round-switch`, `hue-simple-switch`, and any later ones) implement the same contract. Do not edit them from this repo. If behavior both should share differs, say so.

## Hardware

- Board: Seeed Studio XIAO ESP32-C6
- Arduino IDE board name: `XIAO_ESP32C6`
- FQBN: `esp32:esp32:XIAO_ESP32C6`
- Core: Arduino-ESP32 **3.3.12** (pinned by the `sketch.yaml` profile)
- Flash: 4 MB, `partitions.csv` = **min_spiffs** (~1.9 MB APP × 2 OTA, 128 KB SPIFFS). Not Zigbee. Not default 1.2 MB.
- USB CDC on boot: Enabled (default for this board)

This sketch lives in the Arduino IDE sketchbook (`arduino-cli config get directories.user`). The sketchbook folder itself is **not** a git repo.

## Secrets

- `config.h` (gitignored) holds only `SERIAL_DEBUG`. No Wi-Fi, console URL, or token is compiled in, in dev or product. Wi-Fi is Arduino STA (Improv), token/url are NVS `console` (`HUESET`), both written by the console over USB. Uploads do not erase NVS, so a board provisioned once keeps them across dev flashes.
- Bridge IP, Hue application key, and recipes are not in `config.h`. Discover / pair / NVS / console poll.
- `config.example.h` is the template that is committed.
- Never put SSID, passwords, or Hue keys in the `.ino` or in git.

If `config.h` is missing: `copy config.example.h config.h`.

## Build (arduino-cli)

Use the same Arduino15 data dir as the IDE so the 3.3.12 core is reused.

```
arduino-cli compile --profile xiao-c6 --build-property build.partitions=min_spiffs --build-property upload.maximum_size=1966080 .
arduino-cli upload  --profile xiao-c6 -p COMx .
arduino-cli monitor -p COMx -c baudrate=115200
```

Replace `COMx` with the XIAO port (`arduino-cli board list`).

Release: a push to `main` runs `.github/workflows/firmware.yml`. It builds with `SERIAL_DEBUG` 0 and uploads the four installer parts plus this version's `CHANGELOG.md` entry to the console (`POST https://hue.tineira.com/api/firmware/simple`, secret `FIRMWARE_UPLOAD_TOKEN`); the console serves `/install` and `/changelog` from that upload. A failed upload fails the run. The same version with different bins is kept as released (`409 version_exists`, a warning, normal on pushes that do not bump the version): bump `FIRMWARE_VERSION` to ship new bins. It also sends `THIRD_PARTY.json` as the release credits; that file must list the core and every library pinned in `sketch.yaml` at the same version (licenses read from each component's own files), or the run fails. The console still owns the contract docs; see its `docs/specs/finished/firmware-uploads.md`.

Changelog: when `FIRMWARE_VERSION` changes, add a `### X.Y.Z — YYYY-MM-DD` heading at the top of this repo's `CHANGELOG.md` in the same commit (not in the console's `docs/changelog.md`). CI sends that entry as the release notes. Write each bullet as what changed for the person using the switch (what they see or can now do), not how the code changed: no function names, macros, USB command names, NVS keys, or GPIO numbers. Example: "The switch remembers the Wi-Fi network you saved during setup after it restarts." Not: "Reconnect stored Wi-Fi through the Arduino STA API."

## Arduino IDE 2.3.10

- File → Open this folder (`hue-simple-switch.ino`)
- Board: `XIAO_ESP32C6` (esp32)
- Partition: sketch `partitions.csv` (min_spiffs). Do not use Zigbee.
- Libraries: ESP32 core only (`WiFi`, `HTTPClient`, `ESPmDNS`, `Preferences`)

## Product

- `product`: always `"simple"` on `POST /api/device/register` (plus GPIO `channels[]`, `source: xiao`)
- Channels v1: `boot` GPIO9 momentary (short = recipe, hold 3 s = Hue re-pair); `d0`/`d1`/`d2` GPIO 0/1/2 maintained (`on` / `off` / `double_click`)
- USB commands (ASCII lines, besides Improv): `HUESET`, `HUEGET`, `HUEPAIR`, `HUECLR`, `HUEBOOT` (answers `HUEOK boot`, then restarts into the ROM bootloader so Devices can flash without the BOOT button; RESET is still pressed once after the write). Anything else answers `HUEERR unknown`. See the console `docs/specs/finished/devices.md` §6
- Console: URL + token from NVS `console` (`HUESET` over USB). Register + `GET /api/device/config`. Poll at boot, then as often as the console says (`X-Poll-Sec`, clamped 30 s to 1 h); without it, ~1 min if no recipes and 1 h if any (console `docs/specs/finished/config-sync.md`). GPIO loop never waits on that HTTP (console FreeRTOS task)
- Hue gestures: the GPIO loop only posts (channel, event); the Hue worker task (`hue_worker.h`) looks up the recipe and calls the Bridge. One short queue per channel, served round-robin; queue rules are in that file's header
- Last **good** Hue snapshot wins: do not POST empty `[]` if a Clip stream is not 200
- On `bridgeid` change: clear NVS recipes/`rev` **before** the next poll

## Code conventions

- Arduino `.ino` + small `.h` files; no PlatformIO for this project
- English everywhere: identifiers, comments, docs, specs, README, commit messages. Translate Spanish you touch
- Digital input channels: each channel has its own recipe; see hue-switch-console `docs/definitions.md` and `docs/device-api.md`
- Hue API: local HTTPS Clip v2, not the cloud
- Do not impersonate Hue accessories or use Zigbee on this sketch
- Scratch notes (`docs/_audit-*.md`, `docs/_review-*.md`) are not spec. Delete them once used. Do not commit them.
