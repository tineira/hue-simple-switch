# Firmware artifacts (simple / ESP32-C6)

Product installer binary for hue.tineira.com. Empty `WIFI_SSID` / `WIFI_PASSWORD` / `CONSOLE_URL` / `CONSOLE_TOKEN`, `SERIAL_DEBUG=0`. Wi-Fi is Arduino STA (Improv). Token and URL go in NVS namespace `console`.

## Compile

Sketch folder must be named `hue-simple-switch` (matches `hue-simple-switch.ino`). Profile: `sketch.yaml` `xiao-c6` (`esp32:esp32:XIAO_ESP32C6`, Arduino-ESP32 3.3.12). Partition table: sketch `partitions.csv` (min_spiffs: 1.9MB APP ×2, app at `0x10000`). USB CDC on boot is the board default.

```
copy config.product.h config.h
arduino-cli compile --profile xiao-c6 --export-binaries --build-property upload.maximum_size=1966080 .
```

`--export-binaries` writes under `build/esp32.esp32.XIAO_ESP32C6/`. `upload.maximum_size` matches the min_spiffs app slot (boards.txt still advertises the smaller default 4MB scheme).

Stage stable names in `dist/installer/` (gitignored; do not commit bins here):

| Export | Installer |
| --- | --- |
| `hue-simple-switch.ino.bootloader.bin` | `bootloader.bin` |
| `hue-simple-switch.ino.partitions.bin` | `partitions.bin` |
| `boot_app0.bin` | `boot_app0.bin` |
| `hue-simple-switch.ino.bin` | `firmware.bin` |

CI (`.github/workflows/firmware.yml`) copies `config.product.h` → `config.h`, compiles on push to `main` / `workflow_dispatch`, uploads those four files, and refreshes GitHub Release tag `usb-installer` (version `0.2.9`).

`FIRMWARE_VERSION` is `0.2.9` (see `console.h`).

## Manifest parts

`chipFamily`: `ESP32-C6`. Offsets from this compile’s `flash_args` (esptool write order). Do not erase flash (NVS survives).

| Part | File | Offset |
| --- | --- | --- |
| bootloader | `hue-simple-switch.ino.bootloader.bin` | `0x0` |
| partitions | `hue-simple-switch.ino.partitions.bin` | `0x8000` |
| boot_app0 | `boot_app0.bin` | `0xe000` |
| app | `hue-simple-switch.ino.bin` | `0x10000` |

Recorded from `arduino-cli compile --profile xiao-c6 --export-binaries` on 2026-09-20, core 3.3.12, `flash_args`:

```
--flash-mode dio --flash-freq 80m --flash-size 4MB
0x0 hue-simple-switch.ino.bootloader.bin
0x8000 hue-simple-switch.ino.partitions.bin
0xe000 boot_app0.bin
0x10000 hue-simple-switch.ino.bin
```
