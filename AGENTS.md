# hue-simple-switch

Wi-Fi wall switch firmware for Seeed XIAO ESP32-C6. Calls the Philips Hue local API. Not Zigbee.

## Hardware

- Board: Seeed Studio XIAO ESP32-C6
- Arduino IDE board name: `XIAO_ESP32C6`
- FQBN: `esp32:esp32:XIAO_ESP32C6`
- Core: Arduino-ESP32 **3.3.12** (already installed in `%LOCALAPPDATA%\Arduino15`)
- Flash: 4 MB, default partition scheme (not Zigbee)
- USB CDC on boot: Enabled (default for this board)

This sketch lives in the Arduino IDE sketchbook (`directories.user` = `C:\Users\tinei\Arduino`). The parent folder is **not** a git repo. GigaDash stays in `OneDrive\Documents\Arduino`.

## Secrets

- Real Wi-Fi / Hue values go in `config.h` (gitignored).
- `config.example.h` is the template that is committed.
- Never put SSID, passwords, or Hue keys in the `.ino` or in git.

If `config.h` is missing: `copy config.example.h config.h` and edit it.

## Build (arduino-cli)

Use the same Arduino15 data dir as the IDE so the 3.3.12 core is reused.

```
arduino-cli compile --profile xiao-c6 .
arduino-cli upload  --profile xiao-c6 -p COMx .
arduino-cli monitor -p COMx -c baudrate=115200
```

Replace `COMx` with the XIAO port (`arduino-cli board list`).

## Arduino IDE 2.3.10

- File → Open this folder (`hue-simple-switch.ino`)
- Board: `XIAO_ESP32C6` (esp32)
- Do not use the Zigbee partition scheme
- Libraries: none beyond the ESP32 core (`WiFi`, `HTTPClient`)

## Code conventions

- Arduino `.ino` + small `.h` files; no PlatformIO for this project
- English identifiers; comments in Spanish if they explain intent
- Keep the first milestone tiny: one button (or BOOT) → one Hue light on/off
- Hue API: local HTTPS Clip v2, not the cloud
- Do not impersonate Hue accessories or use Zigbee on this sketch
