# hue-simple-switch

Interruptor simple Wi-Fi para **Seeed XIAO ESP32-C6**. Controla **una** lámpara Philips Hue por la API local Clip v2 (HTTPS). No es Zigbee.

Pulsar **BOOT** (GPIO9) hace toggle on/off.

## Setup

1. Copia `config.example.h` a `config.h` y rellena al menos:
   - `WIFI_SSID` / `WIFI_PASSWORD` (red **2.4 GHz**)
   - `HUE_LIGHT_ID` (UUID Clip v2 de la lámpara)
2. IP del Bridge y application key son opcionales: el XIAO busca el Bridge por mDNS (`_hue._tcp`) y puede emparejar la key (LED parpadea → pulsa el botón del Bridge). Quedan en flash. BOOT 3 s = volver a emparejar.
3. Arduino IDE 2.3.10: abre `hue-simple-switch.ino`, placa **XIAO_ESP32C6**.
4. O con arduino-cli: `arduino-cli compile --profile xiao-c6 .`

Listar lámparas:

```
curl -k -H "hue-application-key: KEY" https://BRIDGE_IP/clip/v2/resource/light
```

`config.h` no se sube a git.
