# hue-simple-switch

Wi-Fi wall switch firmware for Seeed XIAO ESP32-C6. Calls the Philips Hue local API. Not Zigbee.

## Hardware

- Board: Seeed Studio XIAO ESP32-C6
- Arduino IDE board name: `XIAO_ESP32C6`
- FQBN: `esp32:esp32:XIAO_ESP32C6`
- Core: Arduino-ESP32 **3.3.12** (already installed in `%LOCALAPPDATA%\Arduino15`)
- Flash: 4 MB, `partitions.csv` = **min_spiffs** (~1.9 MB APP × 2 OTA, 128 KB SPIFFS). Not Zigbee. Not default 1.2 MB.
- USB CDC on boot: Enabled (default for this board)

This sketch lives in the Arduino IDE sketchbook (`directories.user` = `C:\Users\tinei\Arduino`). The parent folder is **not** a git repo. GigaDash stays in `OneDrive\Documents\Arduino`.

## Secrets

- `config.h` (gitignored) holds `WIFI_SSID`, `WIFI_PASSWORD`, `CONSOLE_URL`, and `CONSOLE_TOKEN`.
- Bridge IP, Hue application key, and recipes are not in `config.h`. Discover / pair / NVS / console poll.
- `config.example.h` is the template that is committed.
- Never put SSID, passwords, or Hue keys in the `.ino` or in git.

If `config.h` is missing: `copy config.example.h config.h` and edit it.

## Build (arduino-cli)

Use the same Arduino15 data dir as the IDE so the 3.3.12 core is reused.

```
arduino-cli compile --profile xiao-c6 --build-property build.partitions=min_spiffs --build-property upload.maximum_size=1966080 .
arduino-cli upload  --profile xiao-c6 -p COMx .
arduino-cli monitor -p COMx -c baudrate=115200
```

Replace `COMx` with the XIAO port (`arduino-cli board list`).

USB installer images: compile with empty `WIFI_*` / `CONSOLE_*`, copy the four parts into the **console** tree `public/firmware/simple/` and set `manifest.json` `version` to `FIRMWARE_VERSION`. Console agents must not revert that folder; tell them in the same recorte. The wizard shows that version, not this sketch until those files are in the console repo (and deployed).

Changelog: when `FIRMWARE_VERSION` changes, add a `### X.Y.Z — YYYY-MM-DD` heading under `## Simple` in the **console** tree `docs/changelog.md`, with the `<!-- commit -->` marker above it, in the same recorte. Write each bullet as what changed for the person using the switch (what they see or can now do), not how the code changed: no function names, macros, USB command names, NVS keys, or GPIO numbers. Example: "The switch remembers the Wi-Fi network you saved during setup after it restarts." Not: "Reconnect stored Wi-Fi through the Arduino STA API."

## Arduino IDE 2.3.10

- File → Open this folder (`hue-simple-switch.ino`)
- Board: `XIAO_ESP32C6` (esp32)
- Partition: sketch `partitions.csv` (min_spiffs). Do not use Zigbee.
- Libraries: ESP32 core only (`WiFi`, `HTTPClient`, `ESPmDNS`, `Preferences`)

## Product

- `product`: always `"simple"` on `POST /api/device/register` (plus GPIO `channels[]`, `source: xiao`)
- Channels v1: `boot` GPIO9 momentary (short = recipe, hold 3 s = Hue re-pair); `d0`/`d1`/`d2` GPIO 0/1/2 maintained (`on` / `off` / `double_click`)
- Console: `CONSOLE_URL` + `CONSOLE_TOKEN` in `config.h`. Register + `GET /api/device/config`. Poll ~1 min if no recipes; at boot and every 1 h if any. GPIO loop never waits on that HTTP (console FreeRTOS task)
- Last **good** Hue snapshot wins: do not POST empty `[]` if a Clip stream is not 200
- On `bridgeid` change: clear NVS recipes/`rev` **before** the next poll

## Code conventions

- Arduino `.ino` + small `.h` files; no PlatformIO for this project
- English identifiers; comments in Spanish if they explain intent
- Digital input channels: each channel has its own recipe; see hue-switch-console `docs/definiciones.md` and `docs/device-api.md`
- Hue API: local HTTPS Clip v2, not the cloud
- Do not impersonate Hue accessories or use Zigbee on this sketch
- Scratch notes (`docs/_audit-*.md`, `docs/_review-*.md`) are not spec. Delete them once used. Do not commit them.
