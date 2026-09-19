# hue-simple-switch

Interruptor simple Wi-Fi para **Seeed XIAO ESP32-C6**. Controla luces Philips Hue por la API local (no Zigbee).

## Setup

1. Copia `config.example.h` a `config.h` y rellena SSID, password, IP del Bridge y application key de Hue.
2. Arduino IDE 2.3.10: abre `hue-simple-switch.ino`, placa **XIAO_ESP32C6**.
3. O con arduino-cli: `arduino-cli compile --profile xiao-c6 .`

`config.h` no se sube a git.
